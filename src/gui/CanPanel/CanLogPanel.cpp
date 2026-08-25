#include "pch.hpp"
#include "../WxClipboard.hpp"
#include "../GridBuilder.hpp"
#include "../MainFrameAccess.hpp"

wxBEGIN_EVENT_TABLE(CanLogPanel, wxPanel)
EVT_SIZE(CanLogPanel::OnSize)
EVT_CHAR_HOOK(CanLogPanel::OnKeyDown)
EVT_SPINCTRL(ID_CanLogLevelSpinCtrl, CanLogPanel::OnLogLevelChange)
wxEND_EVENT_TABLE()

CanLogPanel::CanLogPanel(wxWindow* parent, CanEntryHandler& handler)
    : wxPanel(parent, wxID_ANY), m_handler(handler)
{

    wxBoxSizer* v_sizer = new wxBoxSizer(wxVERTICAL);
    wxBoxSizer* h_sizer = new wxBoxSizer(wxHORIZONTAL);

    static_box = new wxStaticBoxSizer(wxHORIZONTAL, this, "&Log :: TX: 0, RX: 0, Total: 0");
    static_box->GetStaticBox()->SetFont(static_box->GetStaticBox()->GetFont().Bold());
    static_box->GetStaticBox()->SetForegroundColour(*wxBLUE);

    /* In CanLogGridCol order. */
    static constexpr gui::GridColumn kColumns[]{
        { "Time" },          // Log_Time
        { "Direction" },     // Log_Direction
        { "Id" },            // Log_Id
        { "Size" },          // Log_DataSize
        { "Data", 200 },     // Log_Data
        { "Comment", 200 },  // Log_Comment
    };
    static_assert(std::size(kColumns) == CanLogGridCol::Log_Max);

    m_grid = gui::BuildGrid(this, gui::GridSpec{
        .size = wxSize(800, 600),
        .initial_rows = 1,
        .columns = kColumns,
        .selection_mode = wxGrid::wxGridSelectRows,
        .hide_row_labels = true,
    });
    static_box->Add(m_grid);

    m_RecordingBar = std::make_unique<gui::LogRecordingBar>(this, h_sizer,
        gui::LogRecordingBar::Spec{
            .subject = "CAN frames",
            .start = [this] { m_handler.StartRecording(); },
            .pause = [this] { m_handler.PauseRecording(); },
            .stop = [this] { m_handler.StopRecording(); inserted_until = 0; },
            .clear = [this] { ClearRecordingsFromGrid(); m_handler.ClearRecording(); },
            .save_directory = "Can",
            .save_prefix = "CanLog",
            .save = [this](std::filesystem::path p) { m_handler.SaveRecordingToFile(p); },
            .auto_scroll = true,
        });

    h_sizer->AddSpacer(10);
    h_sizer->Add(new wxStaticText(this, wxID_ANY, "LogLevel:"));
    m_LogLevelCtrl = new wxSpinCtrl(this, ID_CanLogLevelSpinCtrl, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 0, 10, 1);
    h_sizer->Add(m_LogLevelCtrl);

    v_sizer->Add(h_sizer);
    v_sizer->Add(m_RecordingBar->SaveButton());
    v_sizer->Add(static_box);

    SetSizerAndFit(v_sizer);
    Show();
}

void CanLogPanel::On10MsTimer()
{

    if(search_pattern.empty())
    {
        if(m_LastShownTxCount != m_handler.GetTxFrameCount() || m_LastShownRxCount != m_handler.GetRxFrameCount())
        {
            static_box->GetStaticBox()->SetLabelText(wxString::Format("Log :: TX: %lld, RX: %lld, Total: %lld", m_handler.GetTxFrameCount(), m_handler.GetRxFrameCount(),
                m_handler.GetTxFrameCount() + m_handler.GetRxFrameCount()));
        }
    }
    else
    {
        if(m_LastShownTxCount != m_handler.GetTxFrameCount() || m_LastShownRxCount != m_handler.GetRxFrameCount() || m_LastShownSearchPattern != search_pattern)
        {
            static_box->GetStaticBox()->SetLabelText(wxString::Format("Log :: Filter: %s, TX: %lld, RX: %lld, Total: %lld", search_pattern,
                m_handler.GetTxFrameCount(), m_handler.GetRxFrameCount(), m_handler.GetTxFrameCount() + m_handler.GetRxFrameCount()));
        }
    }

    m_LastShownTxCount = m_handler.GetTxFrameCount();
    m_LastShownRxCount = m_handler.GetRxFrameCount();
    m_LastShownSearchPattern = search_pattern;

    if(!is_something_inserted)
        inserted_until = 0;

    /* Copy the new entries out under the lock, then draw them.
       This used to walk m_LogEntries directly and call InsertRow - a run of
       wxGrid calls - from inside the loop, while the CAN receive thread was
       appending to the very same vector. A push_back that reallocated left this
       iterating freed memory. */
    struct PendingLogRow
    {
        std::chrono::steady_clock::time_point timestamp;
        uint8_t direction = 0;
        uint32_t frame_id = 0;
        std::vector<uint8_t> data;
        std::string comment;
    };

    std::vector<PendingLogRow> rows;
    m_handler.WithModel([&](CanEntryHandler::Model& model)
    {
        if(inserted_until >= model.log.Entries().size())
            return;

        rows.reserve(model.log.Entries().size() - inserted_until);
        for(size_t i = inserted_until; i < model.log.Entries().size(); ++i)
        {
            const auto& entry = model.log.Entries()[i];
            if(!entry)
                continue;

            PendingLogRow row;
            row.timestamp = entry->last_execution;
            row.direction = entry->direction;
            row.frame_id = entry->frame_id;
            row.data = entry->data;

            if(entry->direction == CAN_LOG_DIR_RX)
            {
                const auto comment_it = model.rx_comments.find(entry->frame_id);
                if(comment_it != model.rx_comments.end())
                    row.comment = comment_it->second;
            }
            else
            {
                for(const auto& tx : model.tx_entries)
                {
                    if(tx && tx->id == entry->frame_id)
                    {
                        row.comment = tx->comment;
                        break;
                    }
                }
            }
            rows.push_back(std::move(row));
        }
        inserted_until = model.log.Entries().size();
    });

    if(rows.empty())
        return;

    for(auto& row : rows)
    {
        if(search_pattern.empty() || boost::icontains(row.comment, search_pattern))
            InsertRow(row.timestamp, row.direction, row.frame_id, row.data, row.comment);
    }
    is_something_inserted = true;
}

void CanLogPanel::InsertRow(std::chrono::steady_clock::time_point& t1, uint8_t direction, uint32_t id, std::vector<uint8_t>& data, std::string& comment)
{
    /* The row count before the append: the auto-scroll below moves by it,
       which reaches the bottom of a grid that is still growing. */
    const int num_rows = m_grid->GetNumberRows();
    gui::EnsureRow(*m_grid, cnt);

    uint64_t elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - m_handler.GetStartTime()).count();
    m_grid->SetCellValue(wxGridCellCoords(cnt, CanLogGridCol::Log_Time), wxString::Format("%.3lf", static_cast<double>(elapsed) / 1000.0));

    std::string hex;
    utils::ConvertHexBufferToString(data, hex);
    m_grid->SetCellValue(wxGridCellCoords(cnt, CanLogGridCol::Log_Data), hex);
    m_grid->SetCellValue(wxGridCellCoords(cnt, CanLogGridCol::Log_Direction), direction == CAN_LOG_DIR_TX ? "TX" : "RX");
    m_grid->SetCellValue(wxGridCellCoords(cnt, CanLogGridCol::Log_Id), wxString::Format("%X", id));
    m_grid->SetCellValue(wxGridCellCoords(cnt, CanLogGridCol::Log_DataSize), wxString::Format("%lld", data.size()));
    m_grid->SetCellValue(wxGridCellCoords(cnt, CanLogGridCol::Log_Comment), comment);

    if(m_RecordingBar->IsAutoScroll())
        m_grid->ScrollLines(num_rows);

    for(uint8_t i = 0; i != CanLogGridCol::Log_Max; i++)
    {
        m_grid->SetReadOnly(cnt, i, true);
        m_grid->SetCellBackgroundColour(cnt, i, (direction == CAN_LOG_DIR_RX) ? 0xE6E6E6 : 0xFFFFFF);
    }

    cnt++;
}

void CanLogPanel::ClearRecordingsFromGrid()
{
    int num_rows = m_grid->GetNumberRows();
    if(num_rows)
        m_grid->DeleteRows(0, num_rows);
    cnt = 0;

    is_something_inserted = false;
    inserted_until = 0;
}

void CanLogPanel::OnKeyDown(wxKeyEvent& evt)
{
    if(evt.ControlDown())
    {
        switch(evt.GetKeyCode())
        {
            case 'F':
            {
                wxWindow* focus = wxWindow::FindFocus();
                if(focus == m_grid)
                {
                    wxTextEntryDialog d(this, "Enter frame name for what you want to filter", "Frame filter");
                    d.SetValue(search_pattern);
                    int ret = d.ShowModal();
                    if(ret == wxID_OK)
                    {
                        std::string new_search_pattern = d.GetValue().ToStdString();
                        if(new_search_pattern != search_pattern)
                        {
                            search_pattern = new_search_pattern;
                            ClearRecordingsFromGrid();
                        }
                    }
                }
                break;
            }
            case 'C':
            {
                /* The last column is deliberately excluded here; the Modbus and
                   time tracker grids copy all of theirs. */
                if(wxWindow::FindFocus() == m_grid &&
                    gui::CopySelectedRowsToClipboard(*m_grid, CanLogGridCol::Log_Max - 1))
                {
                    PostAppNotification(SimpleNotification{SimpleNotificationKind::SelectedLogsCopied});
                }
                break;
            }
        }
    }
}

void CanLogPanel::OnLogLevelChange(wxSpinEvent& evt)
{
    uint8_t new_log_level = static_cast<uint8_t>(evt.GetValue());
    m_handler.SetRecordingLogLevel(new_log_level);
}

void CanLogPanel::OnSize(wxSizeEvent& evt)
{
    evt.Skip(true);
}

wxBEGIN_EVENT_TABLE(CanLogForFrameDialog, wxDialog)
EVT_BUTTON(wxID_APPLY, CanLogForFrameDialog::OnApply)
wxEND_EVENT_TABLE()

CanLogForFrameDialog::CanLogForFrameDialog(wxWindow* parent)
    : wxDialog(parent, wxID_ANY, "CAN log for frame", wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
    sizerTop = new wxBoxSizer(wxVERTICAL);

    m_Log = new wxListBox(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, 0, 0, wxLB_SINGLE | wxLB_HSCROLL | wxLB_NEEDED_SB);
    m_Log->Bind(wxEVT_LEFT_DCLICK, [this](wxMouseEvent& event)
        {
            const int selection = m_Log->GetSelection();
            if(selection == wxNOT_FOUND)
                return;

            gui::CopyTextToClipboard(m_Log->GetString(selection));
        });
    sizerTop->Add(m_Log, wxSizerFlags(1).Left().Expand());

    //sizerTop->Add(sizerMsgs, wxSizerFlags(1).Expand().Border());

    // finally buttons to show the resulting message box and close this dialog
    sizerTop->Add(CreateStdDialogButtonSizer(wxAPPLY | wxCLOSE), wxSizerFlags().Right().Border()); /* wxOK */

    sizerTop->SetMinSize(wxSize(640, 480));
    SetAutoLayout(true);
    SetSizer(sizerTop);
    sizerTop->Fit(this);
    sizerTop->SetSizeHints(this);
    CentreOnScreen();
}

void CanLogForFrameDialog::ShowDialog(std::vector<std::string>& values)
{
    m_Log->Clear();
    for(const auto i : values)
    {
        m_Log->Append(i);
    }
    ShowModal();
}

void CanLogForFrameDialog::OnApply(wxCommandEvent& WXUNUSED(event))
{
    Close();
    m_Log->Clear();
}
