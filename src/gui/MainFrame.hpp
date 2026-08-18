#pragma once

#include "MainPanel.hpp"
#include "EscaperPanel.hpp"
#include "LogPanel.hpp"
#include "FilePanel.hpp"
#include "CanPanel/CanPanel.hpp"
#include "ModbusMasterPanel.hpp"
#include "CmdExecutorPanel.hpp"
#include "DidPanel.hpp"
#include "AlarmPanel.hpp"
#include "TimeTrackerPanel.hpp"
#include "AppNotification.hpp"

#include <wx/wx.h>
#include <wx/spinctrl.h>
#include <wx/aui/aui.h>
#include <wx/button.h>
#include <wx/combobox.h>
#include <wx/stc/stc.h>
#include <wx/filepicker.h>
#include <wx/progdlg.h>

#include <mutex>
#include <deque>

class TrayIcon;

class MyFrame : public wxFrame
{
public:
	MyFrame(const wxString& title);
	~MyFrame();

	void SetIconTooltip(const wxString& str);

	// !\brief Register terminal systemwide hotkey
	void RegisterTerminalHotkey(int vkey);

	// !\brief Toggles frame visibility
	void ToggleForegroundVisibility();

	// !\brief Set currently opened page
	void SetCurrentPage(uint8_t page_id);

	// Thread-safe entry point for application notifications.
	void PostNotification(AppNotification notification);

	MainPanel* main_panel = nullptr;
	EscaperPanel* escape_panel = nullptr;
	DebugPanel* debug_panel = nullptr;
	FilePanel* file_panel = nullptr;
	CmdExecutorPanelBase* cmd_panel = nullptr;
	CanPanel* can_panel = nullptr;
	ModbusMasterPanel* modbus_master_panel = nullptr;
	AlarmPanel* alarm_panel = nullptr;
	TimeTrackerPanel* timesheet_panel = nullptr;
	DidPanel* did_panel = nullptr;
	LogPanel* log_panel = nullptr;
	wxAuiNotebook* ctrl = nullptr;
	wxProgressDialog* backup_prog = NULL;
	std::atomic<bool> show_backup_dlg = false;
	bool is_initialized = false;

	wxDECLARE_EVENT_TABLE();
private:
	void OnHelp(wxCommandEvent& event);
	void OnAbout(wxCommandEvent& event);
	void OnQuit(wxCommandEvent& event);
	void OnCanLoadTxList(wxCommandEvent& event);
	void OnCanSaveTxList(wxCommandEvent& event);
	void OnCanLoadRxList(wxCommandEvent& event);
	void OnCanSaveRxList(wxCommandEvent& event);
	void OnCanLoadMapping(wxCommandEvent& event);
	void OnCanSaveMapping(wxCommandEvent& event);
	void OnCanSaveAll(wxCommandEvent& event);
	void OnSaveCmdExecutor(wxCommandEvent& event);
	void OnSaveBsecCache(wxCommandEvent& event);
	void OnSaveEverything(wxCommandEvent& event);
	void OnEditSettings(wxCommandEvent& event);
	void OnReloadSettings(wxCommandEvent& event);
	void OnClose(wxCloseEvent& event);
	void OnSize(wxSizeEvent& event);
	void OnKeyDown(wxKeyEvent& event);
	void OnHotkey(wxKeyEvent& evt);

	// !\brief Fast timer for frame
	void On10msTimer(wxTimerEvent& event);

	// !\brief Main timer for frame
	void On100msTimer(wxTimerEvent& event);

	// !\brief Handles debug panel related updates
	void HandleDebugPanelUpdate();

	// !\brief Handles backup progress dialog
	void HandleBackupProgressDialog();

	// !\brief Handles numlock to be always on
	void HandleAlwaysOnNumlock();		
	
	// !\brief Handles crypto price update
	void HandleCryptoPriceUpdate();

	// !\brief Handles DID panel update
	void HandleDidPanelUpdate();

private:
	// !\brief Handles notifications
	void HandleNotifications();
	void HandleNotification(const SimpleNotification& notification);
	void HandleNotification(const FileSavedNotification& notification);
	void HandleNotification(const PathSeparatorsReplacedNotification& notification);
	void HandleNotification(const BackupCompletedNotification& notification);
	void HandleNotification(const BackupFailedNotification& notification);
	void HandleNotification(const AlarmSetupNotification& notification);
	void HandleNotification(const AlarmTriggeredNotification& notification);
	void HandleNotification(const WorktimeToggledNotification& notification);

	// !\brief Show notification
	template<typename T> void ShowNotificaiton(const wxString& title, const wxString& message, int timeout, int flags, T&& fptr);

	// !\brief Application icon
	wxIcon applicationIcon;

	// !\brief Tray
	TrayIcon* tray = nullptr;

	// !\brief AUI manager for subwindows
	wxAuiManager m_mgr;

	// !\brief Fast main frame timer
	wxTimer* m_10msTimer = nullptr;
	
	// !\brief Main frame timer
	wxTimer* m_100msTimer = nullptr;

	std::mutex m_notificationMutex;
	std::deque<AppNotification> m_pendingNotifications;
};
