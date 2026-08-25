#pragma once

#include <wx/wx.h>

#include <cstdint>
#include <map>
#include <optional>
#include <vector>

#include "../TimeTracker.hpp"

class WorkingDays;
class TimeTracker;

enum TimeTrackerCol : int
{
    TimeTracker_Date,
    TimeTracker_Start,
    TimeTracker_End,
    TimeTracker_Comment,
    TimeTracker_Hours,
    TimeTracker_TotalWork,
    TimeTracker_Max
};

class TimeTrackerGrid
{
public:
    TimeTrackerGrid(wxWindow* parent);

    /* The rate is passed in: the grid needs one number for its column
       label, not the whole tracker. */
    void AddRow(TimeEntry* entry, int hourly_rate);
    void AddRowSerialized(TimeEntrySerialized* entry);
    void ClearRows();

    wxGrid* m_grid = nullptr;

    /* Were function-local statics inside AddRow: state that survived
       ClearRows and belonged to every grid instance at once. Reset with
       the rows they describe. */
    boost::gregorian::date m_LastSetDate;
    boost::posix_time::time_duration m_TotalDurationForDay{0, 0, 0};
    boost::posix_time::time_duration m_TotalDuration{0, 0, 0};
    bool m_RowIsGray = true;

    // Rows are ephemeral during refreshes; durable SQL IDs are safe to retain
    // across model reloads, while TimeEntry pointers are not.
    std::map<int, int> grid_to_entry_id;

    size_t cnt = 0;
};

class TimeTrackerPanel : public wxPanel
{
public:
	TimeTrackerPanel(wxFrame* parent, TimeTracker& tracker, WorkingDays& working_days);
    TimeTrackerGrid* tracker_grid;

    wxStaticBoxSizer* static_box_grid = nullptr;

    wxButton* m_StartButton = nullptr;
    wxStaticText* m_TimeCounter = nullptr;
    wxStaticText* m_WeekNumber = nullptr;
    wxStaticText* m_WorkingDaysSk = nullptr;
    wxStaticText* m_WorkingDaysHu = nullptr;
    wxStaticText* m_WorkingDaysAt = nullptr;
    wxChoice* m_WorktimeMonth = nullptr;
	wxChoice* m_WorktimeYear = nullptr;
	wxButton* m_RefreshButton = nullptr;

    void On10MsTimer();
    void UpdateWorkingDays();
    void RefreshPanel();

    void ToggleWorktime();

private:
	std::optional<int> lastTimeEntryId;
	boost::posix_time::ptime lastTimeEntryStart;
    std::int64_t lastPersistedMinute{ 0 };
    const std::vector<int> m_defaultSizes = { 70, 50, 50, 400, 50, 135 };

    void HandleElapsedTime();
    void HandleInit();
    void FinishActiveEdit();
    void ScheduleRefresh();
    void SetWorktimeUi(bool working);

    void UpdateCurrentWeekNumber();
    wxString FormatCurrentWeekNumber();

    bool is_working{ false };
    bool is_inited{ false };
    bool refresh_pending{ false };

    void OnSize(wxSizeEvent& event);
    void OnCellValueChanged(wxGridEvent& ev);
    void OnCellRightClick(wxGridEvent& ev);
    void OnKeyDown(wxKeyEvent& evt);


    void AdjustColumns();

	/* Handed in rather than fetched from wxGetApp() on every use.
	   docs/code-style.md: "A class gets its collaborators through its
	   constructor. It does not fetch them." */
	TimeTracker& m_tracker;
	WorkingDays& m_WorkingDays;

	wxDECLARE_EVENT_TABLE();
};
