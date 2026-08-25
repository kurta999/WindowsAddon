#pragma once

#include <wx/wx.h>

class PrintScreenSaver;
class IdlePowerSaver;
class IKeySink;

class DebugPanel : public wxPanel
{
public:
	DebugPanel(wxFrame* parent, PrintScreenSaver& screenshots, IdlePowerSaver& power_saver,
		IKeySink& keys);
	
	void HandleUpdate();

	wxStaticText* m_MousePos = nullptr;
	wxStaticText* m_ActiveWindowTitle = nullptr;
	wxButton* m_SaveScreenshot = nullptr;
	wxButton* m_PathSeparatorReplace = nullptr;

	wxTextCtrl* m_KeyTopress = nullptr;
	wxButton* m_SimulateKeypress = nullptr;

	wxTextCtrl* m_TextPanel = nullptr;

	wxSpinCtrl* m_CpuMinPowerPercent = nullptr;
	wxSpinCtrl* m_CpuMaxPowerPercent = nullptr;
	wxButton* m_CpuPowerRefresh = nullptr;
	wxButton* m_CpuPowerApply = nullptr;
	wxButton* m_TestCrashHandler = nullptr;
	wxTextCtrl* m_HourMinSec = nullptr;
	wxButton* m_HourConvert = nullptr;

private:
	/* Held rather than captured: the button's handler outlives the
	   constructor, so a reference to its parameter would dangle. */
	PrintScreenSaver& m_Screenshots;
	IdlePowerSaver& m_PowerSaver;
	IKeySink& m_Keys;

	std::future<void> keypress_future;
	wxDECLARE_EVENT_TABLE();
};
