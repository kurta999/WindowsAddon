#pragma once

#include <wx/wx.h>
#include <semaphore>

#include "../TimeTracker.hpp"

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

    void AddRow(TimeEntry* entry);
    void AddRowSerialized(TimeEntrySerialized* entry);
    void ClearRows();

    wxGrid* m_grid = nullptr;

    std::map<uint16_t, TimeEntry*> grid_to_entry;  /* Helper map for storing an additional ID to CanTxEntry */
    std::map<uint16_t, uint16_t> did_to_row;  /* Helper map for storing an additional ID to CanTxEntry */

    size_t cnt = 0;
};

class TimeTrackerPanel : public wxPanel
{
public:
	TimeTrackerPanel(wxFrame* parent);
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
    TimeEntry* lastTimeEntry = nullptr;
	boost::posix_time::ptime lastTimeEntryStart;
    const std::vector<int> m_defaultSizes = { 70, 50, 50, 400, 50, 135 };

    void HandleElapsedTime();
    void HandleInit();

    void UpdateCurrentWeekNumber();
    wxString FormatCurrentWeekNumber();

    bool is_working{ false };
    bool is_inited{ false };

    void OnSize(wxSizeEvent& event);
    void OnCellValueChanged(wxGridEvent& ev);
    void OnCellRightClick(wxGridEvent& ev);
    void OnKeyDown(wxKeyEvent& evt);

    std::vector<double> m_columnRatios;

    void AutoSizeGrid(bool initialFit);
    void StoreColumnRatios();
    void ApplyColumnRatios();
    void AdjustColumns();

	wxDECLARE_EVENT_TABLE();
};
