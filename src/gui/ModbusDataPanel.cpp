#include "pch.hpp"
#include "GridBuilder.hpp"
#include "StatusLabel.hpp"
#include "Prompts.hpp"
#include "MenuCommand.hpp"
#include "ModbusConditionalColors.hpp"
#include "ModbusRegisterValueCodec.hpp"
#include "ModbusCustomCommand.hpp"
#include <wx/dcbuffer.h>
#include <cmath>
#include "MainFrameAccess.hpp"

using namespace std::chrono_literals;

wxBEGIN_EVENT_TABLE(ModbusDataPanel, wxPanel)
EVT_GRID_CELL_CHANGED(ModbusDataPanel::OnCellValueChanged)
EVT_SIZE(ModbusDataPanel::OnSize)
EVT_GRID_CELL_RIGHT_CLICK(ModbusDataPanel::OnCellRightClick)
EVT_GRID_LABEL_LEFT_CLICK(ModbusDataPanel::OnGridLabelLeftClick)
EVT_GRID_LABEL_RIGHT_CLICK(ModbusDataPanel::OnGridLabelRightClick)
EVT_CHAR_HOOK(ModbusDataPanel::OnKeyDown)
wxEND_EVENT_TABLE()

namespace
{
// !\brief A register's bitfield mapping as the shared editor takes it.
//
// ModbusMap already inherits TextStyle, so the presentation half passes
// straight through; the Modbus-specific editor this replaced copied it field by
// field, in code identical to the CAN editor's.
std::vector<gui::BitFieldRow> ToEditorRows(const ModbusBitfieldInfo& info)
{
    std::vector<gui::BitFieldRow> rows;
    rows.reserve(info.size());
    for(const auto& [label, value, mapping] : info)
    {
        gui::BitFieldRow row;
        row.label = label;
        row.value = value;
        if(mapping)
        {
            row.tooltip = mapping->m_Description;
            row.style = static_cast<const TextStyle&>(*mapping);
        }
        rows.push_back(std::move(row));
    }
    return rows;
}

// !\brief Run `fn` on one item with the handler's model lock held.
// !\return false when `index` no longer exists, which is what a stale row from
//          before a device change looks like.
//
// `fn` must not call back into a handler method that takes the model lock; read
// what you need into locals and act after this returns.
template <typename F>
bool WithItem(ModbusEntryHandler& handler, ModbusEntryHandler::Table table, size_t index, F&& fn)
{
    return handler.WithTable(table, [&](auto& items) -> bool
    {
        if(index >= items.size() || !items[index])
            return false;
        fn(*items[index]);
        return true;
    });
}
}

ModbusItemPanel::ModbusItemPanel(wxWindow* parent, ModbusEntryHandler& handler,
    const wxString& header_name, ModbusEntryHandler::Table table, bool is_read_only)
    : m_handler(handler), m_table(table), m_isReadOnly(is_read_only)
{
	static_box = new wxStaticBoxSizer(wxVERTICAL, parent, header_name);
    static_box->GetStaticBox()->SetFont(static_box->GetStaticBox()->GetFont().Bold());
    static_box->GetStaticBox()->SetForegroundColour(wxColor(235, 52, 204));

    /* In ModbusGridCol order. */
    static constexpr gui::GridColumn kColumns[]{
        { "Name", 130 },  // Modbus_Name
        { "Value" },      // Modbus_Value
    };
    static_assert(std::size(kColumns) == ModbusGridCol::Modbus_Max);

    m_grid = gui::BuildGrid(parent, gui::GridSpec{
        .size = wxSize(250, 700),
        .initial_rows = 0,
        .columns = kColumns,
        .selection_mode = wxGrid::wxGridSelectCells,
        .row_label_width = 60,
    });

    //m_grid->HideRowLabels();
    static_box->Add(m_grid);

    UpdatePanel();
}

void ModbusItemPanel::AddItem(size_t item_index, const ModbusItem& item)
{
    const ModbusItem* e = &item;
    const int num_row = gui::AppendRow(*m_grid);
    if (e->GetSize() == 1)
        m_grid->SetRowLabelValue(num_row, wxString::Format("%lld", e->m_Offset));
    else
        m_grid->SetRowLabelValue(num_row, wxString::Format("%lld - %lld", e->m_Offset, e->m_Offset + (e->GetSize() - 1)));

    grid_to_entry[static_cast<uint16_t>(num_row)] = item_index;

    m_grid->SetCellValue(wxGridCellCoords(num_row, ModbusGridCol::Modbus_Name), e->m_Name);
    m_grid->SetCellValue(wxGridCellCoords(num_row, ModbusGridCol::Modbus_Value), wxString::Format("%lld", e->m_Value.Integer()));

    gui::ApplyEntryStyle(*m_grid, static_cast<int>(num_row), ModbusGridCol::Modbus_Max, gui::StyleOf(*e));

    if (m_isReadOnly)
        m_grid->SetReadOnly(num_row, ModbusGridCol::Modbus_Value, true);
}

std::optional<size_t> ModbusItemPanel::ItemIndexForRow(int row) const
{
    if(row < 0)
        return std::nullopt;
    const auto entry = grid_to_entry.find(static_cast<uint16_t>(row));
    return entry == grid_to_entry.end() ? std::nullopt : std::optional<size_t>{ entry->second };
}

void ModbusItemPanel::UpdatePanel()
{
    if(m_grid->GetNumberRows())
        m_grid->DeleteRows(0, m_grid->GetNumberRows());

    grid_to_entry.clear();

    const uint8_t default_fav_level = m_handler.GetFavouriteLevel();

    /* Building the grid walks the whole table, so it happens once under the
       model lock rather than row by row against a reference the worker is
       free to be writing. */
    m_handler.WithTable(m_table, [&](const ModbusItemType& items)
    {
        for(size_t index = 0; index < items.size(); ++index)
        {
            const auto& e = items[index];
            if(!e || default_fav_level > e->m_FavLevel)
                continue;
            if(!search_pattern.empty() && !boost::icontains(e->m_Name, search_pattern))
                continue;
            AddItem(index, *e);
        }
    });
}

void ModbusItemPanel::ApplyPendingChanges()
{
    const auto changed_rows = m_pendingChanges.Take();
    for(uint8_t changed_index : changed_rows)
    {
        const auto row = std::ranges::find_if(grid_to_entry,
            [changed_index](const auto& entry) { return entry.second == changed_index; });
        if(row == grid_to_entry.end())
            continue;

        if(const auto render = m_handler.RenderItem(m_table, changed_index))
            ShowRender(row->first, *render);
    }
}

void ModbusItemPanel::RefreshItemValues(bool is_clear)
{
    const int num_rows = m_grid->GetNumberRows();
    for(int row = 0; row != num_rows; row++)
    {
        const auto index = ItemIndexForRow(row);
        if(!index)
            continue;

        if(is_clear)
        {
            ClearRow(row);
            continue;
        }
        if(const auto render = m_handler.RenderItem(m_table, *index))
            ShowRender(row, *render);
    }
}

// !\brief Paint one row from a cell the handler rendered under its lock.
//
// This was UpdateItem, which took a ModbusItem* and did the formatting itself:
// six numeric formats, scaling, float precision and the conditional-colour
// lookup, all on the UI thread against an item the polling worker could be
// writing at the same time. Deciding what a register reads as is domain logic
// and now lives in RenderModbusItem, where it is tested; this only paints.
void ModbusItemPanel::ShowRender(int num_row, const ModbusCellRender& render)
{
    m_grid->SetCellValue(wxGridCellCoords(num_row, ModbusGridCol::Modbus_Value), render.text);

    for(int column = 0; column < ModbusGridCol::Modbus_Max; ++column)
    {
        m_grid->SetCellTextColour(num_row, column,
            render.color ? RGB_TO_WXCOLOR(*render.color) : *wxBLACK);
        m_grid->SetCellBackgroundColour(num_row, column,
            render.background_color ? RGB_TO_WXCOLOR(*render.background_color)
                                    : gui::RowShade(static_cast<int>(num_row)));
    }

    m_grid->SetReadOnly(num_row, ModbusGridCol::Modbus_Value, m_isReadOnly);
}

void ModbusItemPanel::ClearRow(int num_row)
{
    m_grid->SetReadOnly(num_row, ModbusGridCol::Modbus_Name, true);
    m_grid->SetReadOnly(num_row, ModbusGridCol::Modbus_Value, true);
    m_grid->SetCellValue(wxGridCellCoords(num_row, ModbusGridCol::Modbus_Value), "0");
}

ModbusDataPanel::ModbusDataPanel(wxWindow* parent, ModbusEntryHandler& handler) :
    wxPanel(parent, wxID_ANY), m_handler(handler)
{
    /* This was 249 lines: four modal dialogs, four register grids and four
       rows of controls, every button's handler written inline. Each Build*
       below is one of the rows the layout already had. */
    CreateDialogs();

    wxBoxSizer* v_sizer = new wxBoxSizer(wxVERTICAL);
    BuildTablePanels(*v_sizer);
    BuildConnectionToolbar(*v_sizer);
    BuildPollingToolbar(*v_sizer);
    BuildActionToolbar(*v_sizer);
    BuildStatusBar(*v_sizer);

    SetSizer(v_sizer);
    Show();
}

// !\brief The four modal editors this panel opens from its context menus.
void ModbusDataPanel::CreateDialogs()
{
    m_StyleEditDialog = new ModbusDataEditDialog(this);
    m_ConditionalColorsDialog = new ModbusConditionalColorsDialog(this);
    m_ScalingDialog = new ModbusScalingDialog(this);
    m_BitfieldEditor = new gui::BitFieldEditorDialog(this, {});
}

// !\brief The row of four register tables.
void ModbusDataPanel::BuildTablePanels(wxSizer& parent)
{

    using Table = ModbusEntryHandler::Table;
    const NumModbusEntries counts = m_handler.EntryCounts();
    m_coil = new ModbusItemPanel(this, m_handler, wxString::Format("Coil Status - %zu", counts.coils), Table::Coils, false);
    m_input = new ModbusItemPanel(this, m_handler, wxString::Format("Input Status - %zu", counts.inputStatus), Table::InputStatus, true);
    m_holding = new ModbusItemPanel(this, m_handler, wxString::Format("Holding Registers - %zu", counts.holdingRegisters), Table::Holding, false);
    m_inputReg = new ModbusItemPanel(this, m_handler, wxString::Format("Input Registers - %zu", counts.inputRegisters), Table::Input, true);


    m_hSizer = new wxBoxSizer(wxHORIZONTAL);

    m_hSizer->Add(m_coil->static_box);
    m_hSizer->Add(m_input->static_box);
    m_hSizer->Add(m_holding->static_box);
    m_hSizer->Add(m_inputReg->static_box);

    parent.Add(m_hSizer);
}

// !\brief Start/stop, polling rate, slave id and the transport settings.
void ModbusDataPanel::BuildConnectionToolbar(wxSizer& parent)
{
    wxBoxSizer* h_sizer2 = new wxBoxSizer(wxHORIZONTAL);
    m_StartButton = new wxButton(this, wxID_ANY, wxT("Start"), wxDefaultPosition, wxDefaultSize, 0);
    m_StartButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent&)
        {

            wxString tcp_ip = m_TcpIp->GetValue();
            uint16_t tcp_port = m_TcpPort->GetValue();
            uint16_t com_port = m_ComPort->GetValue();
            bool is_tcp = m_UseTcp->IsChecked();
            wxString default_branch = m_DefaultBranch->GetValue();

            m_handler.GetSerial().SetTcpIp(tcp_ip.ToStdString());
            m_handler.GetSerial().SetTcpPort(tcp_port);
            m_handler.GetSerial().SetComPort(com_port);
            m_handler.GetSerial().SetTcp(is_tcp);
            m_handler.SetDefaultBranch(default_branch.ToStdString());
            m_handler.SetPollingStatus(true);
        });
    h_sizer2->Add(m_StartButton);

    m_StopButton = new wxButton(this, wxID_ANY, wxT("Stop"), wxDefaultPosition, wxDefaultSize, 0);
    m_StopButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent&)
        {
            m_handler.SetPollingStatus(false);

        });
    h_sizer2->Add(m_StopButton);

    m_SaveButton = new wxButton(this, wxID_ANY, wxT("Save"), wxDefaultPosition, wxDefaultSize, 0);
    m_SaveButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent&)
        {
            m_handler.Save();
        });
    h_sizer2->Add(m_SaveButton);

    h_sizer2->AddSpacer(10);
    h_sizer2->Add(new wxStaticText(this, wxID_ANY, "Polling rate [ms]: "));
    h_sizer2->AddSpacer(5);
    m_PollingRate = new wxSpinCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 50, 10000, m_handler.GetPollingRate());
    m_PollingRate->Bind(wxEVT_COMMAND_SPINCTRL_UPDATED, [this](wxSpinEvent& event)
        {
            int new_value = event.GetValue();
            m_handler.SetPollingRate(new_value);
        });

    h_sizer2->Add(m_PollingRate);

    h_sizer2->AddSpacer(10);
    h_sizer2->Add(new wxStaticText(this, wxID_ANY, "Slave ID:"), 0, wxALIGN_CENTER_VERTICAL);
    m_SlaveId = new wxSpinCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
        wxSP_ARROW_KEYS, 0, 247, m_handler.GetSlaveId());
    m_SlaveId->Bind(wxEVT_SPINCTRL, [this](wxSpinEvent& event)
        { m_handler.SetSlaveId(static_cast<uint8_t>(event.GetValue())); });
    h_sizer2->Add(m_SlaveId);

    h_sizer2->AddSpacer(10);
    h_sizer2->Add(new wxStaticText(this, wxID_ANY, "Response timeout [ms]: "));
    h_sizer2->AddSpacer(5);
    m_ResponseTimeout = new wxSpinCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 50, 10000, m_handler.GetSerial().m_ResponseTimeout);
    m_ResponseTimeout->Bind(wxEVT_COMMAND_SPINCTRL_UPDATED, [this](wxSpinEvent& event)
        {
            int new_value = event.GetValue();
            m_handler.GetSerial().m_ResponseTimeout = new_value;
        });
    h_sizer2->Add(m_ResponseTimeout);
    h_sizer2->AddSpacer(30);

    m_Clear = new wxButton(this, wxID_ANY, wxT("Clear"), wxDefaultPosition, wxDefaultSize, 0);
    m_Clear->Bind(wxEVT_BUTTON, [this](wxCommandEvent&)
        {
            m_handler.ClearValues();

            if (m_coil)
                m_coil->RefreshItemValues(true);
            if (m_input)
                m_input->RefreshItemValues(true);
            if (m_holding)
                m_holding->RefreshItemValues(true);
            if (m_inputReg)
                m_inputReg->RefreshItemValues(true);
        });
    h_sizer2->Add(m_Clear);

    parent.Add(h_sizer2);
    parent.AddSpacer(5);
}

// !\brief Favourite level, search and the device selector.
void ModbusDataPanel::BuildPollingToolbar(wxSizer& parent)
{
    wxBoxSizer* h_sizer3 = new wxBoxSizer(wxHORIZONTAL);
    h_sizer3->AddSpacer(235);
    h_sizer3->Add(new wxStaticText(this, wxID_ANY, "TCP IP:"));
    h_sizer3->AddSpacer(5);
    m_TcpIp = new wxTextCtrl(this, wxID_ANY, "");
    m_TcpIp->SetLabelText(m_handler.GetSerial().GetTcpIp());
    h_sizer3->Add(m_TcpIp);

    h_sizer3->AddSpacer(10);
    h_sizer3->Add(new wxStaticText(this, wxID_ANY, "TCP Port:"));
    h_sizer3->AddSpacer(5);
    m_TcpPort = new wxSpinCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 0, 65535, m_handler.GetSerial().GetTcpPort());
    h_sizer3->Add(m_TcpPort);

    h_sizer3->AddSpacer(10);
    h_sizer3->Add(new wxStaticText(this, wxID_ANY, "COM Port:"));
    h_sizer3->AddSpacer(5);
    m_ComPort = new wxSpinCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 0, 65535, m_handler.GetSerial().GetComPort());
    h_sizer3->Add(m_ComPort);

    h_sizer3->AddSpacer(10);
    h_sizer3->Add(new wxStaticText(this, wxID_ANY, "Use TCP?"));
    h_sizer3->AddSpacer(5);
    m_UseTcp = new wxCheckBox(this, wxID_ANY, "");
    m_UseTcp->SetValue(m_handler.GetSerial().IsTcp());
    h_sizer3->Add(m_UseTcp);

    parent.Add(h_sizer3);
}

// !\brief Import/export, the register editor buttons, the graph and the raw
// frame sender.
void ModbusDataPanel::BuildActionToolbar(wxSizer& parent)
{
    wxBoxSizer* h_sizer3_2 = new wxBoxSizer(wxHORIZONTAL);
    h_sizer3_2->Add(new wxStaticText(this, wxID_ANY, "Branch:"));
    h_sizer3_2->AddSpacer(5);
    m_DefaultBranch = new wxTextCtrl(this, wxID_ANY, "");
    m_DefaultBranch->SetLabelText(m_handler.GetDefaultBranch());
    h_sizer3_2->Add(m_DefaultBranch);

    const auto devices = m_handler.GetAvailableDevices();
    if(!devices.empty())
    {
        h_sizer3_2->AddSpacer(10);
        h_sizer3_2->Add(new wxStaticText(this, wxID_ANY, "Device:"), 0, wxALIGN_CENTER_VERTICAL);
        wxArrayString choices;
        for(const auto& device : devices)
            choices.Add(device);
        m_DeviceChoice = new wxChoice(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, choices);
        const int selected = m_DeviceChoice->FindString(m_handler.GetSelectedDevice());
        m_DeviceChoice->SetSelection(selected == wxNOT_FOUND ? 0 : selected);
        m_DeviceChoice->Bind(wxEVT_CHOICE, [this](wxCommandEvent& event)
        {
            if(!ChangeDevice(event.GetString().ToStdString()))
                wxMessageBox("Failed to load the selected Modbus device.", "Modbus", wxOK | wxICON_ERROR, this);
        });
        h_sizer3_2->Add(m_DeviceChoice);
    }

    h_sizer3_2->AddSpacer(10);
    m_ExportButton = new wxButton(this, wxID_ANY, wxT("Export"), wxDefaultPosition, wxDefaultSize, 0);
    m_ExportButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent&)
        {
            wxFileDialog saveFileDialog(this, "Export to file", "", "",
                "Text files (*.txt)|*.txt|All files (*.*)|*.*", wxFD_SAVE | wxFD_OVERWRITE_PROMPT);

            if (saveFileDialog.ShowModal() == wxID_CANCEL)
                return;

            wxString path = saveFileDialog.GetPath();
            std::filesystem::path file_path = path.ToStdString();
            m_handler.ExportValues(file_path);
        });
    h_sizer3_2->Add(m_ExportButton);

    m_ImportButton = new wxButton(this, wxID_ANY, wxT("Import"), wxDefaultPosition, wxDefaultSize, 0);
    m_ImportButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent&)
        {
            wxFileDialog openFileDialog(this, "Import from file", "", "",
                "Text files (*.txt)|*.txt|All files (*.*)|*.*", wxFD_OPEN | wxFD_FILE_MUST_EXIST);

            if (openFileDialog.ShowModal() == wxID_CANCEL)
                return;

            wxString path = openFileDialog.GetPath();
            std::filesystem::path file_path = path.ToStdString();
            m_handler.ImportValues(file_path);
        });
    h_sizer3_2->Add(m_ImportButton);

    auto make_edit_button = [&](const wxString& text, const wxString& tooltip, ModbusRegisterEditAction action)
    {
        auto* button = new wxButton(this, wxID_ANY, text, wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
        button->SetToolTip(tooltip);
        button->Bind(wxEVT_BUTTON, [this, action](wxCommandEvent&) { HandleRegisterEdit(action); });
        h_sizer3_2->Add(button);
        return button;
    };
    m_RegisterMoveUpButton = make_edit_button("Up", "Move the selected holding register up", ModbusRegisterEditAction::MoveUp);
    m_RegisterMoveDownButton = make_edit_button("Down", "Move the selected holding register down", ModbusRegisterEditAction::MoveDown);
    m_RegisterInsertButton = make_edit_button("Insert", "Insert a register after the selected row", ModbusRegisterEditAction::InsertAfter);
    m_RegisterDeleteButton = make_edit_button("Delete", "Delete the selected holding register", ModbusRegisterEditAction::Delete);

    m_GraphButton = new wxButton(this, wxID_ANY, "Graph");
    m_GraphButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent&)
    {
        if(!m_GraphFrame)
            m_GraphFrame = new ModbusGraphFrame(this);
        m_GraphFrame->Show();
        m_GraphFrame->Raise();
    });
    h_sizer3_2->Add(m_GraphButton);

    auto* custom = new wxButton(this, wxID_ANY, "Custom frame");
    custom->Bind(wxEVT_BUTTON, [this](wxCommandEvent&)
    {
        wxTextEntryDialog dialog(this, "Enter hexadecimal Modbus frame bytes", "Custom Modbus frame");
        if(dialog.ShowModal() != wxID_OK)
            return;
        auto bytes = modbus_custom_command::ParseHexBytes(dialog.GetValue().ToStdString());
        if(!bytes)
        {
            wxMessageBox(bytes.error(), "Invalid frame", wxOK | wxICON_ERROR, this);
            return;
        }
        auto& serial = m_handler.GetSerial();
        if(!serial.IsTcp())
            modbus_custom_command::AppendCheck(*bytes, modbus_custom_command::CheckType::Crc);
        const auto result = serial.SendCustomCommand(*bytes);
        wxMessageBox(result.response.empty() ? "No response" : modbus_custom_command::FormatHexBytes(result.response),
            result.Ok() ? "Modbus response" : "Modbus error", wxOK | (result.Ok() ? wxICON_INFORMATION : wxICON_ERROR), this);
    });
    h_sizer3_2->Add(custom);

    parent.Add(h_sizer3_2);
}

// !\brief The connection status line.
void ModbusDataPanel::BuildStatusBar(wxSizer& parent)
{
    wxBoxSizer* h_sizer4 = new wxBoxSizer(wxHORIZONTAL);
    m_ConnectionStatus = new wxStaticText(this, wxID_ANY, "Status: N/A");
    h_sizer4->Add(m_ConnectionStatus);
    parent.Add(h_sizer4);
}

bool ModbusDataPanel::ChangeDevice(const std::string& device)
{
    if(m_GraphFrame)
    {
        m_GraphFrame->Destroy();
        m_GraphFrame = nullptr;
    }
    if(!m_handler.ChangeDevice(device))
        return false;
    m_SlaveId->SetValue(m_handler.GetSlaveId());
    RefreshDevicePanels();
    return true;
}

void ModbusDataPanel::RefreshDevicePanels()
{
    const NumModbusEntries counts = m_handler.EntryCounts();
    m_coil->static_box->GetStaticBox()->SetLabelText(wxString::Format("Coil Status - %zu", counts.coils));
    m_input->static_box->GetStaticBox()->SetLabelText(wxString::Format("Input Status - %zu", counts.inputStatus));
    m_holding->static_box->GetStaticBox()->SetLabelText(wxString::Format("Holding Registers - %zu", counts.holdingRegisters));
    m_inputReg->static_box->GetStaticBox()->SetLabelText(wxString::Format("Input Registers - %zu", counts.inputRegisters));
    for(ModbusItemPanel* panel : { m_coil, m_input, m_holding, m_inputReg })
        panel->UpdatePanel();
    Layout();
}

void ModbusDataPanel::HandleRegisterEdit(ModbusRegisterEditAction action)
{
    if(!m_holding || m_holding->m_grid->GetGridCursorRow() < 0)
        return;
    const auto index = m_holding->ItemIndexForRow(m_holding->m_grid->GetGridCursorRow());
    if(!index)
        return;

    /* Adding or removing a register restructures the table, so the edit and the
       register recount both happen inside the lock rather than through a
       reference the polling worker also holds. */
    size_t register_count = 0;
    size_t item_count = 0;
    const auto result = m_handler.WithTable(ModbusEntryHandler::Table::Holding,
        [&](ModbusItemType& items)
        {
            const auto edit = EditModbusRegisterLayout(items, *index, action);
            if(edit.success)
            {
                register_count = GetModbusRegisterCount(items);
                item_count = items.size();
            }
            return edit;
        });

    if(!result.success)
    {
        wxMessageBox(result.error, "Register layout", wxOK | wxICON_WARNING, this);
        return;
    }
    m_handler.SetRegisterCount(ModbusEntryHandler::Table::Holding, register_count);
    m_holding->UpdatePanel();
    if(result.selected_index < item_count)
    {
        const auto row = std::ranges::find_if(m_holding->grid_to_entry,
            [&](const auto& entry) { return entry.second == result.selected_index; });
        if(row != m_holding->grid_to_entry.end())
            m_holding->m_grid->SetGridCursor(row->first, ModbusGridCol::Modbus_Name);
    }
}

// !\brief Which of the four register grids raised this event.
ModbusItemPanel* ModbusDataPanel::PanelForGrid(const wxObject* grid) const
{
    for(ModbusItemPanel* panel : { m_coil, m_input, m_holding, m_inputReg })
    {
        if(panel && grid == static_cast<const wxObject*>(panel->m_grid))
            return panel;
    }
    return nullptr;
}

// !\brief Apply an edited coil value cell.
void ModbusDataPanel::ApplyCoilEdit(ModbusItemPanel& panel, int row, size_t index, const wxString& text)
{
    /* Unguarded std::stoi on a cell the user can type anything into:
       std::invalid_argument escaped through wxWidgets. */
    const std::optional<int> parsed = utils::TryParse<int>(text.ToStdString());
    if(!parsed)
    {
        if(const auto render = m_handler.RenderItem(ModbusEntryHandler::Table::Coils, index))
            panel.ShowRender(row, *render);
        return;
    }

    m_handler.EditCoil(index, *parsed != 0);
}

// !\brief Apply an edited holding register value cell, in whatever format and
// scaling that register is configured for.
void ModbusDataPanel::ApplyHoldingEdit(ModbusItemPanel& panel, int row, size_t index, const wxString& text)
{
    /* Everything this needs from the item is copied out under the lock first:
       the edit methods below queue work on the handler, and the model lock is
       not recursive. */
    ModbusBitfieldType type{};
    ModbusValueFormat format{};
    ModbusValueScaling scaling{};
    bool scaling_active = false;
    if(!WithItem(m_handler, ModbusEntryHandler::Table::Holding, index,
        [&](const ModbusItem& item)
        {
            type = item.m_Type;
            format = item.m_Format;
            scaling = item.m_ValueScaling;
            scaling_active = IsModbusScalingActive(item);
        }))
    {
        return;
    }

    /* Was a try/catch over std::stof/stod/stoull. The control flow is the same,
       but "did it parse" is now a value rather than an exception, and stoull's
       silent acceptance of a trailing tail is gone: "12G" in a hex register was
       0x12. */
    const std::string value_text = text.ToStdString();
    bool accepted = false;

    if(type == MBT_FLOAT)
    {
        if(const auto parsed = utils::TryParse<float>(value_text))
        {
            m_handler.EditHoldingFloat(index, *parsed);
            accepted = true;
        }
    }
    else if(type == MBT_DOUBLE)
    {
        if(const auto parsed = utils::TryParse<double>(value_text))
        {
            m_handler.EditHoldingDouble(index, *parsed);
            accepted = true;
        }
    }
    else if(scaling_active)
    {
        if(const auto display = utils::TryParse<double>(value_text))
        {
            const double raw = GetModbusRawFromDisplayValue(scaling, *display);
            m_handler.EditHolding(index, static_cast<uint64_t>(std::llround(raw)));
            accepted = true;
        }
    }
    else
    {
        const int base = GetModbusNumericBase(format);
        if(const auto parsed = utils::TryParse<uint64_t>(value_text, utils::ParseMode::Whole, base))
        {
            m_handler.EditHolding(index, *parsed);
            accepted = true;
        }
    }

    if(accepted)
        return;

    if(const auto render = m_handler.RenderItem(ModbusEntryHandler::Table::Holding, index))
        panel.ShowRender(row, *render);
    wxMessageBox("The entered value is not valid for this register type.", "Modbus value",
        wxOK | wxICON_ERROR, this);
}

void ModbusDataPanel::OnCellValueChanged(wxGridEvent& ev)
{
    const int row = ev.GetRow();
    const int col = ev.GetCol();
    if(row == -1 || col == -1)  /* Header */
        return;

    /* This was four near-identical blocks, one per grid, each repeating the
       row-to-index lookup, the cell read and the Name case. Only the value
       column ever differed between them, and only for two of the four. */
    ModbusItemPanel* const panel = PanelForGrid(ev.GetEventObject());
    if(!panel)
        return;

    const auto index = panel->ItemIndexForRow(row);
    if(!index)
        return;

    const wxString new_value = panel->m_grid->GetCellValue(row, col);

    if(col == ModbusGridCol::Modbus_Name)
    {
        WithItem(m_handler, panel->TableId(), *index,
            [&](ModbusItem& item) { item.m_Name = new_value.ToStdString(); });
        return;
    }

    if(col != ModbusGridCol::Modbus_Value)
        return;

    /* Only coils and holding registers are writable. The two input tables are
       read-only on the wire, and their value cells are set read-only, so an
       edit there is not expected to arrive at all. */
    switch(panel->TableId())
    {
        case ModbusEntryHandler::Table::Coils:
            ApplyCoilEdit(*panel, row, *index, new_value);
            break;
        case ModbusEntryHandler::Table::Holding:
            ApplyHoldingEdit(*panel, row, *index, new_value);
            break;
        case ModbusEntryHandler::Table::InputStatus:
        case ModbusEntryHandler::Table::Input:
            break;
    }
}

// !\brief Show, and for holding registers edit, one register's bitfields.
void ModbusDataPanel::ShowItemBits(ModbusItemPanel* item_panel, size_t item_index)
{
    const bool is_holding = item_panel == m_holding;
    for(;;)
    {
        ModbusBitfieldInfo info = m_handler.GetMapForHolding(item_index, is_holding);
        if(info.empty())
        {
            wxMessageDialog(this, "There is no mapping found for the selected register", "Error", wxOK).ShowModal();
            return;
        }

        m_BitfieldEditor->ShowDialog(ToEditorRows(info));
        if(!is_holding)
            return;

        if(m_BitfieldEditor->IsAccepted())
            m_handler.ApplyEditingOnHolding(item_index, m_BitfieldEditor->GetOutput());

        if(m_BitfieldEditor->GetResult() != gui::BitFieldEditorResult::Apply)
            return;
    }
}

// !\brief Restyle one register's row.
void ModbusDataPanel::EditItemStyle(ModbusItemPanel* item_panel, size_t item_index)
{
    const auto table = item_panel->TableId();

    /* A modal dialog must not run with the model lock held, so the current
       style is copied out, shown, and written back. */
    gui::TextStyleEdit style;
    ModbusBitfieldType type{};
    uint8_t precision = 0;
    if(!WithItem(m_handler, table, item_index, [&](const ModbusItem& item)
    {
        style = { item.m_color, item.m_bg_color, item.m_is_bold, item.m_font_face, item.m_scale };
        type = item.m_Type;
        precision = item.m_FloatPrecision;
    }))
    {
        return;
    }

    m_StyleEditDialog->ShowDialog(style, type, precision);
    if(!m_StyleEditDialog->IsApplyClicked())
        return;

    const gui::TextStyleEdit edited = m_StyleEditDialog->GetStyle();
    const uint8_t edited_precision = m_StyleEditDialog->GetFloatPrecision();
    WithItem(m_handler, table, item_index, [&](ModbusItem& item)
    {
        item.m_color = edited.color;
        item.m_bg_color = edited.background_color;
        item.m_is_bold = edited.is_bold;
        item.m_scale = edited.scale;
        item.m_font_face = edited.font_face;
        item.m_FloatPrecision = edited_precision;
    });
    item_panel->UpdatePanel();
}

// !\brief Edit the value-dependent colour rules for one register.
void ModbusDataPanel::EditConditionalColors(ModbusItemPanel* item_panel, size_t item_index)
{
    const auto table = item_panel->TableId();

    std::array<ModbusConditionalColorRule, 2> rules{};
    if(!WithItem(m_handler, table, item_index,
        [&](const ModbusItem& item) { rules = item.m_ConditionalColors; }))
    {
        return;
    }

    m_ConditionalColorsDialog->ShowDialog(rules);
    if(!m_ConditionalColorsDialog->IsApplyClicked())
        return;

    WithItem(m_handler, table, item_index, [&](ModbusItem& item)
        { item.m_ConditionalColors = m_ConditionalColorsDialog->GetRules(); });
    item_panel->UpdatePanel();
}

// !\brief Change how many registers a value spans and how it is decoded.
void ModbusDataPanel::ChangeItemType(ModbusItemPanel* item_panel, size_t item_index, ModbusBitfieldType new_type)
{
    const auto table = item_panel->TableId();

    size_t register_count = 0;
    const auto result = m_handler.WithTable(table, [&](ModbusItemType& items)
    {
        const auto changed = ChangeModbusRegisterType(items, item_index, new_type);
        if(changed.success)
            register_count = GetModbusRegisterCount(items);
        return changed;
    });

    if(!result.success)
    {
        wxMessageBox(result.error, "Register type", wxOK | wxICON_WARNING, this);
        return;
    }
    m_handler.SetRegisterCount(table, register_count);
    item_panel->UpdatePanel();
}

// !\brief Edit the two-point linear scaling applied before display.
void ModbusDataPanel::EditItemScaling(ModbusItemPanel* item_panel, size_t item_index)
{
    const auto table = item_panel->TableId();

    ModbusValueScaling scaling{};
    ModbusBitfieldType type{};
    ModbusValueFormat format{};
    if(!WithItem(m_handler, table, item_index, [&](const ModbusItem& item)
    {
        scaling = item.m_ValueScaling;
        type = item.m_Type;
        format = item.m_Format;
    }))
    {
        return;
    }

    m_ScalingDialog->ShowDialog(scaling, type, format);
    if(!m_ScalingDialog->IsApplyClicked())
        return;

    WithItem(m_handler, table, item_index, [&](ModbusItem& item)
        { item.m_ValueScaling = m_ScalingDialog->GetScaling(); });
    item_panel->UpdatePanel();
}

// !\brief Add one register to the live value graph.
void ModbusDataPanel::WatchItemInGraph(ModbusItemPanel* item_panel, size_t item_index)
{
    const auto table = item_panel->TableId();

    wxString name;
    if(!WithItem(m_handler, table, item_index,
        [&](const ModbusItem& item) { name = item.m_Name; }))
    {
        return;
    }

    if(!m_GraphFrame)
        m_GraphFrame = new ModbusGraphFrame(this);
    if(!m_GraphFrame->AddItem(table, item_index, name))
    {
        wxMessageBox("This item is already watched or the graph is full.", "Modbus graph",
            wxOK | wxICON_INFORMATION, this);
    }
    m_GraphFrame->Show();
    m_GraphFrame->Raise();
}

// !\brief Change how one register's value is written out.
void ModbusDataPanel::SetItemFormat(ModbusItemPanel* item_panel, size_t item_index, ModbusValueFormat format)
{
    WithItem(m_handler, item_panel->TableId(), item_index,
        [format](ModbusItem& item) { item.m_Format = format; });
    item_panel->UpdatePanel();
}

void ModbusDataPanel::OnCellRightClick(wxGridEvent& ev)
{
    const int row = ev.GetRow(), col = ev.GetCol();
    if(row == -1 || col == -1)  /* Header */
        return;

    ModbusItemPanel* item_panel = nullptr;
    for(ModbusItemPanel* candidate : { m_coil, m_input, m_holding, m_inputReg })
    {
        if(candidate && ev.GetEventObject() == static_cast<wxObject*>(candidate->m_grid))
        {
            item_panel = candidate;
            break;
        }
    }
    if(item_panel == nullptr)
        return;

    const auto item_index = item_panel->ItemIndexForRow(row);
    if(!item_index)
        return;

    const size_t index = *item_index;
    /* Bit mapping, byte order, register type, scaling and the graph only mean
       something for a register table; coils and input status are single bits. */
    const auto is_register_table = [this, item_panel]
        { return item_panel == m_holding || item_panel == m_inputReg; };

    /* In the order the hand-written menu built them. */
    const gui::MenuEntry entries[]{
        gui::MenuCommand{ "&Show Bits", [=, this] { ShowItemBits(item_panel, index); },
            wxART_QUESTION, is_register_table },
        gui::MenuCommand{ "&Edit", [=, this] { EditItemStyle(item_panel, index); }, wxART_EDIT },
        gui::MenuCommand{ "Conditional colors...", [=, this] { EditConditionalColors(item_panel, index); } },
        gui::MenuRadioGroup{
            "Byte/word order",
            {
                { static_cast<int>(ModbusRegisterByteOrder::BigEndian), "Big endian" },
                { static_cast<int>(ModbusRegisterByteOrder::LittleEndian), "Little endian" },
                { static_cast<int>(ModbusRegisterByteOrder::BigEndianByteSwap), "Big endian, byte swapped" },
                { static_cast<int>(ModbusRegisterByteOrder::LittleEndianByteSwap), "Little endian, byte swapped" },
            },
            [=, this](int order)
            {
                WithItem(m_handler, item_panel->TableId(), index, [order](ModbusItem& item)
                    { item.m_NetworkByteOrder = static_cast<ModbusRegisterByteOrder>(order); });
            },
            is_register_table,
        },
        gui::MenuRadioGroup{
            "Register type",
            {
                { MBT_UI16, "uint16_t" }, { MBT_I16, "int16_t" },
                { MBT_UI32, "uint32_t" }, { MBT_I32, "int32_t" },
                { MBT_UI64, "uint64_t" }, { MBT_I64, "int64_t" },
                { MBT_FLOAT, "float" },   { MBT_DOUBLE, "double" },
            },
            [=, this](int chosen_type)
                { ChangeItemType(item_panel, index, static_cast<ModbusBitfieldType>(chosen_type)); },
            is_register_table,
        },
        gui::MenuCommand{ "Value scaling...", [=, this] { EditItemScaling(item_panel, index); },
            {}, is_register_table },
        gui::MenuCommand{ "Watch in graph", [=, this] { WatchItemInGraph(item_panel, index); },
            {}, is_register_table },
        gui::MenuCommand{ "&Dec", [=, this] { SetItemFormat(item_panel, index, ModbusValueFormat::MVF_DEC); }, wxART_PLUS },
        gui::MenuCommand{ "&Hex", [=, this] { SetItemFormat(item_panel, index, ModbusValueFormat::MVF_HEX); }, wxART_PLUS },
        gui::MenuCommand{ "&Bin", [=, this] { SetItemFormat(item_panel, index, ModbusValueFormat::MVF_BIN); }, wxART_PLUS },
    };

    gui::RunContextMenu(this, entries);
}


void ModbusDataPanel::OnGridLabelLeftClick(wxGridEvent& ev)
{
    int row = ev.GetRow(), col = ev.GetCol();
    if (row == -1 || col == -1)  /* Header */
        return;

    ModbusItemPanel* item_panel = nullptr;
    for(ModbusItemPanel* candidate : { m_coil, m_input, m_holding, m_inputReg })
    {
        if(candidate && ev.GetEventObject() == static_cast<wxObject*>(candidate->m_grid))
        {
            item_panel = candidate;
            break;
        }
    }

    if(item_panel != nullptr)
    {
        const auto index = item_panel->ItemIndexForRow(row);
        if(!index)
            return;

        std::string description;
        WithItem(m_handler, item_panel->TableId(), *index,
            [&](const ModbusItem& item) { description = item.m_Desc; });

        wxRect size(wxSize(100, 100));
        if(!description.empty())
        {
            tip = new wxTipWindow(item_panel->m_grid, description, 100, &tip, &size);
            tip->Show();
        }
    }
    ev.Skip();
}

void ModbusDataPanel::OnGridLabelRightClick(wxGridEvent& ev)
{
    if (ev.GetEventObject() == static_cast<wxObject*>(m_holding->m_grid))
    {
        const gui::MenuEntry entries[]{
            gui::MenuCommand{ "&Edit favourites", [this]
                {
                    if(const auto favourite_level = gui::PromptForByte(this, "Enter default favourite level for modbus list",
                           "Default favourite level", m_handler.GetFavouriteLevel(), "favourite level"))
                    {
                        m_handler.SetFavouriteLevel(*favourite_level);
                        m_holding->UpdatePanel();
                    }
                }, wxART_FOLDER },
        };
        gui::RunContextMenu(this, entries);
    }
    ev.Skip();
}

void ModbusDataPanel::OnKeyDown(wxKeyEvent& evt)
{
    if (evt.ControlDown())
    {
        switch (evt.GetKeyCode())
        {
            case 'F':
            {
                wxWindow* focus = wxWindow::FindFocus();
                if (focus == m_holding->m_grid)
                {
                    wxTextEntryDialog d(this, "Enter field name for what you want to filter", "Search for frame");
                    d.SetValue(m_holding->search_pattern);
                    int ret = d.ShowModal();
                    if (ret == wxID_OK)
                    {
                        m_holding->search_pattern = d.GetValue().ToStdString();
                        m_holding->UpdatePanel();
                    }
                    return;
                }
            }
        }
    }
    evt.Skip();
}

void ModbusDataPanel::OnSize(wxSizeEvent& event)
{
    event.Skip(true);
}

void ModbusDataPanel::On10MsTimer()
{
    const gui::LinkStatus status = m_handler.IsEnabled()
        ? gui::LinkStatusOf(m_handler.GetSerial().GetConnectionState())
        : gui::LinkStatus::Off;
    gui::SetLinkStatus(m_ConnectionStatus, "Status", status, gui::LinkWording::Verbose);

    for(ModbusItemPanel* panel : { m_coil, m_input, m_holding, m_inputReg })
        if(panel)
            panel->ApplyPendingChanges();

    if(m_GraphFrame && m_GraphFrame->IsShown())
    {
        const auto now = std::chrono::steady_clock::now();
        if(now - m_LastGraphSample >= 100ms)
        {
            m_GraphFrame->RecordSamples();
            m_LastGraphSample = now;
        }
    }
}
