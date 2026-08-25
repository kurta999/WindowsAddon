#pragma once

#include <wx/taskbar.h>

class MyFrame;
class PrintScreenSaver;
class DirectoryBackup;
class Settings;

class MenuEventFilter : public wxEventFilter  /* without this event filter doing backups from tray is not possible due to dynamic menu ids */
{
public:
	MenuEventFilter()
	{
		wxEvtHandler::AddFilter(this);
	}
	virtual ~MenuEventFilter()
	{
		wxEvtHandler::RemoveFilter(this);
	}
	int FilterEvent(wxEvent& event) override;
};

class TrayIcon : public wxTaskBarIcon
{
public:
	TrayIcon(PrintScreenSaver& screenshots, DirectoryBackup& backups,
		Settings& settings) :
		m_Screenshots(screenshots), m_Backups(backups), m_Settings(settings)
	{
	}
	~TrayIcon(void);

	void SetMainFrame(MyFrame* frame);

	void OnLeftDoubleClick(wxTaskBarIconEvent& event);
	void OnOpenScreenshots(wxCommandEvent& event);
	void OnOpenRootFolder(wxCommandEvent& event);
	void OnReload(wxCommandEvent& event);
	void OnQuit(wxCommandEvent& event);
	virtual wxMenu* CreatePopupMenu();

	enum ID
	{
		Exit = 0,
		ReloadConfig,
		OpenScreenshots,
		OpenRootFolder,
		DoBackup = 1500,
		DoAlarm = 1600,
	};
	static int max_backups;
	static int max_alarms;
protected:
	MyFrame* mainFrame = nullptr;
private:
	PrintScreenSaver& m_Screenshots;
	DirectoryBackup& m_Backups;
	Settings& m_Settings;
	MenuEventFilter* filter = nullptr;

	DECLARE_EVENT_TABLE()
};