#include "pch.hpp"

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

TimeTrackerGrid::TimeTrackerGrid(wxWindow* parent)
{
    m_grid = new wxGrid(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, 0);
    m_grid->SetMinSize(wxSize(1024, 768));
    m_grid->SetMaxSize(wxSize(2048, 2048));

    // Grid
    m_grid->CreateGrid(1, TimeTrackerCol::TimeTracker_Max);
    m_grid->EnableEditing(true);
    m_grid->EnableGridLines(true);
    m_grid->EnableDragGridSize(false);
    m_grid->SetMargins(0, 0);

    m_grid->SetColLabelValue(TimeTrackerCol::TimeTracker_Date, "Time");
    m_grid->SetColLabelValue(TimeTrackerCol::TimeTracker_Start, "Start");
    m_grid->SetColLabelValue(TimeTrackerCol::TimeTracker_End, "End");
    m_grid->SetColLabelValue(TimeTrackerCol::TimeTracker_Comment, "Comment");
    m_grid->SetColLabelValue(TimeTrackerCol::TimeTracker_Hours, "Hours");
    m_grid->SetColLabelValue(TimeTrackerCol::TimeTracker_TotalWork, "Total Work");

    // Columns
    m_grid->EnableDragColMove(true);
    m_grid->EnableDragColSize(true);
    m_grid->SetColLabelAlignment(wxALIGN_CENTER, wxALIGN_CENTER);

    m_grid->SetSelectionMode(wxGrid::wxGridSelectionModes::wxGridSelectRows);

    // Rows
    m_grid->EnableDragRowSize(true);
    m_grid->SetRowLabelAlignment(wxALIGN_CENTER, wxALIGN_CENTER);


    // Label Appearance

    // Cell Defaults
    m_grid->SetDefaultCellAlignment(wxALIGN_LEFT, wxALIGN_TOP);
    m_grid->HideRowLabels();

    m_grid->SetColSize(TimeTrackerCol::TimeTracker_Date, 70);
    m_grid->SetColSize(TimeTrackerCol::TimeTracker_Start, 50);
    m_grid->SetColSize(TimeTrackerCol::TimeTracker_End, 50);
    m_grid->SetColSize(TimeTrackerCol::TimeTracker_Comment, 400);
    m_grid->SetColSize(TimeTrackerCol::TimeTracker_Hours, 50);
    m_grid->SetColSize(TimeTrackerCol::TimeTracker_TotalWork, 135);

    m_grid->AutoSizeRows();
    m_grid->AutoSizeColumns();
}

void TimeTrackerGrid::AddRow(TimeEntry* entry)
{
    static boost::gregorian::date lastSetDate; // Tracks the last set date
    static boost::posix_time::time_duration totalDurationForDay(0, 0, 0); // Tracks total duration for the current day
    static boost::posix_time::time_duration totalDuration(0, 0, 0); // Tracks total duration
    static bool isGray = true;

    int num_rows = m_grid->GetNumberRows();
    if (num_rows <= cnt)
        m_grid->AppendRows(1);

    // Extract the date from the entry's start time
    boost::gregorian::date currentDate = entry->start.date();

    // If the date changes, set the total hours for the previous day
    if (currentDate != lastSetDate)
    {
        wxString totalHoursStr = wxString::Format("%02lld:%02lld:%02lld",
            totalDurationForDay.hours(),
            totalDurationForDay.minutes(),
            totalDurationForDay.seconds());
        m_grid->SetCellValue(wxGridCellCoords(cnt, TimeTrackerCol::TimeTracker_TotalWork), totalHoursStr);

        // Reset the total duration for the new day
        totalDurationForDay = boost::posix_time::time_duration(0, 0, 0);

        // Alternate the row color
        isGray = !isGray;

        // Set the date for the new day
        wxString dateStr = wxString::Format("%04d-%02d-%02d",
            static_cast<int>(currentDate.year()),
            static_cast<int>(currentDate.month().as_number()),
            static_cast<int>(currentDate.day()));
        m_grid->SetCellValue(wxGridCellCoords(cnt, TimeTrackerCol::TimeTracker_Date), dateStr);
        lastSetDate = currentDate; // Update the last set date
    }

    // Calculate the duration for the current entry and add it to the total duration for the day
    boost::posix_time::time_duration entryDuration = entry->end - entry->start;
    totalDurationForDay += entryDuration;
    totalDuration += entryDuration;

    // Set the start time
    boost::posix_time::time_duration startDuration = entry->start.time_of_day();
    wxString timeStartStr = wxString::Format("%02lld:%02lld", startDuration.hours(), startDuration.minutes());
    m_grid->SetCellValue(wxGridCellCoords(cnt, TimeTrackerCol::TimeTracker_Start), timeStartStr);

    // Set the end time
    boost::posix_time::time_duration endDuration = entry->end.time_of_day();
    wxString timeEndStr = wxString::Format("%02lld:%02lld", endDuration.hours(), endDuration.minutes());
    m_grid->SetCellValue(wxGridCellCoords(cnt, TimeTrackerCol::TimeTracker_End), timeEndStr);

    // Set the time difference (duration) in the TimeTracker_Hours column
	if (entryDuration.hours() < 0)
		entryDuration = boost::posix_time::time_duration(0, 0, 0);
    wxString timeDiffStr = wxString::Format("%02lld:%02lld", entryDuration.hours(), entryDuration.minutes());
    m_grid->SetCellValue(wxGridCellCoords(cnt, TimeTrackerCol::TimeTracker_Hours), timeDiffStr);

    // Set the comment
    m_grid->SetCellValue(wxGridCellCoords(cnt, TimeTrackerCol::TimeTracker_Comment), entry->desc);

    // Apply the row color based on the current state of `isGray`
    wxColor rowColor = isGray ? wxColor(230, 230, 230) : wxColor(255, 255, 255);
    for (int col = 0; col < TimeTrackerCol::TimeTracker_Max; ++col)
    {
        m_grid->SetCellBackgroundColour(cnt, col, rowColor);
    }

    if (totalDuration.hours() < 0)
		totalDuration = boost::posix_time::time_duration(0, 0, 0);

    std::unique_ptr<TimeTracker>& time_tracker = wxGetApp().time_tracker;
    m_grid->SetColLabelValue(TimeTrackerCol::TimeTracker_TotalWork,
        wxString::Format("%lld [h] / %lld€", totalDuration.hours(), totalDuration.hours() * time_tracker->GetHourlyRate()));

    // Map the entry to the grid row
    grid_to_entry[cnt] = entry;
    cnt++;
}

void TimeTrackerGrid::AddRowSerialized(TimeEntrySerialized* entry)
{
    int num_rows = m_grid->GetNumberRows();
    if (num_rows <= cnt)
        m_grid->AppendRows(1);

    if (!entry->date.is_not_a_date())
    {
		wxString dateStr = wxString::Format("%04d-%02d-%02d",
			static_cast<int>(entry->date.year()),
			static_cast<int>(entry->date.month().as_number()),
			static_cast<int>(entry->date.day()));
		m_grid->SetCellValue(wxGridCellCoords(cnt, TimeTrackerCol::TimeTracker_Date), dateStr);
    }

    wxString timeStartStr = wxString::Format("%02lld:%02lld", entry->start.hours(), entry->start.minutes());
    m_grid->SetCellValue(wxGridCellCoords(cnt, TimeTrackerCol::TimeTracker_Start), timeStartStr);

    // Set the end time
    wxString timeEndStr = wxString::Format("%02lld:%02lld", entry->end.hours(), entry->end.minutes());
    m_grid->SetCellValue(wxGridCellCoords(cnt, TimeTrackerCol::TimeTracker_End), timeEndStr);

	auto entryDuration = entry->end - entry->start;
    if (entryDuration.hours() < 0)
        entryDuration = boost::posix_time::time_duration(0, 0, 0);
    wxString timeDiffStr = wxString::Format("%02lld:%02lld", entryDuration.hours(), entryDuration.minutes());
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

	grid_to_entry[cnt] = entry->entry;
	cnt++;
}

void TimeTrackerGrid::ClearRows()
{
	m_grid->ClearGrid();
    if(m_grid->GetNumberRows())
	    m_grid->DeleteRows(0, m_grid->GetNumberRows());
	grid_to_entry.clear();
	cnt = 0;
}

void TimeTrackerPanel::ToggleWorktime()
{
	boost::posix_time::time_duration duration = boost::posix_time::time_duration(0, 0, 0);
    std::unique_ptr<TimeTracker>& time_tracker = wxGetApp().time_tracker;
    if (!is_working)
    {
        auto time = boost::posix_time::second_clock::local_time();
        TimeEntry* time_entry = time_tracker->AddEntry(time, time, "");
        lastTimeEntry = time_entry;
		lastTimeEntryStart = time_entry->start;

        tracker_grid->AddRow(time_entry);
        time_tracker->UpdateEntries();
        RefreshPanel();

        m_StartButton->SetBackgroundColour(*wxRED);
        m_StartButton->SetLabelText("Stop");
        //m_TimeCounter->Show();
        m_TimeCounter->SetForegroundColour(wxColor(242, 141, 68));
    }
    else
    {
        time_tracker->EditEntry(lastTimeEntry, lastTimeEntry->start, boost::posix_time::second_clock::local_time(), lastTimeEntry->desc);
        time_tracker->UpdateEntries();
        RefreshPanel();

        m_StartButton->SetBackgroundColour(wxNullColour);
        m_StartButton->SetLabelText("Start");
        //m_TimeCounter->Hide();
        m_TimeCounter->SetForegroundColour(*wxBLACK);

		duration = lastTimeEntry->end - lastTimeEntry->start;
        lastTimeEntry = nullptr;
    }

    is_working = !is_working;

    MyFrame* frame = ((MyFrame*)(wxGetApp().GetTopWindow()));
    std::lock_guard lock(frame->mtx);
    frame->pending_msgs.push_back({ static_cast<uint8_t>(PopupMsgIds::WorktimeToggled), is_working, duration });
}

TimeTrackerPanel::TimeTrackerPanel(wxFrame* parent) :
    wxPanel(parent, wxID_ANY)
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
    years.Add("2023");
    years.Add("2024");
    years.Add("2025");
    m_WorktimeYear = new wxChoice(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, years);
    m_WorktimeYear->SetStringSelection("2024");
    v_sizer_0->Add(m_WorktimeYear);

    m_RefreshButton = new wxButton(this, wxID_ANY, "Refresh");
    v_sizer_0->Add(m_RefreshButton);
    m_RefreshButton->Bind(wxEVT_BUTTON, [=](wxCommandEvent&)
        {
            wxString month_str = m_WorktimeMonth->GetStringSelection();
            wxString year_str = m_WorktimeYear->GetStringSelection();

            long month_int = m_WorktimeMonth->GetSelection() + 1;
            long year_int = -1;

            bool validMonth = 1;
            bool validYear = year_str.ToLong(&year_int);

            // Additional optional range checks
            if (!validMonth || month_int < 0 || month_int > 12) {
                wxMessageBox("Invalid month selected. Please choose a valid numeric month (1-12).", "Error", wxICON_ERROR);
                return;
            }

            if (!validYear || year_int < 1900 || year_int > 2100) {
                wxMessageBox("Invalid year selected. Please choose a valid year (e.g. 2023).", "Error", wxICON_ERROR);
                return;
            }

            std::unique_ptr<TimeTracker>& time_tracker = wxGetApp().time_tracker;
            time_tracker->SetActualYearMonth(year_int, month_int);
            RefreshPanel();
            time_tracker->LoadEntries(year_int, month_int);
            RefreshPanel();
        });

    std::unique_ptr<TimeTracker>& time_tracker = wxGetApp().time_tracker;
    m_WorktimeMonth->SetSelection(time_tracker->m_Month - 1);
    m_WorktimeYear->SetSelection(time_tracker->m_Year - 2023);

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
	if (tracker_grid->grid_to_entry.empty())
		return;
	auto it = tracker_grid->grid_to_entry.find(ev.GetRow());
	if (it == tracker_grid->grid_to_entry.end())
		return;
	auto entry = it->second;
	if (entry == nullptr)
		return;

    std::unique_ptr<TimeTracker>& time_tracker = wxGetApp().time_tracker;
	switch (ev.GetCol())
	{
        case TimeTrackerCol::TimeTracker_Date:
        {
			std::string date_str = tracker_grid->m_grid->GetCellValue(wxGridCellCoords(ev.GetRow(), TimeTrackerCol::TimeTracker_Date)).ToStdString();
			static const boost::regex date_regex("^([0-9]{4})-([0-1][0-9])-([0-3][0-9])$");
            if (boost::regex_match(date_str, date_regex))
            {
                try

                {
                    // Extract year, month, and day
                    size_t dash_pos1 = date_str.find('-');
                    size_t dash_pos2 = date_str.find('-', dash_pos1 + 1);
                    int year = std::stoi(date_str.substr(0, dash_pos1));
                    int month = std::stoi(date_str.substr(dash_pos1 + 1, dash_pos2 - dash_pos1 - 1));
                    int day = std::stoi(date_str.substr(dash_pos2 + 1));
                    // Create a new date object
                    boost::gregorian::date new_date(year, month, day);
                    // Update entry->start and entry->end with the new date
                    boost::posix_time::time_duration start_time = entry->start.time_of_day();
                    boost::posix_time::time_duration end_time = entry->end.time_of_day();
                    entry->start = boost::posix_time::ptime(new_date, start_time);
                    entry->end = boost::posix_time::ptime(new_date, end_time);
                    DBG("Final date and time: %s\n", boost::posix_time::to_simple_string(entry->start).c_str());

                    time_tracker->SaveEntry(it->second);
                    time_tracker->UpdateEntries();
                    RefreshPanel();
                }
                catch (...)
                {

                    // Silently ignore invalid entries (e.g., stoi fails)
                    // or invalid date (e.g., 2023-02-30)
                }
            }
            break;
        }
        case TimeTrackerCol::TimeTracker_Start:
        {
            std::string start_str = tracker_grid->m_grid->GetCellValue(wxGridCellCoords(ev.GetRow(), TimeTrackerCol::TimeTracker_Start)).ToStdString();
            static const boost::regex hhmm_regex("^([0-1]?[0-9]|2[0-3]):([0-5][0-9])$");
            if (boost::regex_match(start_str, hhmm_regex))
            {
                try
                {
                    // Extract hours and minutes
                    size_t colon_pos = start_str.find(':');
                    int hours = std::stoi(start_str.substr(0, colon_pos));
                    int minutes = std::stoi(start_str.substr(colon_pos + 1));

                    // Get the original date from entry->end
                    const boost::gregorian::date original_date = entry->start.date();

                    // Create a new time_duration (hh:mm:00)
                    boost::posix_time::time_duration new_time(hours, minutes, 0);

                    // Update entry->end with the original date + new time
                    entry->start = boost::posix_time::ptime(original_date, new_time);

                    DBG("Final date and time: %s\n", boost::posix_time::to_simple_string(entry->start).c_str());
                }
                catch (...)
                {
                    // Silently ignore invalid entries (e.g., stoi fails)
                }
            }

            time_tracker->SaveEntry(it->second);
            time_tracker->UpdateEntries();
            RefreshPanel();
            break;
        }
        case TimeTrackerCol::TimeTracker_End:
        {
            std::string end_str = tracker_grid->m_grid->GetCellValue(wxGridCellCoords(ev.GetRow(), TimeTrackerCol::TimeTracker_End)).ToStdString();
            static const boost::regex hhmm_regex("^([0-1]?[0-9]|2[0-3]):([0-5][0-9])$");
            if (boost::regex_match(end_str, hhmm_regex)) 
            {
                try 
                {
                    // Extract hours and minutes
                    size_t colon_pos = end_str.find(':');
                    int hours = std::stoi(end_str.substr(0, colon_pos));
                    int minutes = std::stoi(end_str.substr(colon_pos + 1));

                    // Get the original date from entry->start
                    const boost::gregorian::date original_date = entry->start.date();

                    // Create a new time_duration (hh:mm:00)
                    boost::posix_time::time_duration new_time(hours, minutes, 0);

                    // Update entry->end with the original date + new time
                    entry->end = boost::posix_time::ptime(original_date, new_time);

                    DBG("Final date and time: %s\n", boost::posix_time::to_simple_string(entry->end).c_str());
                }
                catch (...) 
                {
                    // Silently ignore invalid entries (e.g., stoi fails)
                }
            }

            time_tracker->SaveEntry(it->second);
            time_tracker->UpdateEntries();
            RefreshPanel();
            break;
        }
        case TimeTrackerCol::TimeTracker_Comment:
        {
            std::string str_comment = tracker_grid->m_grid->GetCellValue(wxGridCellCoords(ev.GetRow(), TimeTrackerCol::TimeTracker_Comment)).ToStdString();
            it->second->desc = str_comment;

			time_tracker->SaveEntry(it->second);
            time_tracker->UpdateEntries();
            RefreshPanel();
            break;
        }
	    default:
		    break;
	}
}

void TimeTrackerPanel::OnCellRightClick(wxGridEvent& ev)
{
    if (ev.GetEventObject() == dynamic_cast<wxObject*>(tracker_grid->m_grid))
    {
        wxMenu menu;
        menu.Append(ID_TimesheetAdd, "&Add")->SetBitmap(wxArtProvider::GetBitmap(wxART_ADD_BOOKMARK, wxART_OTHER, FromDIP(wxSize(14, 14))));
        menu.Append(ID_TimesheetDelete, "&Delete")->SetBitmap(wxArtProvider::GetBitmap(wxART_DELETE, wxART_OTHER, FromDIP(wxSize(14, 14))));
        int ret = GetPopupMenuSelectionFromUser(menu);
        switch (ret)
        {
            case ID_TimesheetAdd:
            {
                int row = ev.GetRow();
                std::unique_ptr<TimeTracker>& time_tracker = wxGetApp().time_tracker;

				time_tracker->AddEntry(boost::posix_time::second_clock::local_time(), boost::posix_time::second_clock::local_time(), "");
                time_tracker->UpdateEntries();
                RefreshPanel();
                break;
            }
            case ID_TimesheetDelete:
            {
                int row = ev.GetRow();
                std::unique_ptr<TimeTracker>& time_tracker = wxGetApp().time_tracker;

                if (lastTimeEntry == tracker_grid->grid_to_entry[row])
                {
                    wxMessageDialog(this, "Given entry can't be removed\nStop the worktime counter, then try again!", "Error", wxOK).ShowModal();
                    return;
                }

				time_tracker->RemoveEntry(tracker_grid->grid_to_entry[row]);
                time_tracker->UpdateEntries();
				RefreshPanel();
                break;
            }
        }
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
                wxWindow* focus = wxWindow::FindFocus();
                if (focus == tracker_grid->m_grid)
                {
                    wxArrayInt rows = tracker_grid->m_grid->GetSelectedRows();
                    if (rows.empty()) return;

                    wxString str_to_copy;
                    for (auto& row : rows)
                    {
                        for (uint8_t col = 0; col < TimeTrackerCol::TimeTracker_Max; col++)
                        {
                            str_to_copy += tracker_grid->m_grid->GetCellValue(row, col);
                            str_to_copy += '\t';
                        }
                        str_to_copy += '\n';
                    }
                    if (str_to_copy.Last() == '\n')
                        str_to_copy.RemoveLast();

                    if (wxTheClipboard->Open())
                    {
                        wxTheClipboard->SetData(new wxTextDataObject(str_to_copy));
                        wxTheClipboard->Close();
                        MyFrame* frame = ((MyFrame*)(wxGetApp().GetTopWindow()));
                        frame->pending_msgs.push_back({ static_cast<uint8_t>(PopupMsgIds::SelectedLogsCopied) });
                    }
                }
                break;
            }
        }
    }
    evt.Skip();
}

void TimeTrackerPanel::RefreshPanel()
{
    tracker_grid->ClearRows();
    is_inited = false;
}

void TimeTrackerPanel::On10MsTimer()
{
	HandleElapsedTime();
    HandleInit();
}

void TimeTrackerPanel::HandleElapsedTime()
{
    if (tracker_grid->grid_to_entry.empty() || !is_working)
        return;

    // Calculate the time difference
    auto now = boost::posix_time::second_clock::local_time();
    auto duration = now - lastTimeEntryStart;

    // Format the duration into hh:mm:ss
    int hours = duration.hours();
    int minutes = duration.minutes();
    int seconds = duration.seconds();
    wxString formattedTime = wxString::Format("%02d:%02d:%02d", hours, minutes, seconds);

    // Update the static text
    if (m_TimeCounter)
    {
        m_TimeCounter->SetLabel(formattedTime);
    }


    if (duration.total_seconds() % 60 == 0 && !tracker_grid->m_grid->IsCellEditControlEnabled())
    {
        std::unique_ptr<TimeTracker>& time_tracker = wxGetApp().time_tracker;
        auto now_time = boost::posix_time::second_clock::local_time();
        if (lastTimeEntry->start.date() == now_time.date())
        {
            time_tracker->EditEntry(lastTimeEntry, lastTimeEntry->start, boost::posix_time::second_clock::local_time(), lastTimeEntry->desc);
        }
        else  /* If it's overlapping to next day, save old one and start a new one */
        {
            TimeEntry* time_entry = time_tracker->AddEntry(now_time, now_time, std::format("%s #2", lastTimeEntry->desc));
            lastTimeEntry = time_entry;
            lastTimeEntryStart = time_entry->start;

            tracker_grid->AddRow(time_entry);
        }

        time_tracker->UpdateEntries();
        RefreshPanel();
    }
}

void TimeTrackerPanel::HandleInit()
{
	if (is_inited)
		return;

    std::unique_ptr<TimeTracker>& time_tracker = wxGetApp().time_tracker;

    boost::gregorian::date date(time_tracker->m_Year, time_tracker->m_Month, 1);
    boost::posix_time::ptime posixt(date);  // time defaults to 00:00:00

	int offset = time_tracker->CalculateMapDateOffset(posixt);
    const auto& entries = time_tracker->GetEntries();

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

	float hours = it->second->total_worktime / 3600.0f;
    wxString total_work = wxString::Format("%.2f / %.2f", hours, hours * time_tracker->GetHourlyRate());
	is_inited = true;  
    tracker_grid->m_grid->SetColLabelValue(TimeTrackerCol::TimeTracker_TotalWork, total_work);

    AdjustColumns();
}

void TimeTrackerPanel::UpdateWorkingDays()
{
    WorkingDays::Get()->Update();
    m_WorkingDaysSk->SetLabelText(wxString::Format("SK: %d [%d] - (%d)", WorkingDays::Get()->m_WorkingDaysSlovakia, WorkingDays::Get()->m_WorkingDaysSlovakia * 8,
        WorkingDays::Get()->m_HolidaysSlovakia));
    m_WorkingDaysSk->SetToolTip(wxString::Format("Holidays:\n%s", WorkingDays::Get()->m_HolidaysStrSlovakia));

    m_WorkingDaysHu->SetLabelText(wxString::Format("HU: %d [%d] - (%d)", WorkingDays::Get()->m_WorkingDaysHungary, WorkingDays::Get()->m_WorkingDaysHungary * 8,
        WorkingDays::Get()->m_HolidaysHungary));
    m_WorkingDaysHu->SetToolTip(wxString::Format("Holidays:\n%s", WorkingDays::Get()->m_HolidaysStrHungary));

    m_WorkingDaysAt->SetLabelText(wxString::Format("AT: %d [%d] - (%d)", WorkingDays::Get()->m_WorkingDaysAustria, WorkingDays::Get()->m_WorkingDaysAustria * 8,
        WorkingDays::Get()->m_HolidaysAustria));
    m_WorkingDaysAt->SetToolTip(wxString::Format("Holidays:\n%s", WorkingDays::Get()->m_HolidaysStrAustria));

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

void TimeTrackerPanel::AutoSizeGrid(bool initialFit)
{
    tracker_grid->m_grid->Freeze();

    if (initialFit) {
        // First-time setup - fit columns to content
        tracker_grid->m_grid->AutoSizeColumns(false);
        StoreColumnRatios();

        // Calculate total width needed
        int totalWidth = 0;
        for (int col = 0; col < tracker_grid->m_grid->GetNumberCols(); ++col) {
            totalWidth += tracker_grid->m_grid->GetColSize(col);
        }

        // Add margins for labels and scrollbars
        totalWidth += tracker_grid->m_grid->GetRowLabelSize() + 5;

        // Set initial size
        wxSize newSize(
            wxMin(totalWidth, GetClientSize().GetWidth() - 5),
            -1 // Keep current height
        );
        tracker_grid->m_grid->SetSize(newSize);
    }

    ApplyColumnRatios();
    tracker_grid->m_grid->AutoSizeRows();
    tracker_grid->m_grid->Thaw();
    Layout();
}

void TimeTrackerPanel::StoreColumnRatios()
{
    m_columnRatios.clear();
    int totalWidth = 0;

    // Calculate total width first
    for (int col = 0; col < tracker_grid->m_grid->GetNumberCols(); ++col) {
        totalWidth += tracker_grid->m_grid->GetColSize(col);
    }

    // Store ratios
    for (int col = 0; col < tracker_grid->m_grid->GetNumberCols(); ++col) {
        if (totalWidth > 0) {
            m_columnRatios.push_back(static_cast<double>(tracker_grid->m_grid->GetColSize(col)) / totalWidth);
        }
        else {
            m_columnRatios.push_back(1.0 / tracker_grid->m_grid->GetNumberCols());
        }
    }
}

void TimeTrackerPanel::ApplyColumnRatios()
{
    if (m_columnRatios.empty() || m_columnRatios.size() != static_cast<size_t>(tracker_grid->m_grid->GetNumberCols())) {
        return;
    }

    int availableWidth = tracker_grid->m_grid->GetClientSize().GetWidth() - tracker_grid->m_grid->GetRowLabelSize();
    if (availableWidth <= 0) return;

    tracker_grid->m_grid->Freeze();

    // Apply ratios
    for (int col = 0; col < tracker_grid->m_grid->GetNumberCols(); ++col) {
        int newWidth = static_cast<int>(availableWidth * m_columnRatios[col]);
        tracker_grid->m_grid->SetColSize(col, wxMax(newWidth, 70)); // Minimum 30px width
    }

    tracker_grid->m_grid->Thaw();
}

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

    // Only resize if we have column ratios stored
    if (!m_columnRatios.empty()) {
        ApplyColumnRatios();
    }

    SetSize(event.GetSize());
    tracker_grid->m_grid->SetMinSize(event.GetSize() - wxSize(50, 120));

    // Adjust grid height based on content if needed
    AdjustColumns();
}