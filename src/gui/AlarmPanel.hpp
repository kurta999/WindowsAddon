#pragma once

#include <wx/wx.h>
#include <semaphore>
#include <atomic>

class AlarmEntryHandler;

class AlarmPanel : public wxPanel
{
public:
	AlarmPanel(wxFrame* parent, AlarmEntryHandler& handler);

	void ShowAlarmDialog();
	void On10MsTimer();
	void WaitForAlarmSemaphore();
	const std::string& GetAlarmTime() const;

private:
	void ShowAlarmDialogInternal();
	void UpdateAlarmsDisplay();

	void OnRightClick(wxMouseEvent& event);

	std::binary_semaphore m_alarmSemaphore{0};
	std::atomic<bool> m_showAlarmDialog{false};
	std::string m_DurationText;

	std::vector<wxStaticText*> alarms;

	/* Handed in rather than fetched from wxGetApp() on every use.
	   docs/code-style.md: "A class gets its collaborators through its
	   constructor. It does not fetch them." */
	AlarmEntryHandler& m_handler;

	wxDECLARE_EVENT_TABLE();
};
