#include "pch.hpp"
#include "ModbusSpecialRegisters.hpp"
#include "GridBuilder.hpp"
#include "MenuCommand.hpp"
#include "ModbusConditionalColors.hpp"
#include "ModbusRegisterValueCodec.hpp"
#include "ModbusCustomCommand.hpp"
#include <wx/dcbuffer.h>
#include <cmath>
#include "MainFrameAccess.hpp"


wxBEGIN_EVENT_TABLE(ModbusLogPanel, wxPanel)
EVT_SIZE(ModbusLogPanel::OnSize)
EVT_CHAR_HOOK(ModbusLogPanel::OnKeyDown)
wxEND_EVENT_TABLE()

wxBEGIN_EVENT_TABLE(ModbusSpecialRegisterPanel, wxPanel)
EVT_SIZE(ModbusSpecialRegisterPanel::OnSize)
EVT_CHAR_HOOK(ModbusSpecialRegisterPanel::OnKeyDown)
wxEND_EVENT_TABLE()

wxBEGIN_EVENT_TABLE(ModbusMasterPanel, wxPanel)
EVT_SIZE(ModbusMasterPanel::OnSize)
wxEND_EVENT_TABLE()

wxBEGIN_EVENT_TABLE(ModbusDataEditDialog, wxDialog)
EVT_BUTTON(wxID_APPLY, ModbusDataEditDialog::OnApply)
wxEND_EVENT_TABLE()

wxBEGIN_EVENT_TABLE(ModbusConditionalColorsDialog, wxDialog)
EVT_BUTTON(wxID_APPLY, ModbusConditionalColorsDialog::OnApply)
wxEND_EVENT_TABLE()

wxBEGIN_EVENT_TABLE(ModbusScalingDialog, wxDialog)
EVT_BUTTON(wxID_APPLY, ModbusScalingDialog::OnApply)
wxEND_EVENT_TABLE()

ModbusLogPanel::ModbusLogPanel(wxWindow* parent, ModbusEntryHandler& handler, IModbusRowSink& row_sink) :
    wxPanel(parent, wxID_ANY), m_handler(handler), m_RowSink(row_sink)
{
    wxBoxSizer* v_sizer = new wxBoxSizer(wxVERTICAL);

    wxBoxSizer* h_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_RecordingBar = std::make_unique<gui::LogRecordingBar>(this, h_sizer,
        gui::LogRecordingBar::Spec{
            .subject = "Modbus frames",
            .start = [this] { m_handler.StartRecording(); },
            .pause = [this] { m_handler.PauseRecording(); },
            .stop = [this] { m_handler.StopRecording(); },
            .clear = [this] { ClearRecordingsFromGrid(); m_handler.ClearRecording(); },
            .save_directory = "Modbus",
            .save_prefix = "ModbusLog",
            .save = [this](std::filesystem::path p) { m_handler.SaveRecordingToFile(p); },
            .auto_scroll = false  /* the one copy that defaulted off; preserved */,
        });

    v_sizer->Add(h_sizer);

    wxBoxSizer* h_sizer2 = new wxBoxSizer(wxHORIZONTAL);
    h_sizer2->Add(m_RecordingBar->SaveButton());

    v_sizer->Add(h_sizer2);

    static_box = new wxStaticBoxSizer(wxHORIZONTAL, this, "&Log :: TX: 0, RX: 0, Err: 0 - Total: 0");
    static_box->GetStaticBox()->SetFont(static_box->GetStaticBox()->GetFont().Bold());
    static_box->GetStaticBox()->SetForegroundColour(*wxBLUE);

    /* In ModbusLogGridCol order. */
    static constexpr gui::GridColumn kColumns[]{
        { "Time", 50 },  // ModbusLog_Time
        { "Dir", 30 },   // ModbusLog_Direction
        { "FC", 20 },    // ModbusLog_FCode
        { "Size", 30 },  // ModbusLog_DataSize
        { "Err", 70 },   // ModbusLog_ErrorType
        { "Data", 550 }, // ModbusLog_Data
    };
    static_assert(std::size(kColumns) == ModbusLogGridCol::ModbusLog_Max);

    m_grid = gui::BuildGrid(this, gui::GridSpec{
        .size = wxSize(800, 600),
        .initial_rows = 1,
        .columns = kColumns,
        .selection_mode = wxGrid::wxGridSelectRows,
        .default_cell_alignment = false,
        .row_label_width = 40,
    });

    static_box->Add(m_grid);
    v_sizer->Add(static_box, wxSizerFlags(1).Top().Expand());

    SetSizer(v_sizer);
    Show();
}

ModbusLogPanel::~ModbusLogPanel()
{
}

ModbusSpecialRegisterPanel::ModbusSpecialRegisterPanel(wxWindow* parent, ModbusEntryHandler& handler) :
    wxPanel(parent, wxID_ANY), m_handler(handler)
{
    //m_handler.SetValueObserver(this);

    wxBoxSizer* v_sizer = new wxBoxSizer(wxVERTICAL);
    wxBoxSizer* h_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_RecordingBar = std::make_unique<gui::LogRecordingBar>(this, h_sizer,
        gui::LogRecordingBar::Spec{
            .subject = "Modbus frames",
            .start = [this] { m_handler.StartRecording(); },
            .pause = [this] { m_handler.PauseRecording(); },
            .stop = [this] { m_handler.StopRecording(); },
            .clear = [this] { ClearRecordingsFromGrid(); m_handler.ClearRecording(); },
            .save_directory = "Modbus",
            .save_prefix = "ModbusSpecialLog",
            .save_tooltip = "Save special recording to file",
            .save = [this](std::filesystem::path p) { m_handler.SaveSpecialRecordingToFile(p); },
            .auto_scroll = true,
        });

    v_sizer->Add(h_sizer);

    wxBoxSizer* h_sizer2 = new wxBoxSizer(wxHORIZONTAL);
    h_sizer2->Add(m_RecordingBar->SaveButton());

    v_sizer->Add(h_sizer2);

    static_box = new wxStaticBoxSizer(wxHORIZONTAL, this, "&Log :: TX: 0, RX: 0, Err: 0 - Total: 0");
    static_box->GetStaticBox()->SetFont(static_box->GetStaticBox()->GetFont().Bold());
    static_box->GetStaticBox()->SetForegroundColour(*wxBLUE);

    /* In ModbusSpecialRegisterCol order. */
    static constexpr gui::GridColumn kColumns[]{
        { "Time", 50 },  // ModbusSpec_Time
        { "Hex", 300 },  // ModbusSpec_DataHex
        { "Data", 300 }, // ModbusSpec_Data
    };
    static_assert(std::size(kColumns) == ModbusSpecialRegisterCol::ModbusSpec_Max);

    m_grid = gui::BuildGrid(this, gui::GridSpec{
        .size = wxSize(800, 600),
        .initial_rows = 1,
        .columns = kColumns,
        .selection_mode = wxGrid::wxGridSelectRows,
        .default_cell_alignment = false,
        .row_label_width = 40,
        .double_buffered = true,
    });

    static_box->Add(m_grid);
    v_sizer->Add(static_box, wxSizerFlags(1).Top().Expand());

    SetSizer(v_sizer);
    Show();
}

void ModbusSpecialRegisterPanel::On10MsTimer()
{
    ModbusLogSlice<EventLogEntry> slice = m_handler.FrameLog().TakeEventsSince(m_LogCursor);
    if(slice.restarted)
        ClearRecordingsFromGrid();

    for(const EventLogEntry& entry : slice.entries)
        AppendLog(entry.last_execution, entry.data);
}

void ModbusSpecialRegisterPanel::OnKeyDown(wxKeyEvent& evt)
{

}

void ModbusSpecialRegisterPanel::AppendLog(std::chrono::steady_clock::time_point t1, const std::vector<uint16_t>& data)
{
    /* The row count before the append: the auto-scroll below moves by it,
       which reaches the bottom of a grid that is still growing. */
    const int num_rows = m_grid->GetNumberRows();
    gui::EnsureRow(*m_grid, cnt);

    uint64_t elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - m_handler.GetStartTime()).count();
    m_grid->SetCellValue(wxGridCellCoords(cnt, ModbusSpecialRegisterCol::ModbusSpec_Time), wxString::Format("%.3lf", static_cast<double>(elapsed) / 1000.0));

    std::string hex;
    utils::ConvertHexBufferToString(data, hex);
    m_grid->SetCellValue(wxGridCellCoords(cnt, ModbusSpecialRegisterCol::ModbusSpec_DataHex), hex);

    /* The block's layout lives in modbus_special now, where it is length
       checked and tested. The panel used to index data[8] whatever the size,
       so a short block was undefined behaviour; it renders as its hex and this
       note instead. */
    const auto record = modbus_special::Decode(data);
    m_grid->SetCellValue(wxGridCellCoords(cnt, ModbusSpecialRegisterCol::ModbusSpec_Data),
        record ? modbus_special::FormatRecord(*record)
               : std::string("(block too short to decode)"));

    /* Was (cnt % 2) == 0, the opposite stripe phase to every other grid. */
    gui::ApplyRowShading(*m_grid, static_cast<int>(cnt), ModbusSpecialRegisterCol::ModbusSpec_Max);

    m_grid->Update();

    if(m_RecordingBar->IsAutoScroll())
        m_grid->ScrollLines(num_rows);

    cnt++;
}

void ModbusSpecialRegisterPanel::ClearRecordingsFromGrid()
{
    /* See ModbusLogPanel::ClearRecordingsFromGrid. */
    int num_rows = m_grid->GetNumberRows();
    if(num_rows)
        m_grid->DeleteRows(0, num_rows);
    cnt = 0;
}

void ModbusSpecialRegisterPanel::OnSize(wxSizeEvent& event)
{
    event.Skip(true);
}

void ModbusLogPanel::AppendLog(std::chrono::steady_clock::time_point& t1, uint8_t direction, uint8_t fcode, uint8_t error, const std::vector<uint8_t>& data)
{
    /* The row count before the append: the auto-scroll below moves by it,
       which reaches the bottom of a grid that is still growing. */
    const int num_rows = m_grid->GetNumberRows();
    gui::EnsureRow(*m_grid, cnt);

    uint64_t elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - m_handler.GetStartTime()).count();
    m_grid->SetCellValue(wxGridCellCoords(cnt, ModbusLogGridCol::ModbusLog_Time), wxString::Format("%.3lf", static_cast<double>(elapsed) / 1000.0));

    std::string hex;
    utils::ConvertHexBufferToString(data, hex);
    m_grid->SetCellValue(wxGridCellCoords(cnt, ModbusLogGridCol::ModbusLog_Direction), direction == CAN_LOG_DIR_TX ? "TX" : "RX");
    m_grid->SetCellValue(wxGridCellCoords(cnt, ModbusLogGridCol::ModbusLog_FCode), wxString::Format("%X", fcode));

    wxString error_type;
    switch (error)
    {
        case ModbusErrorType::MB_ERR_CRC:
        {
            error_type = "CRC";
            break;
        }
        case ModbusErrorType::MB_ERR_TIMEOUT:
        {
            error_type = "TO";
            break;
        }
        case ModbusErrorType::MB_ERR_ILLEGAL_FUNCTION:
        {
            error_type = "MB_ERR_ILLEGAL_FUNCTION";
            break;
        }
        case ModbusErrorType::MB_ERR_ILLEGAL_DATA_ADDRESS:
        {
            error_type = "MB_ERR_ILLEGAL_DATA_ADDRESS";
            break;
        }
        case ModbusErrorType::MB_ERR_ILLEGAL_DATA_VALUE:
        {
            error_type = "MB_ERR_ILLEGAL_DATA_VALUE";
            break;
        }
        case ModbusErrorType::MB_ERR_SLAVE_DEVICE_FAILURE:
        {
            error_type = "MB_ERR_SLAVE_DEVICE_FAILURE";
            break;
        }
        case ModbusErrorType::MB_ERR_ACK:
        {
            error_type = "MB_ERR_ACK";
            break;
        }
        case ModbusErrorType::MB_ERR_SLAVE_DEVICE_BUSY:
        {
            error_type = "MB_ERR_SLAVE_DEVICE_BUSY";
            break;
        }
        case ModbusErrorType::MB_ERR_NAK:
        {
            error_type = "MB_ERR_NAK";
            break;
        }
        case ModbusErrorType::MB_ERR_MEMORY_PARITY_ERROR:
        {
            error_type = "MB_ERR_MEMORY_PARITY_ERROR";
            break;
        }
        case ModbusErrorType::MB_ERR_GATEWAY_UNAVAILABLE:
        {
            error_type = "MB_ERR_GATEWAY_UNAVAILABLE";
            break;
        }
        case ModbusErrorType::MB_ERR_GATEWAY_TARGET_FAILED:
        {
            error_type = "MB_ERR_GATEWAY_TARGET_FAILED";
            break;
        }
    }
    m_grid->SetCellValue(wxGridCellCoords(cnt, ModbusLogGridCol::ModbusLog_Data), hex + " " + error_type);

    m_grid->SetCellValue(wxGridCellCoords(cnt, ModbusLogGridCol::ModbusLog_ErrorType), error_type);
    m_grid->SetCellValue(wxGridCellCoords(cnt, ModbusLogGridCol::ModbusLog_DataSize), wxString::Format("%lld", data.size()));

    if(m_RecordingBar->IsAutoScroll())
        m_grid->ScrollLines(num_rows);

    for(uint8_t i = 0; i != ModbusLogGridCol::ModbusLog_Max; i++)
    {
        m_grid->SetReadOnly(cnt, i, true);

        if(error == MB_ERR_OK)
            m_grid->SetCellBackgroundColour(cnt, i, (direction == CAN_LOG_DIR_RX) ? 0xE6E6E6 : 0xFFFFFF);
        else
            m_grid->SetCellBackgroundColour(cnt, i, *wxRED);
    }

    cnt++;
}

void ModbusLogPanel::OnMaxEntriesReached()
{
    ClearRecordingsFromGrid();
}

void ModbusLogPanel::RefreshItems()
{
    if(!wxIsMainThread())
    {
        CallAfter(&ModbusLogPanel::RefreshItems);
        return;
    }

    m_RowSink.RefreshWriteTargets();
}

void ModbusDataPanel::QueueRowChanges(IModbusValueObserver::Table table, std::vector<uint8_t> rows)
{
    const std::array<ModbusItemPanel*, 4> panels = { m_coil, m_input, m_holding, m_inputReg };
    const auto index = static_cast<size_t>(table);
    if(index < panels.size() && panels[index])
        panels[index]->QueueChanges(std::move(rows));
}

void ModbusDataPanel::RefreshWriteTargets()
{
    /* The two writable grids, re-rendered after a write goes out. Each row
       re-renders itself through the handler, which locks per read. */
    for(ModbusItemPanel* panel : { m_holding, m_coil })
    {
        if(panel)
            panel->RefreshItemValues(false);
    }
}

void ModbusLogPanel::QueueValueChanges(IModbusValueObserver::Table table, const std::vector<uint8_t>& rows)
{
    m_pendingValueChanges[static_cast<size_t>(table)].Add(rows);
}

void ModbusLogPanel::ClearRecordingsFromGrid()
{
    /* Empties the view only. Discarding the recording bumps the log's
       generation, and the next drain repopulates from the start of the new
       one, so the cursor does not need resetting here as well. */
    int num_rows = m_grid->GetNumberRows();
    if(num_rows)
        m_grid->DeleteRows(0, num_rows);
    cnt = 0;
}

void ModbusLogPanel::On10MsTimer()
{

    for(size_t index = 0; index < m_pendingValueChanges.size(); ++index)
        m_RowSink.QueueRowChanges(static_cast<IModbusValueObserver::Table>(index),
            m_pendingValueChanges[index].Take());

    if(m_LastShownTxCount != m_handler.GetTxFrameCount() || m_LastShownRxCount != m_handler.GetRxFrameCount() || m_LastShownErrCount != m_handler.GetErrFrameCount())
    {
        static_box->GetStaticBox()->SetLabelText(wxString::Format("Log :: TX: %lld, RX: %lld, ERR: %lld, Total: %lld", m_handler.GetTxFrameCount(), m_handler.GetRxFrameCount(),
            m_handler.GetErrFrameCount(), m_handler.GetTxFrameCount() + m_handler.GetRxFrameCount()));
    }

    m_LastShownTxCount = m_handler.GetTxFrameCount();
    m_LastShownRxCount = m_handler.GetRxFrameCount();
    m_LastShownErrCount = m_handler.GetErrFrameCount();

    ModbusLogSlice<ModbusLogEntry> slice = m_handler.FrameLog().TakeFramesSince(m_LogCursor);
    if(slice.restarted)
        ClearRecordingsFromGrid();

    for(ModbusLogEntry& entry : slice.entries)
        AppendLog(entry.last_execution, entry.direction, entry.fcode, static_cast<uint8_t>(entry.error_type), entry.data);
}

void ModbusLogPanel::OnSize(wxSizeEvent& event)
{
    event.Skip(true);
}

void ModbusLogPanel::OnKeyDown(wxKeyEvent& evt)
{
    if (evt.ControlDown())
    {
        switch (evt.GetKeyCode())
        {
            case 'C':
            {
                if (wxWindow::FindFocus() == m_grid &&
                    gui::CopySelectedRowsToClipboard(*m_grid, ModbusLogGridCol::ModbusLog_Max))
                {
                    PostAppNotification(SimpleNotification{SimpleNotificationKind::SelectedLogsCopied});
                }
                break;
            }
        }
    }
}

ModbusMasterPanel::ModbusMasterPanel(wxWindow* parent, ModbusEntryHandler& handler, const wxSize& notebook_size)
	: wxPanel(parent, wxID_ANY), m_handler(handler)
{
    wxSize client_size = GetClientSize();

    m_mgr.SetManagedWindow(this);
	m_notebook = new wxAuiNotebook(this, wxID_ANY, wxPoint(0, 0), notebook_size, wxAUI_NB_TOP | wxAUI_NB_TAB_SPLIT | wxAUI_NB_TAB_MOVE | wxAUI_NB_SCROLL_BUTTONS | wxAUI_NB_MIDDLE_CLICK_CLOSE | wxAUI_NB_TAB_EXTERNAL_MOVE | wxNO_BORDER);

    data_panel = new ModbusDataPanel(this, m_handler);
    log_panel = new ModbusLogPanel(this, m_handler, *data_panel);
    /* The owner wires the observer, the same way MainFrame wires LogPanel to
       the logger: the panel used to register itself from its own constructor
       and deregister in its own destructor. */
    m_handler.SetValueObserver(log_panel);
    special_panel = new ModbusSpecialRegisterPanel(this, m_handler);

    m_notebook->Freeze();
    m_notebook->AddPage(data_panel, "Data", false, wxArtProvider::GetBitmap(wxART_HELP_BOOK, wxART_OTHER, FromDIP(wxSize(16, 16))));
    m_notebook->AddPage(log_panel, "Log", false, wxArtProvider::GetBitmap(wxART_HELP_SETTINGS, wxART_OTHER, FromDIP(wxSize(16, 16))));
    m_notebook->AddPage(special_panel, "Special", false, wxArtProvider::GetBitmap(wxART_HELP_SETTINGS, wxART_OTHER, FromDIP(wxSize(16, 16))));
	m_notebook->Connect(wxEVT_COMMAND_AUINOTEBOOK_PAGE_CHANGED, wxAuiNotebookEventHandler(ModbusMasterPanel::Changeing), NULL, this);
    m_notebook->Split(0, wxLEFT);
    //m_notebook->Split(1, wxDOWN);
    //m_notebook->Split(2, wxDOWN);
    m_notebook->Thaw();
}

void ModbusMasterPanel::OnFrameResized(const wxSize& size)
{
	SetSize(size);
	if(m_notebook)
		m_notebook->SetSize(size);
}

ModbusMasterPanel::~ModbusMasterPanel()
{
    /* The handler outlives every panel: MyApp::OnExit destroys the frame
       before it releases the services. */
    m_handler.SetValueObserver(nullptr);
    m_mgr.UnInit();
}

void ModbusMasterPanel::UpdateSubpanels()
{

}

void ModbusMasterPanel::On10MsTimer()
{
    /* This used to hold the handler's model lock for the whole tick, which
       blocked the polling worker for the length of three panel repaints. Each
       read now takes the lock for exactly as long as it needs it. */
    if(data_panel)
        data_panel->On10MsTimer();
    if(log_panel)
        log_panel->On10MsTimer();
    if(special_panel)
        special_panel->On10MsTimer();
}

void ModbusMasterPanel::OnSize(wxSizeEvent& event)
{
	event.Skip(true);
}

void ModbusMasterPanel::Changeing(wxAuiNotebookEvent& event)
{
	int sel = event.GetSelection();
	if(sel == 0)
	{
		//comtcp_panel->Update();
	}
}

