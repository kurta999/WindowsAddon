#pragma once

#include <wx/wx.h>
#include "gui/LogRecordingBar.hpp"
#include <memory>
#include <wx/grid.h>

#include <chrono>

class CanEntryHandler;

enum CanLogGridCol : int
{
    Log_Time,
    Log_Direction,
    Log_Id,
    Log_DataSize,
    Log_Data,
    Log_Comment,
    Log_Max
};

class CanLogEntry;
class CanLogPanel : public wxPanel
{
public:
    CanLogPanel(wxWindow* parent, CanEntryHandler& handler);

    void On10MsTimer();
    void InsertRow(std::chrono::steady_clock::time_point& t1, uint8_t direction, uint32_t id, std::vector<uint8_t>& data, std::string& comment);
    void UpdatePanel();

    wxGrid* m_grid = nullptr;

private:

    void OnKeyDown(wxKeyEvent& evt);
    void OnLogLevelChange(wxSpinEvent& evt);
    void OnSize(wxSizeEvent& evt);
    void ClearRecordingsFromGrid();

    bool is_something_inserted = false;
    std::size_t inserted_until = 0;
    wxStaticBoxSizer* static_box = nullptr;
    std::unique_ptr<gui::LogRecordingBar> m_RecordingBar;

    /* Change detectors for the counter label. Were function-local statics
       in the tick - state shared by every instance of this panel, and by
       nobody visibly. */
    std::string m_LastShownSearchPattern;
    uint64_t m_LastShownTxCount = 0;
    uint64_t m_LastShownRxCount = 0;
    wxSpinCtrl* m_LogLevelCtrl = nullptr;

    size_t cnt = 0;
    std::string search_pattern;

    /* Handed in rather than fetched from wxGetApp() on every use.
       docs/code-style.md: "A class gets its collaborators through its
       constructor. It does not fetch them." */
    CanEntryHandler& m_handler;

    wxDECLARE_EVENT_TABLE();
};
