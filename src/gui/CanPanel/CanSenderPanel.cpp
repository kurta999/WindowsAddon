#include "pch.hpp"
#include "utils/HexBytes.hpp"
#include "../MainFrameAccess.hpp"
#include "../GridBuilder.hpp"
#include "../MenuCommand.hpp"
#include "../Prompts.hpp"

wxBEGIN_EVENT_TABLE(CanSenderPanel, wxPanel)
EVT_SIZE(CanSenderPanel::OnSize)
EVT_GRID_CELL_CHANGED(CanSenderPanel::OnCellValueChanged)
/*
EVT_GRID_CELL_LEFT_CLICK(CanSenderPanel::OnCellLeftClick)
EVT_GRID_CELL_LEFT_DCLICK(CanSenderPanel::OnCellLeftDoubleClick)
*/
EVT_GRID_CELL_RIGHT_CLICK(CanSenderPanel::OnCellRightClick)
EVT_GRID_LABEL_RIGHT_CLICK(CanSenderPanel::OnGridLabelRightClick)
EVT_CHAR_HOOK(CanSenderPanel::OnKeyDown)
wxEND_EVENT_TABLE()

wxBEGIN_EVENT_TABLE(CanSenderEditDialog, wxDialog)
EVT_BUTTON(wxID_APPLY, CanSenderEditDialog::OnApply)
wxEND_EVENT_TABLE()

namespace
{
// !\brief Applies an edited log-level or favourite-level cell to `target`.
//
// The two columns behave identically: values above 255 clamp, and anything
// that is not a number leaves the entry alone and puts the old value back in
// the cell. Both used to spell that out inside their own try/catch, and both
// caught std::exception around a std::stoi call in a wx event handler.
void ApplyLevelCell(wxGrid* grid, int row, int column, const wxString& text, uint8_t& target)
{
    constexpr uint32_t max_level = std::numeric_limits<uint8_t>::max();
    const std::optional<uint32_t> parsed = utils::TryParse<uint32_t>(text.ToStdString());

    if(parsed)
        target = static_cast<uint8_t>(std::min(*parsed, max_level));

    /* Rewrite the cell when it does not already show what was stored: the
       value was rejected, or it was clamped. */
    if(!parsed || *parsed > max_level)
        grid->SetCellValue(wxGridCellCoords(row, column), wxString::Format("%u", target));
}
}

CanSenderPanel::CanSenderPanel(wxWindow* parent, CanEntryHandler& handler, CanSerialPort& port)
    : wxPanel(parent, wxID_ANY), m_handler(handler), m_Port(port)
{
    wxBoxSizer* bSizer1 = new wxBoxSizer(wxVERTICAL);

    m_BitfieldEditor = new gui::BitFieldEditorDialog(this, {});
    m_LogForFrame = new CanLogForFrameDialog(this);
    m_UdsRawDialog = new CanUdsRawDialog(this);
    m_StyleEditDialog = new CanSenderEditDialog(this);

    {
        static_box_rx = new wxStaticBoxSizer(wxHORIZONTAL, this, "&Receive");
        static_box_rx->GetStaticBox()->SetFont(static_box_rx->GetStaticBox()->GetFont().Bold());
        static_box_rx->GetStaticBox()->SetForegroundColour(*wxBLUE);

        can_grid_rx = new CanGridRx(this);

        can_grid_rx->m_grid->DeleteRows(0, can_grid_rx->m_grid->GetNumberRows());
        can_grid_rx->cnt = 0;

        static_box_rx->Add(can_grid_rx->m_grid, 0, wxALL, 5);
        bSizer1->Add(static_box_rx, wxSizerFlags(0).Top());
    }

    {
        static_box_tx = new wxStaticBoxSizer(wxHORIZONTAL, this, "&Transmit");
        static_box_tx->GetStaticBox()->SetFont(static_box_tx->GetStaticBox()->GetFont().Bold());
        static_box_tx->GetStaticBox()->SetForegroundColour(*wxBLUE);

        can_grid_tx = new CanGrid(this);
        RefreshTx();

        Bind(wxEVT_CHAR_HOOK, &CanSenderPanel::OnKeyDown, this);

        static_box_tx->Add(can_grid_tx->m_grid, 0, wxALL, 5);
        bSizer1->Add(static_box_tx, 0, wxALL, 5);

        wxBoxSizer* h_sizer = new wxBoxSizer(wxHORIZONTAL);
        m_SingleShot = new wxButton(this, wxID_ANY, "One Shot", wxDefaultPosition, wxDefaultSize);
        m_SingleShot->SetToolTip("Single shot mode for selected CAN frame (or force sending if it's sent periodically)");
        m_SingleShot->Bind(wxEVT_BUTTON, [this](wxCommandEvent& event)
            {
                wxGrid* m_grid = can_grid_tx->m_grid;

                wxArrayInt rows = m_grid->GetSelectedRows();
                if(rows.empty()) return;

                for(auto& i : rows)
                {
                    CanTxEntry* entry = can_grid_tx->grid_to_entry[i];
                    entry->single_shot = true;
                    //m_grid->SetCellBackgroundColour(i, 0, *wxGREEN);
                }
            });
        h_sizer->Add(m_SingleShot);

        m_SendSelected = new wxButton(this, wxID_ANY, "Send selected", wxDefaultPosition, wxDefaultSize);
        m_SendSelected->SetToolTip("Start sending selected CAN frames (which period isn't 0)");
        m_SendSelected->Bind(wxEVT_BUTTON, [this](wxCommandEvent& event)
            {
                wxGrid* m_grid = can_grid_tx->m_grid;

                wxArrayInt rows = m_grid->GetSelectedRows();
                if(rows.empty()) return;

                for(auto& i : rows)
                {
                    CanTxEntry* entry = can_grid_tx->grid_to_entry[i];
                    entry->single_shot = false;
                    entry->send = true;
                }
            });
        h_sizer->Add(m_SendSelected);

        m_StopSelected = new wxButton(this, wxID_ANY, "Stop selected", wxDefaultPosition, wxDefaultSize);
        m_StopSelected->SetToolTip("Stop sending selected CAN frames (which period isn't 0)");
        m_StopSelected->Bind(wxEVT_BUTTON, [this](wxCommandEvent& event)
            {
                wxGrid* m_grid = can_grid_tx->m_grid;

                wxArrayInt rows = m_grid->GetSelectedRows();
                if(rows.empty()) return;

                for(auto& i : rows)
                {
                    CanTxEntry* entry = can_grid_tx->grid_to_entry[i];
                    entry->single_shot = false;
                    entry->send = false;
                }
            });
        h_sizer->Add(m_StopSelected);

        m_SendAll = new wxButton(this, wxID_ANY, "Send All", wxDefaultPosition, wxDefaultSize);
        m_SendAll->SetToolTip("Start send all CAN frame (which period isn't 0)");
        m_SendAll->Bind(wxEVT_BUTTON, [this](wxCommandEvent& event)
            {
                wxGrid* m_grid = can_grid_tx->m_grid;

                for(auto& i : can_grid_tx->grid_to_entry)
                {
                    i.second->single_shot = false;
                    i.second->send = true;
                }
            });
        h_sizer->Add(m_SendAll);

        m_StopAll = new wxButton(this, wxID_ANY, "Stop All", wxDefaultPosition, wxDefaultSize);
        m_StopAll->SetToolTip("Stop all CAN frame");
        m_StopAll->Bind(wxEVT_BUTTON, [this](wxCommandEvent& event)
            {
                wxGrid* m_grid = can_grid_tx->m_grid;

                for(auto& i : can_grid_tx->grid_to_entry)
                {
                    i.second->single_shot = false;
                    i.second->send = false;
                }
            });
        h_sizer->Add(m_StopAll);

        m_Add = new wxButton(this, wxID_ANY, "Add", wxDefaultPosition, wxDefaultSize);
        m_Add->SetToolTip("Add CAN frame below selection");
        m_Add->Bind(wxEVT_BUTTON, [this](wxCommandEvent& event)
            {
                wxGrid* m_grid = can_grid_tx->m_grid;

                wxArrayInt rows = m_grid->GetSelectedRows();
                if(rows.empty() || rows.size() > 1) return;

                /* Free-id search plus insert is the handler's invariant now; the
                   grid call that ran under the model lock here is gone, and
                   RefreshTx below rebuilds the rows either way. */
                m_handler.InsertDefaultTxEntryAfter(static_cast<size_t>(rows[0]) + 1);

                RefreshTx();
                m_grid->SelectRow(rows[0] + 1);
            });
        h_sizer->Add(m_Add);

        m_Copy = new wxButton(this, wxID_ANY, "Copy", wxDefaultPosition, wxDefaultSize);
        m_Copy->SetToolTip("Copy selected CAN frame(s) to the end of TX list");
        m_Copy->Bind(wxEVT_BUTTON, [this](wxCommandEvent& event)
            {
                wxGrid* m_grid = can_grid_tx->m_grid;

                wxArrayInt rows = m_grid->GetSelectedRows();
                if(rows.empty()) return;

                for(auto& i : rows)
                {
                    /* By id rather than through the raw grid-row pointer this
                       read - with no lock - while the worker stamps the same
                       entries. Ids can repeat in a loaded list; the first
                       match wins, as Move Up and Move Down already decided. */
                    const std::optional<uint32_t> parsed_id = FrameIdAt(can_grid_tx->m_grid, i);
                    if(parsed_id)
                        m_handler.DuplicateFirstTxEntry(*parsed_id);
                }
                RefreshTx();
                m_grid->SelectRow(m_grid->GetNumberRows() - 1);
            });
        h_sizer->Add(m_Copy);

        wxSize ret_size = m_Copy->GetSize();
        m_MoveUp = new wxButton(this, wxID_ANY, "Move Up", wxDefaultPosition, wxDefaultSize);
        //m_MoveUp = new wxBitmapButton(this, wxID_ANY, wxArtProvider::GetBitmap(wxART_GO_UP, wxART_OTHER, FromDIP(wxSize(75, 16))));
        m_MoveUp->SetToolTip("Move Up selected TX entry");
        m_MoveUp->Bind(wxEVT_BUTTON, [this](wxCommandEvent& event)
            {
                wxGrid* m_grid = can_grid_tx->m_grid;

                wxArrayInt rows = m_grid->GetSelectedRows();
                if(rows.empty()) return;

                int selection = m_grid->GetNumberRows() - 1;
                for(auto& i : rows)
                {
                    /* These three were the std::stoi call sites FrameIdAt was
                       written to replace. `entry` went with them: it indexed
                       grid_to_entry unchecked and no body ever read it. */
                    const std::optional<uint32_t> parsed_id = FrameIdAt(can_grid_tx->m_grid, i);
                    if(!parsed_id)
                        continue;
                    const uint32_t frame_id = *parsed_id;
                    if(i == 0)
                    {
                        m_handler.RotateTxFrontToBack();
                    }
                    else
                    {
                        /* FrameIdAt range-checks the row, so i - 1 at the top
                           of the grid stops here instead of reading a cell
                           that does not exist. */
                        const std::optional<uint32_t> parsed_above = FrameIdAt(can_grid_tx->m_grid, i - 1);
                        if(!parsed_above)
                            continue;
                        if(const auto new_index = m_handler.SwapTxEntriesById(frame_id, *parsed_above))
                            selection = static_cast<int>(*new_index);
                    }
                }

                RefreshTx();
                m_grid->SelectRow(selection);
            });
        h_sizer->Add(m_MoveUp);

        m_MoveDown = new wxButton(this, wxID_ANY, "Move Down", wxDefaultPosition, wxDefaultSize);
        m_MoveDown->SetToolTip("Move Down selected TX entry");
        m_MoveDown->Bind(wxEVT_BUTTON, [this](wxCommandEvent& event)
            {
                wxGrid* m_grid = can_grid_tx->m_grid;

                wxArrayInt rows = m_grid->GetSelectedRows();
                if(rows.empty()) return;

                int selection = 0;
                for(auto& i : rows)
                {
                    /* These three were the std::stoi call sites FrameIdAt was
                       written to replace. `entry` went with them: it indexed
                       grid_to_entry unchecked and no body ever read it. */
                    const std::optional<uint32_t> parsed_id = FrameIdAt(can_grid_tx->m_grid, i);
                    if(!parsed_id)
                        continue;
                    const uint32_t frame_id = *parsed_id;
                    if(i == m_grid->GetNumberRows() - 1)
                    {
                        m_handler.RotateTxBackToFront();
                    }
                    else
                    {
                        /* Same guard below the last row: i + 1 past the end
                           returns nothing rather than an empty cell's 0. */
                        const std::optional<uint32_t> parsed_below = FrameIdAt(can_grid_tx->m_grid, i + 1);
                        if(!parsed_below)
                            continue;
                        if(const auto new_index = m_handler.SwapTxEntriesById(frame_id, *parsed_below))
                            selection = static_cast<int>(*new_index);
                    }
                }

                RefreshTx();
                m_grid->SelectRow(selection);
            });
        h_sizer->Add(m_MoveDown);

        m_Delete = new wxButton(this, wxID_ANY, "Delete", wxDefaultPosition, wxDefaultSize);
        m_Delete->SetToolTip("Delete selected CAN frame(s) from TX list");
        m_Delete->Bind(wxEVT_BUTTON, [this](wxCommandEvent& event)
            {
                wxGrid* m_grid = can_grid_tx->m_grid;

                wxArrayInt rows = m_grid->GetSelectedRows();
                if(rows.empty()) return;

                for(auto& i : rows)
                {
                    /* These three were the std::stoi call sites FrameIdAt was
                       written to replace. `entry` went with them: it indexed
                       grid_to_entry unchecked and no body ever read it. */
                    const std::optional<uint32_t> parsed_id = FrameIdAt(can_grid_tx->m_grid, i);
                    if(!parsed_id)
                        continue;
                    m_handler.RemoveTxEntries(*parsed_id);
                }

                RefreshTx();
            });
        h_sizer->Add(m_Delete);

        RefreshGuiIconsBasedOnSettings();

        bSizer1->Add(h_sizer);
        bSizer1->AddSpacer(1);

        wxBoxSizer* h_sizer_2 = new wxBoxSizer(wxHORIZONTAL);

        m_SendDataFrame = new wxButton(this, wxID_ANY, "Send Data Frame", wxDefaultPosition, wxDefaultSize);
        m_SendDataFrame->SetToolTip("Send custom Data Frame without adding it to the list");
        m_SendDataFrame->Bind(wxEVT_BUTTON, [this](wxCommandEvent& event)
            {
                wxTextEntryDialog d(this, "Enter data to send\nExample: [FrameID] [Byte1] [Byte2] [ByteX] ...", "Send Data Frame");
                if(!m_LastDataInput.empty())
                    d.SetValue(m_LastDataInput);
                int ret = d.ShowModal();
                if(ret == wxID_OK)
                {
                    m_LastDataInput = d.GetValue().ToStdString();

                    uint32_t frame_id = 0;
                    char hex[MAX_ISOTP_FRAME_LEN] = {};
                    int ret = sscanf(m_LastDataInput.c_str(), "%x%*c%4095[^\n]", &frame_id, hex);
                    if(ret == 2)
                    {
                        const auto bytes = utils::ParseHexBytes(hex, MAX_ISOTP_FRAME_LEN);
                        if(!bytes)
                        {
                            LOG(LogLevel::Error, "Not sending Data Frame: '{}' is not valid hex", hex);
                            return;
                        }

                        m_handler.SendDataFrame(frame_id, std::span<const uint8_t>{ *bytes });
                        LOG(LogLevel::Notification, "Sending Data Frame, ID: {:X}, Len: {}", frame_id, bytes->size());
                    }
                    else
                    {
                        LOG(LogLevel::Notification, "Invalid data format for Data Frame");
                    }

                }
            });

        h_sizer_2->Add(m_SendDataFrame);
        //h_sizer_2->AddSpacer(100);

        m_SendIsoTp = new wxButton(this, wxID_ANY, "Send ISO-TP", wxDefaultPosition, wxDefaultSize);
        m_SendIsoTp->SetToolTip("Send ISO-TP data frame");
        m_SendIsoTp->Bind(wxEVT_BUTTON, [this](wxCommandEvent& event)
            {
                m_UdsRawDialog->ShowDialog();
            });
        h_sizer_2->Add(m_SendIsoTp);

        bSizer1->Add(h_sizer_2);

        wxBoxSizer* h_sizer_3 = new wxBoxSizer(wxHORIZONTAL);
        m_ClearRx = new wxButton(this, wxID_ANY, "Clear RX", wxDefaultPosition, wxDefaultSize);
        m_ClearRx->SetToolTip("Clear RX grid");
        m_ClearRx->Bind(wxEVT_BUTTON, [this](wxCommandEvent& event)
            {
                m_handler.ClearRxData();
                can_grid_rx->ClearGrid();
            });
        h_sizer_3->Add(m_ClearRx);
        bSizer1->AddSpacer(35);
        bSizer1->Add(h_sizer_3);
    }

    SetSizer(bSizer1);
    Show();
}

void CanSenderPanel::On10MsTimer()
{

    /* Snapshot the received frames under the lock, then draw.
       This used to iterate m_rxData directly - a map the CAN receive thread
       inserts into - and hand a live unique_ptr to the grid, while also
       recording raw CanRxData pointers that "Clear RX" invalidated. */
    std::vector<CanGridRx::RxRow> rows;
    m_handler.WithModel([&](CanEntryHandler::Model& model)
    {
        rows.reserve(model.rx_data.size());
        for(const auto& [frame_id, data] : model.rx_data)
        {
            if(!data)
                continue;

            CanGridRx::RxRow row;
            row.frame_id = frame_id;
            row.data = data->data;
            row.period = data->period;
            row.count = data->count;
            row.log_level = data->log_level;
            row.favourite_level = data->favourite_level;

            const auto comment_it = model.rx_comments.find(frame_id);
            if(comment_it != model.rx_comments.end())
                row.comment = comment_it->second;

            rows.push_back(std::move(row));
        }
    });

    for(const auto& row : rows)
    {
        if(!search_pattern_rx.empty() && !boost::icontains(row.comment, search_pattern_rx))
            continue;

        const auto existing = std::ranges::find_if(can_grid_rx->rx_grid_to_entry,
            [&row](const auto& entry) { return entry.second == row.frame_id; });

        if(existing != can_grid_rx->rx_grid_to_entry.end())
            can_grid_rx->UpdateRow(existing->first, row);
        else
            can_grid_rx->AddRow(row);
    }
}

void CanSenderPanel::RefreshSubpanels()
{
    RefreshTx();
    RefreshRx();
    RefreshGuiIconsBasedOnSettings();
}

void CanSenderPanel::RefreshTx()
{
    if(can_grid_tx->m_grid->GetNumberRows())
        can_grid_tx->m_grid->DeleteRows(0, can_grid_tx->m_grid->GetNumberRows());
    can_grid_tx->cnt = 0;
    can_grid_tx->grid_to_entry.clear();

    const uint8_t default_favourite_level = m_handler.GetFavouriteLevel();
    m_handler.WithModel([&](CanEntryHandler::Model& model)
    {
        for(auto& i : model.tx_entries)
        {
            if(!i || default_favourite_level > i->favourite_level)
                continue;
            if(!search_pattern_tx.empty() && !boost::icontains(i->comment, search_pattern_tx))
                continue;
            can_grid_tx->AddRow(i);
        }
    });
}

void CanSenderPanel::RefreshRx()
{

    /* Collect the comments first so the grid is filled with the lock released. */
    std::vector<std::pair<int, std::string>> comments;
    m_handler.WithModel([&](CanEntryHandler::Model& model)
    {
        for(int i = 0; i != can_grid_rx->m_grid->GetNumberRows(); i++)
        {
            const auto frame_id = FrameIdAt(can_grid_rx->m_grid, i);
            if(!frame_id)
                continue;
            const auto it = model.rx_comments.find(*frame_id);
            comments.emplace_back(i, it != model.rx_comments.end() ? it->second : std::string{});
        }
    });

    for(const auto& [row, comment] : comments)
        can_grid_rx->m_grid->SetCellValue(wxGridCellCoords(row, CanSenderGridCol::Sender_Comment), comment);
}

void CanSenderPanel::RefreshGuiIconsBasedOnSettings()
{
    m_SingleShot->Enable(m_Port.IsEnabled());
    m_SendAll->Enable(m_Port.IsEnabled());
    m_StopAll->Enable(m_Port.IsEnabled());
}

void CanSenderPanel::OnCellValueChanged(wxGridEvent& ev)
{
    int row = ev.GetRow(), col = ev.GetCol();
    if(ev.GetEventObject() == static_cast<wxObject*>(can_grid_rx->m_grid))
    {
        wxString new_value = can_grid_rx->m_grid->GetCellValue(row, col);
        switch(col)
        {
            case CanSenderGridCol::Sender_LogLevel:
            {
                const std::optional<uint32_t> parsed_id = FrameIdAt(can_grid_rx->m_grid, row);
                if(!parsed_id)
                    break;
                const uint32_t frame_id = *parsed_id;

                const wxString log_str = can_grid_rx->m_grid->GetCellValue(row, CanSenderGridCol::Sender_LogLevel);
                const std::optional<uint8_t> log_level = utils::TryParse<uint8_t>(log_str.ToStdString());

                /* m_rxData[frame_id] used to be indexed with operator[], which
                   inserts a null unique_ptr for a frame that was never received
                   and then dereferences it. */
                uint8_t current = 0;
                m_handler.WithModel([&](CanEntryHandler::Model& model)
                {
                    const auto it = model.rx_data.find(frame_id);
                    if(it == model.rx_data.end() || !it->second)
                        return;
                    if(log_level)
                    {
                        it->second->log_level = *log_level;
                        model.rx_log_levels[frame_id] = *log_level;
                    }
                    current = it->second->log_level;
                });

                if(!log_level)
                    can_grid_rx->m_grid->SetCellValue(wxGridCellCoords(row, CanSenderGridCol::Sender_LogLevel), wxString::Format("%d", current));
                break;
            }
            case CanSenderGridCol::Sender_FavouriteLevel:
            {
                const std::optional<uint32_t> parsed_id = FrameIdAt(can_grid_rx->m_grid, row);
                if(!parsed_id)
                    break;
                const uint32_t frame_id = *parsed_id;

                const wxString fav_str = can_grid_rx->m_grid->GetCellValue(row, CanSenderGridCol::Sender_FavouriteLevel);
                const std::optional<uint8_t> fav_level = utils::TryParse<uint8_t>(fav_str.ToStdString());

                uint8_t current = 0;
                m_handler.WithModel([&](CanEntryHandler::Model& model)
                {
                    const auto it = model.rx_data.find(frame_id);
                    if(it == model.rx_data.end() || !it->second)
                        return;
                    if(fav_level)
                        it->second->favourite_level = *fav_level;
                    current = it->second->favourite_level;
                });

                if(!fav_level)
                    can_grid_rx->m_grid->SetCellValue(wxGridCellCoords(row, CanSenderGridCol::Sender_FavouriteLevel), wxString::Format("%d", current));
                break;
            }
            case CanSenderGridCol::Sender_Comment:
            {
                const std::optional<uint32_t> parsed_id = FrameIdAt(can_grid_rx->m_grid, row);
                if(!parsed_id)
                    break;
                const uint32_t frame_id = *parsed_id;

                m_handler.WithModel([&](CanEntryHandler::Model& model)
                    { model.rx_comments[frame_id] = new_value.ToStdString(); });
                break;
            }
        }
    }
    else if(ev.GetEventObject() == static_cast<wxObject*>(can_grid_tx->m_grid))
    {
        wxString new_value = can_grid_tx->m_grid->GetCellValue(row, col);
        switch(col)
        {
            case CanSenderGridCol::Sender_Id:
            {
                const std::optional<uint32_t> parsed_id = FrameIdAt(can_grid_tx->m_grid, row);
                if(!parsed_id)
                    break;
                const uint32_t frame_id = *parsed_id;

                /* Check for a clash and renumber in one critical section; the
                   dialog is shown afterwards, with the lock released. The
                   entry itself is excluded from the check, so re-entering a
                   frame's existing ID is not reported as a duplicate. */
                bool duplicate = false;
                uint32_t previous_id = 0;
                m_handler.WithModel([&](CanEntryHandler::Model& model)
                {
                    CanTxEntry* target = can_grid_tx->grid_to_entry[row];
                    if(!target)
                        return;
                    previous_id = target->id;
                    duplicate = std::ranges::any_of(model.tx_entries, [&](const auto& i)
                        { return i && i.get() != target && i->id == frame_id; });
                    if(!duplicate)
                        target->id = frame_id;
                });

                if(duplicate)
                {
                    wxMessageDialog(this, "Given CAN Frame ID already added to the list!", "Error", wxOK).ShowModal();
                    can_grid_tx->m_grid->SetCellValue(wxGridCellCoords(row, CanSenderGridCol::Sender_Id),
                        wxString::Format("%X", previous_id));
                    return;
                }
                break;
            }
            case CanSenderGridCol::Sender_DataSize:
            {
                const std::optional<uint32_t> new_size = utils::TryParse<uint32_t>(new_value.ToStdString());
                if(!new_size || *new_size > 8)
                {
                    /* std::stoi threw here on anything non-numeric the user
                       typed. Non-numeric and too-large are different mistakes,
                       so they get different messages. */
                    wxMessageDialog(this,
                        new_size ? "Max payload size is 8!" : "Payload size must be a number between 0 and 8!",
                        "Error", wxOK).ShowModal();
                    can_grid_tx->m_grid->SetCellValue(wxGridCellCoords(row, CanSenderGridCol::Sender_DataSize), wxString::Format("%lld", can_grid_tx->grid_to_entry[row]->data.size()));
                    return;
                }

                can_grid_tx->grid_to_entry[row]->data.resize(*new_size);

                std::string hex;
                utils::ConvertHexBufferToString(can_grid_tx->grid_to_entry[row]->data, hex);
                can_grid_tx->m_grid->SetCellValue(wxGridCellCoords(row, CanSenderGridCol::Sender_Data), wxString(hex));
                break;
            }
            case CanSenderGridCol::Sender_Data:
            {

                const std::string typed = new_value.ToStdString();
                /* Eight bytes is the whole of a classic CAN frame. */
                const auto bytes = utils::ParseHexBytes(typed, 8);
                if(!bytes)
                {
                    LOG(LogLevel::Error, "Rejecting frame data '{}': not valid hex", typed);
                    break;
                }
                auto& frame_data = can_grid_tx->grid_to_entry[row]->data;
                frame_data.assign(bytes->begin(), bytes->end());

                std::string hex;
                utils::ConvertHexBufferToString(frame_data, hex);
                can_grid_tx->m_grid->SetCellValue(wxGridCellCoords(row, col), wxString(hex));
                can_grid_tx->m_grid->SetCellValue(wxGridCellCoords(row, CanSenderGridCol::Sender_DataSize),
                    wxString::Format("%lld", static_cast<long long>(frame_data.size())));
                break;
            }
            case CanSenderGridCol::Sender_Period:
            {
                if(new_value == "off")
                    new_value = "0";

                /* A negative period was rejected but a non-numeric one threw
                   std::invalid_argument out of this handler; both are the same
                   mistake to the user. */
                const std::optional<int> period = utils::TryParse<int>(new_value.ToStdString());
                if(!period || *period < 0)
                {
                    wxMessageDialog(this, "Period must be a number of milliseconds, or \"off\"!", "Error", wxOK).ShowModal();
                    can_grid_tx->m_grid->SetCellValue(wxGridCellCoords(row, CanSenderGridCol::Sender_Period), wxString::Format("%d", can_grid_tx->grid_to_entry[row]->period));
                    return;
                }
                can_grid_tx->grid_to_entry[row]->period = *period;
                break;
            }
            case CanSenderGridCol::Sender_LogLevel:
            {
                ApplyLevelCell(can_grid_tx->m_grid, row, CanSenderGridCol::Sender_LogLevel,
                    new_value, can_grid_tx->grid_to_entry[row]->log_level);
                break;
            }
            case CanSenderGridCol::Sender_FavouriteLevel:
            {
                ApplyLevelCell(can_grid_tx->m_grid, row, CanSenderGridCol::Sender_FavouriteLevel,
                    new_value, can_grid_tx->grid_to_entry[row]->favourite_level);
                break;
            }
            case CanSenderGridCol::Sender_Comment:
            {
                can_grid_tx->grid_to_entry[row]->comment = std::move(new_value.ToStdString());
                break;
            }
        }
    }
    ev.Skip();
}
/*
void CanSenderPanel::OnCellLeftClick(wxGridEvent& ev)
{
    DBG("left click");
    ev.Skip();
}

void CanSenderPanel::OnCellLeftDoubleClick(wxGridEvent& ev)
{
    DBG("left dclick");
    int row = ev.GetRow(), col = ev.GetCol();

    if(ev.GetEventObject() == static_cast<wxObject*>(can_grid_tx->m_grid))
        can_grid_tx->m_grid->SetReadOnly(row, col, can_grid_tx->m_grid->IsReadOnly(row, col));
    ev.Skip();
}
*/
namespace
{
// !\brief A CAN frame's bitfield mapping as the shared editor takes it.
//
// CanMap already inherits TextStyle, so the presentation half is passed
// straight through rather than copied field by field as the CAN-specific
// editor used to do.
std::vector<gui::BitFieldRow> ToEditorRows(const CanBitfieldInfo& info)
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

void ShowBitEditor(gui::BitFieldEditorDialog& editor, uint32_t frame_id, bool is_rx,
    const CanBitfieldInfo& info)
{
    const std::size_t requested = editor.ShowDialog(ToEditorRows(info),
        wxString::Format("Bit editor - %X (%s)", frame_id, is_rx ? "RX" : "TX"));

    if(requested > gui::kMaxBitFieldRows)
    {
        LOG(LogLevel::Warning,
            "Too much bitfields used for can frame mapping. FrameID: {:X}, Used: {}, Maximum supported: {}",
            frame_id, requested, gui::kMaxBitFieldRows);
    }
}
}

// !\brief Show the bit editor for a received frame.
void CanSenderPanel::ShowRxFrameBits(uint32_t frame_id)
{
    CanBitfieldInfo info = m_handler.GetMapForFrameId(frame_id, true);
    if(info.empty())
    {
        wxMessageDialog(this, "There are no mapping found for selected CAN Frame", "Error", wxOK).ShowModal();
        return;
    }
    ShowBitEditor(*m_BitfieldEditor, frame_id, true, info);
}

// !\brief Edit a transmitted frame's payload bit by bit, reapplying until the
// user stops pressing Apply.
void CanSenderPanel::EditTxFrameBits(uint32_t frame_id, int row)
{
    for(;;)
    {
        CanBitfieldInfo info = m_handler.GetMapForFrameId(frame_id, false);
        if(info.empty())
        {
            wxMessageDialog(this, "There are no mapping found for selected CAN Frame", "Error", wxOK).ShowModal();
            return;
        }

        ShowBitEditor(*m_BitfieldEditor, frame_id, false, info);
        if(m_BitfieldEditor->IsAccepted())
        {
            m_handler.ApplyEditingOnFrameId(frame_id, m_BitfieldEditor->GetOutput());

            const CanTxEntry* entry = can_grid_tx->grid_to_entry[row];
            if(entry)
            {
                std::string hex;
                utils::ConvertHexBufferToString(entry->data, hex);
                can_grid_tx->m_grid->SetCellValue(wxGridCellCoords(row, CanSenderGridCol::Sender_Data), wxString(hex));
                can_grid_tx->m_grid->SetCellValue(wxGridCellCoords(row, CanSenderGridCol::Sender_DataSize),
                    wxString::Format("%lld", entry->data.size()));
            }
        }

        if(m_BitfieldEditor->GetResult() != gui::BitFieldEditorResult::Apply)
            return;
    }
}

// !\brief Show the recorded traffic for one frame, or explain why there is none.
void CanSenderPanel::ShowLogForFrame(uint32_t frame_id, bool is_rx)
{
    std::vector<std::string> logs;
    m_handler.GenerateLogForFrame(frame_id, is_rx, logs);

    if(logs.empty())
    {
        wxMessageDialog(this, "In order to see the logs for frames, enable Recording in Log panel",
            "Error", wxOK).ShowModal();
        return;
    }
    m_LogForFrame->ShowDialog(logs);
}

// !\brief Restyle one transmitted frame's row.
void CanSenderPanel::EditTxFrameStyle(uint32_t frame_id)
{
    auto tx_entry_opt = m_handler.FindTxCanEntryByFrame(frame_id);
    if(!tx_entry_opt.has_value())
        return;

    CanTxEntry& tx_entry = tx_entry_opt->get();
    m_StyleEditDialog->ShowDialog({ tx_entry.m_color, tx_entry.m_bg_color, tx_entry.m_is_bold,
        tx_entry.m_font_face, tx_entry.m_scale });
    if(!m_StyleEditDialog->IsApplyClicked())
        return;

    const gui::TextStyleEdit style = m_StyleEditDialog->GetStyle();
    tx_entry.m_color = style.color;
    tx_entry.m_bg_color = style.background_color;
    tx_entry.m_is_bold = style.is_bold;
    tx_entry.m_scale = style.scale;
    tx_entry.m_font_face = style.font_face;
    RefreshTx();
}

// !\brief Stop showing a received frame.
void CanSenderPanel::RemoveRxFrame(uint32_t frame_id)
{
    m_handler.EraseRxData(frame_id);
    can_grid_rx->ClearGrid();
}

void CanSenderPanel::OnCellRightClick(wxGridEvent& ev)
{
    const int row = ev.GetRow();

    if(ev.GetEventObject() == static_cast<wxObject*>(can_grid_rx->m_grid))
    {
        const auto frame_id = FrameIdAt(can_grid_rx->m_grid, row);
        if(!frame_id)
            return;

        const gui::MenuEntry entries[]{
            gui::MenuCommand{ "&Show bits", [this, id = *frame_id] { ShowRxFrameBits(id); }, wxART_CDROM },
            gui::MenuCommand{ "&Log", [this, id = *frame_id] { ShowLogForFrame(id, true); }, wxART_FOLDER },
            gui::MenuCommand{ "&Remove", [this, id = *frame_id] { RemoveRxFrame(id); }, wxART_DELETE },
        };
        gui::RunContextMenu(this, entries);
        return;
    }

    if(ev.GetEventObject() == static_cast<wxObject*>(can_grid_tx->m_grid))
    {
        const auto frame_id = FrameIdAt(can_grid_tx->m_grid, row);
        if(!frame_id)
            return;

        const gui::MenuEntry entries[]{
            gui::MenuCommand{ "&Edit bits", [this, id = *frame_id, row] { EditTxFrameBits(id, row); }, wxART_CDROM },
            gui::MenuCommand{ "&Edit style", [this, id = *frame_id] { EditTxFrameStyle(id); }, wxART_EDIT },
            gui::MenuCommand{ "&Log", [this, id = *frame_id] { ShowLogForFrame(id, false); }, wxART_FOLDER },
        };
        gui::RunContextMenu(this, entries);
    }
}


void CanSenderPanel::OnGridLabelRightClick(wxGridEvent& ev)
{
    if(ev.GetEventObject() == static_cast<wxObject*>(can_grid_rx->m_grid) || ev.GetEventObject() == static_cast<wxObject*>(can_grid_tx->m_grid))
    {
        const gui::MenuEntry entries[]{
            gui::MenuCommand{ "&Edit log level", [this]
                {
                    if(const auto loglevel = gui::PromptForByte(this, "Enter default log level for TX & RX list",
                           "Default log level", m_handler.GetRecordingLogLevel(), "log level"))
                        m_handler.SetRecordingLogLevel(*loglevel);
                }, wxART_CDROM },
            gui::MenuCommand{ "&Edit favourites", [this]
                {
                    if(const auto favourite_level = gui::PromptForByte(this, "Enter default favourite level for TX & RX list",
                           "Default favourite level", m_handler.GetFavouriteLevel(), "favourite level"))
                    {
                        m_handler.SetFavouriteLevel(*favourite_level);
                        RefreshTx();
                    }
                }, wxART_FOLDER },
        };
        gui::RunContextMenu(this, entries);
    }
}

void CanSenderPanel::OnSize(wxSizeEvent& evt)
{
    evt.Skip(true);
}

namespace
{
/* One description of a file the CAN panel can load or save. The six handlers
   below were six copies of the same eleven lines, differing only in these
   fields - including the wildcard string, which was spelled out six times. */
struct CanFileAction
{
    const char* title;
    bool (CanEntryHandler::*action)(std::filesystem::path&);
    SimpleNotificationKind ok;
    SimpleNotificationKind failed;
};

constexpr const char* kXmlWildcard = "XML files (*.xml)|*.xml";

// !\brief Ask for a file, run one CAN list operation on it and report the result.
//
// `remembered_path` is the panel's memory of what was last chosen for that list
// and is only updated once the user has actually picked something.
void RunCanFileAction(wxWindow* parent, CanEntryHandler& handler, bool is_save,
    const CanFileAction& file_action, wxString& remembered_path,
    const std::function<void()>& on_success)
{
    const long style = is_save ? (wxFD_SAVE | wxFD_OVERWRITE_PROMPT)
                               : (wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    wxFileDialog dialog(parent, wxString::FromUTF8(file_action.title), "", "", kXmlWildcard, style);
    if(dialog.ShowModal() == wxID_CANCEL)
        return;

    remembered_path = dialog.GetPath();
    std::filesystem::path path = remembered_path.ToStdString();

    const bool ok = (handler.*(file_action.action))(path);
    if(ok && on_success)
        on_success();

    PostAppNotification(SimpleNotification{ok ? file_action.ok : file_action.failed});
}
}

void CanSenderPanel::LoadTxList()
{
    static constexpr CanFileAction kAction{"Open TX XML file", &CanEntryHandler::LoadTxList,
        SimpleNotificationKind::TxListLoaded, SimpleNotificationKind::TxListLoadError};
    RunCanFileAction(this, m_handler, false, kAction, file_path_tx, [this] { RefreshTx(); });
}

void CanSenderPanel::SaveTxList()
{
    static constexpr CanFileAction kAction{"Save TX XML file", &CanEntryHandler::SaveTxList,
        SimpleNotificationKind::TxListSaved, SimpleNotificationKind::TxListSaveError};
    RunCanFileAction(this, m_handler, true, kAction, file_path_tx, nullptr);
}

void CanSenderPanel::LoadRxList()
{
    static constexpr CanFileAction kAction{"Open RX XML file", &CanEntryHandler::LoadRxList,
        SimpleNotificationKind::RxListLoaded, SimpleNotificationKind::RxListLoadError};
    RunCanFileAction(this, m_handler, false, kAction, file_path_rx, [this] { RefreshRx(); });
}

void CanSenderPanel::SaveRxList()
{
    static constexpr CanFileAction kAction{"Save RX XML file", &CanEntryHandler::SaveRxList,
        SimpleNotificationKind::RxListSaved, SimpleNotificationKind::RxListSaveError};
    RunCanFileAction(this, m_handler, true, kAction, file_path_rx, nullptr);
}

void CanSenderPanel::LoadMapping()
{
    static constexpr CanFileAction kAction{"Open FrameMapping XML file", &CanEntryHandler::LoadMapping,
        SimpleNotificationKind::FrameMappingLoaded, SimpleNotificationKind::FrameMappingLoadError};
    RunCanFileAction(this, m_handler, false, kAction, file_path_mapping, nullptr);
}

void CanSenderPanel::SaveMapping()
{
    static constexpr CanFileAction kAction{"Save FrameMapping XML file", &CanEntryHandler::SaveMapping,
        SimpleNotificationKind::FrameMappingSaved, SimpleNotificationKind::FrameMappingSaveError};
    RunCanFileAction(this, m_handler, true, kAction, file_path_mapping, nullptr);
}

void CanSenderPanel::OnKeyDown(wxKeyEvent& evt)
{
    if(evt.GetKeyCode() == WXK_F2)
    {
        DBG("f2");

        wxArrayInt rows = can_grid_tx->m_grid->GetSelectedRows();
        wxArrayInt cols = can_grid_tx->m_grid->GetSelectedCols();
        if(rows.empty() || rows.size() > 1 || cols.empty() || cols.size() > 1) return;

        int row = rows[0];
        int col = cols[0];

        can_grid_tx->m_grid->SetReadOnly(row, col, true);
    }

    if(evt.ControlDown())
    {
        switch(evt.GetKeyCode())
        {
            case 'F':
            {
                wxWindow* focus = wxWindow::FindFocus();
                if(focus == can_grid_tx->m_grid)
                {
                    wxTextEntryDialog d(this, "Enter TX frame name for what you want to filter", "Search for frame");
                    d.SetValue(search_pattern_tx);
                    int ret = d.ShowModal();
                    if(ret == wxID_OK)
                    {
                        search_pattern_tx = d.GetValue().ToStdString();
                        if(search_pattern_tx.empty())
                            static_box_tx->GetStaticBox()->SetLabelText("Transmit");
                        else
                            static_box_tx->GetStaticBox()->SetLabelText(wxString::Format("Transmit - Search filter: %s", search_pattern_tx));

                        RefreshTx();
                    }
                    return;
                }
                else if(focus == can_grid_rx->m_grid)
                {
                    wxTextEntryDialog d(this, "Enter RX frame name for what you want to filter", "Search for frame");
                    d.SetValue(search_pattern_rx);
                    int ret = d.ShowModal();
                    if(ret == wxID_OK)
                    {
                        search_pattern_rx = d.GetValue().ToStdString();
                        if(search_pattern_rx.empty())
                            static_box_rx->GetStaticBox()->SetLabelText("Receive");
                        else
                            static_box_rx->GetStaticBox()->SetLabelText(wxString::Format("Receive - Search filter: %s", search_pattern_rx));

                        if(can_grid_rx->m_grid->GetNumberRows())
                            can_grid_rx->m_grid->DeleteRows(0, can_grid_rx->m_grid->GetNumberRows());
                        can_grid_rx->cnt = 0;
                        can_grid_rx->rx_grid_to_entry.clear();
                        RefreshRx();
                    }
                    return;
                }
                break;
            }
            case 'B':  /* Show bits */
            {
                /* This repeated ShowRxFrameBits and EditTxFrameBits inline, and
                   its TX half had been commented out - so Ctrl+B over the TX
                   grid found the mapping, then did nothing with it. The context
                   menu's two entries call the same methods. */
                wxWindow* focus = wxWindow::FindFocus();
                wxGrid* const grid = focus == can_grid_rx->m_grid ? can_grid_rx->m_grid : can_grid_tx->m_grid;

                wxArrayInt rows = grid->GetSelectedRows();
                if(rows.size() != 1)
                    return;

                const std::optional<uint32_t> frame_id = FrameIdAt(grid, rows[0]);
                if(!frame_id)
                    return;

                if(focus == can_grid_rx->m_grid)
                    ShowRxFrameBits(*frame_id);
                else
                    EditTxFrameBits(*frame_id, rows[0]);
                break;
            }
            case 'L':  /* Show log */
            {
                wxWindow* focus = wxWindow::FindFocus();
                if(focus == can_grid_rx->m_grid)
                {
                    wxArrayInt rows = can_grid_rx->m_grid->GetSelectedRows();
                    if(rows.empty() || rows.size() > 1) return;

                    const std::optional<uint32_t> parsed_id = FrameIdAt(can_grid_rx->m_grid, rows[0]);
                    if(!parsed_id)
                        return;
                    const uint32_t frame_id = *parsed_id;

                    std::vector<std::string> logs;
                    m_handler.GenerateLogForFrame(frame_id, true, logs);

                    if(logs.empty())
                    {
                        wxMessageDialog(this, "In order to see the logs for frames, enable Recording in Log panel", "Error", wxOK).ShowModal();
                    }
                    else
                        m_LogForFrame->ShowDialog(logs);
                }
                else
                {
                    wxArrayInt rows = can_grid_tx->m_grid->GetSelectedRows();
                    if(rows.empty() || rows.size() > 1) return;

                    const std::optional<uint32_t> parsed_id = FrameIdAt(can_grid_tx->m_grid, rows[0]);
                    if(!parsed_id)
                        return;
                    const uint32_t frame_id = *parsed_id;

                    std::vector<std::string> logs;
                    m_handler.GenerateLogForFrame(frame_id, false, logs);

                    if(logs.empty())
                    {
                        wxMessageDialog(this, "In order to see the logs for frames, enable Recording in Log panel", "Error", wxOK).ShowModal();
                        return;
                    }

                    m_LogForFrame->ShowDialog(logs);
                }
                break;
            }
        }
    }
    evt.Skip();
}

void CanSenderPanel::UpdateGridForTxFrame(uint32_t frame_id, std::span<const uint8_t> buffer)
{
    std::string hex_str;
    utils::ConvertHexBufferToString(reinterpret_cast<const char*>(buffer.data()), buffer.size(), hex_str);

    for(int i = 0; i != can_grid_tx->cnt; i++)
    {
        if(can_grid_tx->grid_to_entry[i]->id == frame_id)
        {
            can_grid_tx->m_grid->SetCellValue(wxGridCellCoords(i, Sender_Data), wxString(hex_str));
            can_grid_tx->m_grid->SetCellValue(wxGridCellCoords(i, CanSenderGridCol::Sender_DataSize), wxString::Format("%lld", hex_str.length() / 2));
            //can_grid_tx->grid_to_entry[row]->data.assign(bytes, bytes + (hex_str.length() / 2));
            break;
        }
    }
}

CanSenderEditDialog::CanSenderEditDialog(wxWindow* parent)
    : wxDialog(parent, wxID_ANY, "CAN Style editor", wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
    wxSizer* const sizerTop = new wxBoxSizer(wxVERTICAL);
    wxSizer* const sizerMsgs = new wxStaticBoxSizer(wxVERTICAL, this, "&CAN style properties");

    m_style = new gui::TextStylePanel(this);
    sizerMsgs->Add(m_style, wxSizerFlags(1).Expand());

    sizerTop->Add(sizerMsgs, wxSizerFlags(1).Expand().Border());
    sizerTop->Add(CreateStdDialogButtonSizer(wxAPPLY | wxCLOSE), wxSizerFlags().Right().Border());

    SetSizerAndFit(sizerTop);
    CentreOnScreen();
}

void CanSenderEditDialog::ShowDialog(const gui::TextStyleEdit& style)
{
    m_style->SetValue(style);
    m_IsApplyClicked = false;
    ShowModal();
}

void CanSenderEditDialog::OnApply(wxCommandEvent& WXUNUSED(event))
{
    Close();
    m_IsApplyClicked = true;
}


