#include "pch.hpp"
#include "MenuCommand.hpp"
#include "GridBuilder.hpp"
#include "MainFrameAccess.hpp"
#include "TimeTrackerLogic.hpp"

wxBEGIN_EVENT_TABLE(TimeTrackerPanel, wxPanel)
EVT_GRID_CELL_CHANGED(TimeTrackerPanel::OnCellValueChanged)
EVT_GRID_CELL_RIGHT_CLICK(TimeTrackerPanel::OnCellRightClick)
EVT_CHAR_HOOK(TimeTrackerPanel::OnKeyDown)
EVT_SIZE(TimeTrackerPanel::OnSize)
wxEND_EVENT_TABLE()

std::string SecondsToDecimalHours(long total_seconds) 
{
    double decimal_hours = static_cast<double>(total_seconds) / 3600.0;

    std::stringstream ss;
    ss << std::fixed << std::setprecision(2) << decimal_hours;

    // Replace decimal point with comma if needed (European format)
    std::string result = ss.str();
    size_t dot_pos = result.find('.');
    if (dot_pos != std::string::npos) 
    {
        result[dot_pos] = ',';
    }

    return result;
}

// !\brief A duration as hh:mm, the form the worktime grid shows.
//
// Six cells in this file spelled out the same
// wxString::Format("%02lld:%02lld", d.hours(), d.minutes()). utils::SecondsToHms
// is the hh:mm:ss form and takes a count of seconds, so it does not fit here.
static wxString FormatHoursMinutes(const boost::posix_time::time_duration& duration)
{
    return wxString::Format("%02lld:%02lld", duration.hours(), duration.minutes());
}

TimeTrackerGrid::TimeTrackerGrid(wxWindow* parent)
{
    /* In TimeTrackerCol order. */
    static constexpr gui::GridColumn kColumns[]{
        { "Time", 70 },        // TimeTracker_Date
        { "Start", 50 },       // TimeTracker_Start
        { "End", 50 },         // TimeTracker_End
        { "Comment", 400 },    // TimeTracker_Comment
        { "Hours", 50 },       // TimeTracker_Hours
        { "Total Work", 135 }, // TimeTracker_TotalWork
    };
    static_assert(std::size(kColumns) == TimeTrackerCol::TimeTracker_Max);

    m_grid = gui::BuildGrid(parent, gui::GridSpec{
        .size = wxDefaultSize,
        .initial_rows = 1,
        .columns = kColumns,
        .selection_mode = wxGrid::wxGridSelectRows,
        .hide_row_labels = true,
    });
    m_grid->SetMinSize(wxSize(1024, 768));
    m_grid->SetMaxSize(wxSize(2048, 2048));

    m_grid->AutoSizeRows();
    m_grid->AutoSizeColumns();
}

void TimeTrackerGrid::AddRow(TimeEntry* entry, int hourly_rate)
{
    if(!entry)
        return;


    gui::EnsureRow(*m_grid, cnt);

    // Extract the date from the entry's start time
    boost::gregorian::date currentDate = entry->start.date();

    // If the date changes, set the total hours for the previous day
    if (currentDate != m_LastSetDate)
    {
        wxString totalHoursStr = wxString::Format("%02lld:%02lld:%02lld",
            m_TotalDurationForDay.hours(),
            m_TotalDurationForDay.minutes(),
            m_TotalDurationForDay.seconds());
        m_grid->SetCellValue(wxGridCellCoords(cnt, TimeTrackerCol::TimeTracker_TotalWork), totalHoursStr);

        // Reset the total duration for the new day
        m_TotalDurationForDay = boost::posix_time::time_duration(0, 0, 0);

        // Alternate the row color
        m_RowIsGray = !m_RowIsGray;

        // Set the date for the new day
        wxString dateStr = wxString::Format("%04d-%02d-%02d",
            static_cast<int>(currentDate.year()),
            static_cast<int>(currentDate.month().as_number()),
            static_cast<int>(currentDate.day()));
        m_grid->SetCellValue(wxGridCellCoords(cnt, TimeTrackerCol::TimeTracker_Date), dateStr);
        m_LastSetDate = currentDate; // Update the last set date
    }

    // Calculate the duration for the current entry and add it to the total duration for the day
    boost::posix_time::time_duration entryDuration = entry->end - entry->start;
    m_TotalDurationForDay += entryDuration;
    m_TotalDuration += entryDuration;

    // Set the start time
    boost::posix_time::time_duration startDuration = entry->start.time_of_day();
    wxString timeStartStr = FormatHoursMinutes(startDuration);
    m_grid->SetCellValue(wxGridCellCoords(cnt, TimeTrackerCol::TimeTracker_Start), timeStartStr);

    // Set the end time
    boost::posix_time::time_duration endDuration = entry->end.time_of_day();
    wxString timeEndStr = FormatHoursMinutes(endDuration);
    m_grid->SetCellValue(wxGridCellCoords(cnt, TimeTrackerCol::TimeTracker_End), timeEndStr);

    // Set the time difference (duration) in the TimeTracker_Hours column
	if (entryDuration.hours() < 0)
		entryDuration = boost::posix_time::time_duration(0, 0, 0);
    wxString timeDiffStr = FormatHoursMinutes(entryDuration);
    m_grid->SetCellValue(wxGridCellCoords(cnt, TimeTrackerCol::TimeTracker_Hours), timeDiffStr);

    // Set the comment
    m_grid->SetCellValue(wxGridCellCoords(cnt, TimeTrackerCol::TimeTracker_Comment), entry->desc);

    // Apply the row color based on the current stripe phase
    wxColor rowColor = m_RowIsGray ? wxColor(230, 230, 230) : wxColor(255, 255, 255);
    for (int col = 0; col < TimeTrackerCol::TimeTracker_Max; ++col)
    {
        m_grid->SetCellBackgroundColour(cnt, col, rowColor);
    }

    m_grid->SetColLabelValue(TimeTrackerCol::TimeTracker_TotalWork,
        time_tracker_logic::FormatTotalWorkLabel(m_TotalDuration.total_seconds(), hourly_rate));

    // Map the entry to the grid row
    grid_to_entry_id[static_cast<int>(cnt)] = entry->sql_id;
    cnt++;
}

void TimeTrackerGrid::AddRowSerialized(TimeEntrySerialized* entry)
{
    if(!entry || !entry->entry)
        return;

    gui::EnsureRow(*m_grid, cnt);

    if (!entry->date.is_not_a_date())
    {
		wxString dateStr = wxString::Format("%04d-%02d-%02d",
			static_cast<int>(entry->date.year()),
			static_cast<int>(entry->date.month().as_number()),
			static_cast<int>(entry->date.day()));
		m_grid->SetCellValue(wxGridCellCoords(cnt, TimeTrackerCol::TimeTracker_Date), dateStr);
    }

    wxString timeStartStr = FormatHoursMinutes(entry->start);
    m_grid->SetCellValue(wxGridCellCoords(cnt, TimeTrackerCol::TimeTracker_Start), timeStartStr);

    // Set the end time
    wxString timeEndStr = FormatHoursMinutes(entry->end);
    m_grid->SetCellValue(wxGridCellCoords(cnt, TimeTrackerCol::TimeTracker_End), timeEndStr);

	auto entryDuration = entry->end - entry->start;
    if (entryDuration.hours() < 0)
        entryDuration = boost::posix_time::time_duration(0, 0, 0);
    wxString timeDiffStr = FormatHoursMinutes(entryDuration);
    m_grid->SetCellValue(wxGridCellCoords(cnt, TimeTrackerCol::TimeTracker_Hours), timeDiffStr);

	if (entry->total_duration_per_day != boost::posix_time::time_duration(0, 0, 0))
	{
		wxString totalHoursStr = wxString::Format("%02lld:%02lld:%02lld",
			entry->total_duration_per_day.hours(),
			entry->total_duration_per_day.minutes(),
			entry->total_duration_per_day.seconds());
		m_grid->SetCellValue(wxGridCellCoords(cnt, TimeTrackerCol::TimeTracker_TotalWork), totalHoursStr);
	}
	else
	{
		m_grid->SetCellValue(wxGridCellCoords(cnt, TimeTrackerCol::TimeTracker_TotalWork), "");
	}
	m_grid->SetCellValue(wxGridCellCoords(cnt, TimeTrackerCol::TimeTracker_Comment), entry->entry->desc);
	
    if (!entry->entry->is_overlap)
    {
        wxColor rowColor = entry->is_white ? wxColor(255, 255, 255) : wxColor(230, 230, 230);  /* WHITE -> GRAY */
        if (entry->is_time_bad)
        {
            rowColor = wxColor(242, 121, 7);  /* ORANGE */
        }

        for (int col = 0; col < TimeTrackerCol::TimeTracker_Max; ++col)
        {
            m_grid->SetCellBackgroundColour(cnt, col, rowColor);
        }
    }
    else
    {
		wxColor rowColor = wxColor(255, 0, 0);  /* RED */
		for (int col = 0; col < TimeTrackerCol::TimeTracker_Max; ++col)
		{
			m_grid->SetCellBackgroundColour(cnt, col, rowColor);
		}
    }

	grid_to_entry_id[static_cast<int>(cnt)] = entry->entry->sql_id;
	cnt++;
}

void TimeTrackerGrid::ClearRows()
{
	const int row_count = m_grid->GetNumberRows();
    if(row_count > 0)
    {
	    m_grid->ClearGrid();
	    m_grid->DeleteRows(0, row_count);
    }
	grid_to_entry_id.clear();
	cnt = 0;
	m_LastSetDate = {};
	m_TotalDurationForDay = boost::posix_time::time_duration(0, 0, 0);
	m_TotalDuration = boost::posix_time::time_duration(0, 0, 0);
	m_RowIsGray = true;
}

void TimeTrackerPanel::ToggleWorktime()
{
    FinishActiveEdit();
	boost::posix_time::time_duration duration(0, 0, 0);
    if (!is_working)
    {
        auto time = boost::posix_time::second_clock::local_time();
        TimeEntry* time_entry = m_tracker.AddEntry(time, time, "");
        if(!time_entry)
        {
            wxMessageBox(m_tracker.LastError(), "Unable to start time tracking", wxOK | wxICON_ERROR, this);
            return;
        }

        lastTimeEntryId = time_entry->sql_id;
		lastTimeEntryStart = time_entry->start;
        lastPersistedMinute = 0;

        tracker_grid->AddRow(time_entry, m_tracker.GetHourlyRate());
        m_tracker.UpdateEntries();
        RefreshPanel();

        is_working = true;
        SetWorktimeUi(true);
    }
    else
    {
        if(!lastTimeEntryId)
        {
            is_working = false;
            SetWorktimeUi(false);
            return;
        }

        const TimeEntry* entry = m_tracker.FindEntry(*lastTimeEntryId);
        if(!entry)
        {
            is_working = false;
            lastTimeEntryId.reset();
            SetWorktimeUi(false);
            wxMessageBox("The running entry is no longer available. The timer was stopped safely.",
                "Time tracker state changed", wxOK | wxICON_WARNING, this);
            return;
        }

        const auto start = entry->start;
        const auto end = boost::posix_time::second_clock::local_time();
        const auto comment = entry->desc;
        if(!m_tracker.EditEntry(*lastTimeEntryId, start, end, comment))
        {
            wxMessageBox(m_tracker.LastError(), "Unable to stop time tracking", wxOK | wxICON_ERROR, this);
            return;
        }
        m_tracker.UpdateEntries();
        RefreshPanel();

        duration = end - start;
        lastTimeEntryId.reset();
        is_working = false;
        SetWorktimeUi(false);
    }

    PostAppNotification(WorktimeToggledNotification{is_working,
        std::chrono::seconds{duration.total_seconds()}});
}

TimeTrackerPanel::TimeTrackerPanel(wxFrame* parent, TimeTracker& tracker, WorkingDays& working_days) :
    wxPanel(parent, wxID_ANY), m_tracker(tracker), m_WorkingDays(working_days)
{
    wxBoxSizer* bSizer1 = new wxBoxSizer(wxVERTICAL);

    static_box_grid = new wxStaticBoxSizer(wxHORIZONTAL, this, "&Time Tracker");
    static_box_grid->GetStaticBox()->SetFont(static_box_grid->GetStaticBox()->GetFont().Bold());
    static_box_grid->GetStaticBox()->SetForegroundColour(wxColor(4, 120, 35));

    tracker_grid = new TimeTrackerGrid(this);

    // Month choice
    wxBoxSizer* v_sizer_0 = new wxBoxSizer(wxHORIZONTAL);
    wxArrayString months;
    months.Add("January");
    months.Add("February");
    months.Add("March");
    months.Add("April");
    months.Add("May");
    months.Add("June");
    months.Add("July");
    months.Add("August");
    months.Add("September");
    months.Add("October");
    months.Add("November");
    months.Add("December");

    m_WorktimeMonth = new wxChoice(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, months);
    m_WorktimeMonth->SetSelection(0); // Default to January
    v_sizer_0->Add(m_WorktimeMonth);

    wxArrayString years;
    {
        int currentYear = boost::gregorian::day_clock::local_day().year();
        for (int y = 2023; y <= currentYear; ++y)
        {
            years.Add(wxString::Format("%d", y));
        }
    }
    m_WorktimeYear = new wxChoice(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, years);
    m_WorktimeYear->SetStringSelection("2024");
    v_sizer_0->Add(m_WorktimeYear);

    m_RefreshButton = new wxButton(this, wxID_ANY, "Refresh");
    v_sizer_0->Add(m_RefreshButton);
    m_RefreshButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent&)
        {
            FinishActiveEdit();
            if(is_working)
            {
                wxMessageBox("Stop the worktime counter before loading another month.",
                    "Time tracking is active", wxOK | wxICON_INFORMATION, this);
                return;
            }

            wxString year_str = m_WorktimeYear->GetStringSelection();

            const int month_selection = m_WorktimeMonth->GetSelection();
            long month_int = month_selection + 1;
            long year_int = -1;

            bool validMonth = month_selection != wxNOT_FOUND;
            bool validYear = year_str.ToLong(&year_int);

            // Additional optional range checks
            if (!validMonth || month_int <= 0 || month_int > 12) {
                wxMessageBox("Invalid month selected. Please choose a valid numeric month (1-12).", "Error", wxICON_ERROR);
                return;
            }

            if (!validYear || year_int < 1900 || year_int > 2100) {
                wxMessageBox("Invalid year selected. Please choose a valid year (e.g. 2023).", "Error", wxICON_ERROR);
                return;
            }

            m_tracker.SetActualYearMonth(year_int, month_int);
            m_tracker.LoadEntries(year_int, month_int);
            RefreshPanel();
        });

    m_WorktimeMonth->SetSelection(m_tracker.GetMonth() - 1);
    m_WorktimeYear->SetSelection(m_tracker.GetYear() - 2023);

    bSizer1->Add(v_sizer_0);

    wxBoxSizer* v_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_StartButton = new wxButton(this, wxID_ANY, "Start", wxDefaultPosition, wxDefaultSize);
    m_StartButton->SetToolTip("Start working hours calculation)");
    m_StartButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent& event)
        {
            ToggleWorktime();
        });
    v_sizer->Add(m_StartButton);
    v_sizer->AddSpacer(10);

	m_TimeCounter = new wxStaticText(this, wxID_ANY, "00:00:00", wxDefaultPosition, wxSize(100, -1), 0);
	m_TimeCounter->SetFont(wxFont(18, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, wxEmptyString));
	m_TimeCounter->SetForegroundColour(*wxBLACK);
	m_TimeCounter->SetToolTip("Elapsed time since the last entry");
    v_sizer->Add(m_TimeCounter);

    wxBoxSizer* v_sizer_2 = new wxBoxSizer(wxHORIZONTAL);
    m_WeekNumber = new wxStaticText(this, wxID_ANY, FormatCurrentWeekNumber(), wxDefaultPosition, wxSize(-1, -1), 0);
    m_WeekNumber->SetToolTip("Week number in the year");
    m_WeekNumber->SetForegroundColour(*wxBLUE);
    m_WeekNumber->SetFont(wxFont(18, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, wxEmptyString));
    v_sizer_2->Add(m_WeekNumber);
    v_sizer_2->AddSpacer(14);

    m_WorkingDaysSk = new wxStaticText(this, wxID_ANY, "SK", wxDefaultPosition, wxSize(-1, -1), 0);
    m_WorkingDaysSk->SetToolTip("Working days - Total days [hours] (holidays)");
    m_WorkingDaysSk->Wrap(-1);
    m_WorkingDaysSk->SetFont(wxFont(18, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, wxEmptyString));
    m_WorkingDaysSk->SetForegroundColour(wxColor(232, 9, 210));
    v_sizer_2->Add(m_WorkingDaysSk);
    v_sizer_2->AddSpacer(5);

    m_WorkingDaysHu = new wxStaticText(this, wxID_ANY, "HU", wxDefaultPosition, wxSize(-1, -1), 0);
    m_WorkingDaysHu->SetToolTip("Working days - Total days [hours] (holidays)");
    m_WorkingDaysHu->Wrap(-1);
    m_WorkingDaysHu->SetFont(wxFont(18, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, wxEmptyString));
    m_WorkingDaysHu->SetForegroundColour(wxColor(232, 9, 210));
    v_sizer_2->Add(m_WorkingDaysHu);
    v_sizer_2->AddSpacer(5);

    m_WorkingDaysAt = new wxStaticText(this, wxID_ANY, "AT", wxDefaultPosition, wxSize(-1, -1), 0);
    m_WorkingDaysAt->SetToolTip("Working days - Total days [hours] (holidays)");
    m_WorkingDaysAt->Wrap(-1);
    m_WorkingDaysAt->SetFont(wxFont(18, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, wxEmptyString));
    m_WorkingDaysAt->SetForegroundColour(wxColor(232, 9, 210));
    v_sizer_2->Add(m_WorkingDaysAt);
    UpdateWorkingDays();

    static_box_grid->Add(tracker_grid->m_grid);
    bSizer1->Add(static_box_grid, wxSizerFlags(0).Top());
    bSizer1->Add(v_sizer);
    bSizer1->AddSpacer(20);
    bSizer1->Add(v_sizer_2);


    m_WorkingDaysSk->Hide();
    m_WorkingDaysHu->Hide();
    m_WorkingDaysAt->Hide();

    //m_TimeCounter->Hide();
    SetSizerAndFit(bSizer1);
    Layout();
}

void TimeTrackerPanel::OnCellValueChanged(wxGridEvent& ev)
{
	auto row = tracker_grid->grid_to_entry_id.find(ev.GetRow());
	if(row == tracker_grid->grid_to_entry_id.end())
		return;

    TimeEntry* entry = m_tracker.FindEntry(row->second);
	if(!entry)
    {
        ScheduleRefresh();
		return;
    }

    const int entry_id = entry->sql_id;
    if(lastTimeEntryId == entry_id && ev.GetCol() != TimeTrackerCol::TimeTracker_Comment)
    {
        wxMessageBox("Only the comment can be edited while this entry is running.",
            "Time tracking is active", wxOK | wxICON_INFORMATION, this);
        ScheduleRefresh();
        return;
    }

    bool valid_edit = false;
    bool saved = false;
	switch(ev.GetCol())
	{
        case TimeTrackerCol::TimeTracker_Date:
        {
			const std::string date_str = tracker_grid->m_grid->GetCellValue(
                wxGridCellCoords(ev.GetRow(), TimeTrackerCol::TimeTracker_Date)).ToStdString();
			static const boost::regex date_regex("^([0-9]{4})-([0-1][0-9])-([0-3][0-9])$");
            if(boost::regex_match(date_str, date_regex))
            {
                /* The regex has already established that these three are
                   numeric, so the try is here for gregorian::date - the regex
                   admits 2024-02-31 and 2024-00-00, which it rejects. */
                try
                {
                    const size_t dash_pos1 = date_str.find('-');
                    const size_t dash_pos2 = date_str.find('-', dash_pos1 + 1);
                    const int year = utils::ParseOr<int>(date_str.substr(0, dash_pos1), 0);
                    const int month = utils::ParseOr<int>(date_str.substr(dash_pos1 + 1, dash_pos2 - dash_pos1 - 1), 0);
                    const int day = utils::ParseOr<int>(date_str.substr(dash_pos2 + 1), 0);
                    const boost::gregorian::date new_date(year, month, day);
                    const auto date_shift = new_date - entry->start.date();
                    valid_edit = true;
                    saved = m_tracker.EditEntry(entry_id,
                        entry->start + date_shift, entry->end + date_shift, entry->desc);
                }
                catch(const std::exception&)
                {
                    // The deferred refresh restores the previous valid value.
                }
            }
            break;
        }
        case TimeTrackerCol::TimeTracker_Start:
        case TimeTrackerCol::TimeTracker_End:
        {
            const int column = ev.GetCol();
            const std::string time_str = tracker_grid->m_grid->GetCellValue(
                wxGridCellCoords(ev.GetRow(), column)).ToStdString();
            static const boost::regex hhmm_regex("^([0-1]?[0-9]|2[0-3]):([0-5][0-9])$");
            if(boost::regex_match(time_str, hhmm_regex))
            {
                try
                {
                    const size_t colon_pos = time_str.find(':');
                    const int hours = utils::ParseOr<int>(time_str.substr(0, colon_pos), 0);
                    const int minutes = utils::ParseOr<int>(time_str.substr(colon_pos + 1), 0);
                    const boost::posix_time::time_duration new_time(hours, minutes, 0);
                    auto new_start = entry->start;
                    auto new_end = entry->end;
                    if(column == TimeTrackerCol::TimeTracker_Start)
                        new_start = boost::posix_time::ptime(entry->start.date(), new_time);
                    else
                        new_end = boost::posix_time::ptime(entry->start.date(), new_time);
                    valid_edit = true;
                    saved = m_tracker.EditEntry(entry_id, new_start, new_end, entry->desc);
                }
                catch(const std::exception&)
                {
                    // The deferred refresh restores the previous valid value.
                }
            }
            break;
        }
        case TimeTrackerCol::TimeTracker_Comment:
        {
            valid_edit = true;
            const std::string comment = tracker_grid->m_grid->GetCellValue(
                wxGridCellCoords(ev.GetRow(), TimeTrackerCol::TimeTracker_Comment)).ToStdString();
            saved = m_tracker.EditEntry(entry_id, entry->start, entry->end, comment);
            break;
        }
	    default:
		    return;
	}

    if(saved)
        m_tracker.UpdateEntries();
    else if(valid_edit)
        wxMessageBox(m_tracker.LastError(), "Unable to save time entry", wxOK | wxICON_ERROR, this);

    // Deleting rows while EVT_GRID_CELL_CHANGED is still unwinding can leave
    // wxGrid's edit control referencing a destroyed row. Rebuild afterwards.
    ScheduleRefresh();
}

void TimeTrackerPanel::OnCellRightClick(wxGridEvent& ev)
{
    if (ev.GetEventObject() == static_cast<wxObject*>(tracker_grid->m_grid))
    {
        FinishActiveEdit();
        const gui::MenuEntry entries[]{
            gui::MenuCommand{ "&Add", [this]
                {
                    const auto now = boost::posix_time::second_clock::local_time();
                    if(!m_tracker.AddEntry(now, now, ""))
                    {
                        wxMessageBox(m_tracker.LastError(), "Unable to add time entry", wxOK | wxICON_ERROR, this);
                        return;
                    }
                    m_tracker.UpdateEntries();
                    RefreshPanel();
                }, wxART_ADD_BOOKMARK },
            gui::MenuCommand{ "&Delete", [this, row = ev.GetRow()]
                {
                    const auto entry = tracker_grid->grid_to_entry_id.find(row);
                    if(entry == tracker_grid->grid_to_entry_id.end())
                        return;

                    if(lastTimeEntryId == entry->second)
                    {
                        wxMessageDialog(this, "Given entry can\'t be removed\nStop the worktime counter, then try again!", "Error", wxOK).ShowModal();
                        return;
                    }

                    if(m_tracker.RemoveEntry(entry->second))
                        m_tracker.UpdateEntries();
                    else
                        wxMessageBox(m_tracker.LastError(), "Unable to delete time entry", wxOK | wxICON_ERROR, this);
                    RefreshPanel();
                }, wxART_DELETE },
        };
        gui::RunContextMenu(this, entries);
    }
}

void TimeTrackerPanel::OnKeyDown(wxKeyEvent& evt)
{
    if (evt.ControlDown())
    {
        switch (evt.GetKeyCode())
        {
            case 'C':
            {
                if (wxWindow::FindFocus() == tracker_grid->m_grid &&
                    gui::CopySelectedRowsToClipboard(*tracker_grid->m_grid, TimeTrackerCol::TimeTracker_Max))
                {
                    PostAppNotification(SimpleNotification{SimpleNotificationKind::SelectedLogsCopied});
                }
                break;
            }
        }
    }
    evt.Skip();
}

void TimeTrackerPanel::RefreshPanel()
{
    FinishActiveEdit();
    tracker_grid->ClearRows();
    is_inited = false;
}

void TimeTrackerPanel::FinishActiveEdit()
{
    if(!tracker_grid || !tracker_grid->m_grid ||
       !tracker_grid->m_grid->IsCellEditControlEnabled())
        return;

    tracker_grid->m_grid->SaveEditControlValue();
    tracker_grid->m_grid->DisableCellEditControl();
}

void TimeTrackerPanel::ScheduleRefresh()
{
    if(refresh_pending)
        return;

    refresh_pending = true;
    CallAfter([this]
        {
            RefreshPanel();
            refresh_pending = false;
        });
}

void TimeTrackerPanel::SetWorktimeUi(bool working)
{
    m_StartButton->SetBackgroundColour(working ? *wxRED : wxNullColour);
    m_StartButton->SetLabelText(working ? "Stop" : "Start");
    m_TimeCounter->SetForegroundColour(working ? wxColor(242, 141, 68) : *wxBLACK);
    if(!working)
        m_TimeCounter->SetLabel("00:00:00");
}

void TimeTrackerPanel::On10MsTimer()
{
	HandleElapsedTime();
    HandleInit();
}

void TimeTrackerPanel::HandleElapsedTime()
{
    if(!is_working)
        return;

    // Calculate the time difference
    auto now = boost::posix_time::second_clock::local_time();
    auto duration = now - lastTimeEntryStart;
    const std::int64_t elapsed_seconds = std::max<std::int64_t>(0, duration.total_seconds());

    wxString formattedTime = utils::SecondsToHms(static_cast<int>(elapsed_seconds));

    // Update the static text
    if (m_TimeCounter)
    {
        m_TimeCounter->SetLabel(formattedTime);
    }


    if(tracker_grid->m_grid->IsCellEditControlEnabled() ||
       !time_tracker_logic::ShouldPersistElapsedMinute(elapsed_seconds, lastPersistedMinute))
        return;

    lastPersistedMinute = elapsed_seconds / 60;
    if(!lastTimeEntryId)
    {
        is_working = false;
        SetWorktimeUi(false);
        return;
    }

    const TimeEntry* entry = m_tracker.FindEntry(*lastTimeEntryId);
    if(!entry)
    {
        is_working = false;
        lastTimeEntryId.reset();
        SetWorktimeUi(false);
        PostAppNotification(WorktimeToggledNotification{false, std::chrono::seconds{elapsed_seconds}});
        return;
    }

    const auto entry_start = entry->start;
    const auto comment = entry->desc;
    bool saved = false;
    if(entry_start.date() == now.date())
    {
        saved = m_tracker.EditEntry(*lastTimeEntryId, entry_start, now, comment);
    }
    else
    {
        // Close the previous day precisely at midnight before starting the new
        // entry. Keeping only IDs prevents either grid refresh from invalidating
        // the running-session state.
        const boost::posix_time::ptime midnight(now.date());
        saved = m_tracker.EditEntry(*lastTimeEntryId, entry_start, midnight, comment);
        if(saved)
        {
            TimeEntry* next_entry = m_tracker.AddEntry(midnight, now,
                time_tracker_logic::ContinuationName(comment));
            if(next_entry)
            {
                lastTimeEntryId = next_entry->sql_id;
                lastTimeEntryStart = midnight;
                lastPersistedMinute = (now - midnight).total_seconds() / 60;
            }
            else
            {
                is_working = false;
                lastTimeEntryId.reset();
                SetWorktimeUi(false);
                PostAppNotification(WorktimeToggledNotification{false, std::chrono::seconds{elapsed_seconds}});
                return;
            }
        }
    }

    if(saved)
    {
        m_tracker.UpdateEntries();
        RefreshPanel();
    }
}

void TimeTrackerPanel::HandleInit()
{
	if (is_inited)
		return;


    boost::gregorian::date date(m_tracker.GetYear(), m_tracker.GetMonth(), 1);
    boost::posix_time::ptime posixt(date);  // time defaults to 00:00:00

	int offset = m_tracker.CalculateMapDateOffset(posixt);
    const auto& entries = m_tracker.GetEntries();

	auto it = entries.find(offset);

    if (it == entries.end())
    {
        is_inited = true;
        return;
    }

	auto& serialized_entries = it->second->serialized_entries;
	for (auto& entry : serialized_entries)
	{
		for (auto& serialized_entry : entry.second)
		{
			tracker_grid->AddRowSerialized(serialized_entry.get());
		}
	}

	is_inited = true;
    tracker_grid->m_grid->SetColLabelValue(TimeTrackerCol::TimeTracker_TotalWork,
        time_tracker_logic::FormatTotalWorkLabel(it->second->total_worktime, m_tracker.GetHourlyRate()));

    AdjustColumns();
}

void TimeTrackerPanel::UpdateWorkingDays()
{
    m_WorkingDays.Update();
    m_WorkingDaysSk->SetLabelText(wxString::Format("SK: %d [%d] - (%d)", m_WorkingDays.m_WorkingDaysSlovakia, m_WorkingDays.m_WorkingDaysSlovakia * 8,
        m_WorkingDays.m_HolidaysSlovakia));
    m_WorkingDaysSk->SetToolTip(wxString::Format("Holidays:\n%s", m_WorkingDays.m_HolidaysStrSlovakia));

    m_WorkingDaysHu->SetLabelText(wxString::Format("HU: %d [%d] - (%d)", m_WorkingDays.m_WorkingDaysHungary, m_WorkingDays.m_WorkingDaysHungary * 8,
        m_WorkingDays.m_HolidaysHungary));
    m_WorkingDaysHu->SetToolTip(wxString::Format("Holidays:\n%s", m_WorkingDays.m_HolidaysStrHungary));

    m_WorkingDaysAt->SetLabelText(wxString::Format("AT: %d [%d] - (%d)", m_WorkingDays.m_WorkingDaysAustria, m_WorkingDays.m_WorkingDaysAustria * 8,
        m_WorkingDays.m_HolidaysAustria));
    m_WorkingDaysAt->SetToolTip(wxString::Format("Holidays:\n%s", m_WorkingDays.m_HolidaysStrAustria));

    UpdateCurrentWeekNumber();
}

wxString TimeTrackerPanel::FormatCurrentWeekNumber()
{
    boost::gregorian::date current_date(boost::gregorian::day_clock::local_day());
    int week_number = current_date.week_number();
    wxString week_str = wxString::Format("Week: %d", week_number);
    return week_str;
}

void TimeTrackerPanel::UpdateCurrentWeekNumber()
{
    m_WeekNumber->SetLabelText(FormatCurrentWeekNumber());
}

int GetNumberVisibleRows(wxGrid* grid) {  
   if (!grid)  
       return 0;  

   int clientHeight = grid->GetClientSize().GetHeight();  
   int rowHeight = grid->GetDefaultRowSize();  

   if (rowHeight == 0)  
       return 0;  

   return clientHeight / rowHeight;  
}  

/* AutoSizeGrid, StoreColumnRatios and ApplyColumnRatios were here - a
   second column-width algorithm competing with AdjustColumns below. Nothing
   called AutoSizeGrid, so the ratio vector was never populated, so the
   ApplyColumnRatios call OnSize made was guarded into a no-op: one live
   algorithm and ninety lines of dead one that looked alive. */

void TimeTrackerPanel::AdjustColumns()
{
    Freeze();

    const int scrollbarWidth = wxSystemSettings::GetMetric(wxSYS_VSCROLL_X);
    const int rowLabelWidth = tracker_grid->m_grid->GetRowLabelSize();
    const int borderWidth = 2;

    // Calculate available width
    int availableWidth = GetClientSize().GetWidth() - rowLabelWidth - borderWidth;

    // If vertical scrollbar might appear
    if (tracker_grid->m_grid->GetNumberRows() > GetNumberVisibleRows(tracker_grid->m_grid)) {
        availableWidth -= scrollbarWidth;
    }

    if (availableWidth <= 0) {
        Thaw();
        return;
    }

    // Calculate total of default sizes
    int totalDefault = 0;
    for (int size : m_defaultSizes) {
        totalDefault += size;
    }

    // Distribute space
    int remainingWidth = availableWidth;
    int colsProcessed = 0;

    for (int col = 0; col < TimeTracker_Max; col++) {
        // For all columns except last, calculate proportional width
        if (col < TimeTracker_Max - 1) {
            double ratio = static_cast<double>(m_defaultSizes[col]) / totalDefault;
            int newWidth = static_cast<int>(availableWidth * ratio);
            newWidth = wxMax(newWidth, 30); // Minimum width

            tracker_grid->m_grid->SetColSize(col, newWidth);
            remainingWidth -= newWidth;
        }
        // Last column gets remaining space
        else {
            tracker_grid->m_grid->SetColSize(col, wxMax(remainingWidth, 30));
        }
        colsProcessed++;
    }

    Thaw();
    Refresh();
}

void TimeTrackerPanel::OnSize(wxSizeEvent& event)
{
    event.Skip(); // Important for proper layout handling

    SetSize(event.GetSize());
    tracker_grid->m_grid->SetMinSize(event.GetSize() - wxSize(50, 120));

    // Adjust grid height based on content if needed
    AdjustColumns();
}
