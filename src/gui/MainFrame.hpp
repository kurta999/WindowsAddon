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
#include "gui/NotificationPresenter.hpp"
#include "BackupProgressDialog.hpp"
#include "TickRegistry.hpp"
#include "interface/INotificationSink.hpp"

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

class MyFrame : public wxFrame, public INotificationSink
{
public:
	MyFrame(const wxString& title, PrintScreenSaver& screenshots,
		DirectoryBackup& backups, Settings& settings);
	~MyFrame();

	void SetIconTooltip(const wxString& str);

	// !\brief Register terminal systemwide hotkey
	void RegisterTerminalHotkey(int vkey);

	// !\brief Toggles frame visibility
	void ToggleForegroundVisibility();

	// !\brief Set currently opened page
	void SetCurrentPage(uint8_t page_id);

	// Thread-safe entry point for application notifications.
	void PostNotification(AppNotification notification) override;

	MainPanel* main_panel = nullptr;
	BackupPanel* backup_panel = nullptr;

	/* Handed in by the composition root. The tray menu and the keyboard map
	   used to look these up for themselves. */
	PrintScreenSaver& m_Screenshots;
	DirectoryBackup& m_Backups;
	Settings& m_Settings;
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
	// !\brief The backup progress dialog, asked for from the backup worker.
	gui::BackupProgressDialog backup_progress;
	bool is_initialized = false;

	wxDECLARE_EVENT_TABLE();
private:
	void OnHelp(wxCommandEvent& event);
	void OnAbout(wxCommandEvent& event);
	void OnQuit(wxCommandEvent& event);
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
	// !\brief Construct one notebook page, add it, and register whatever timer
	// ticks its type declares.
	//
	// Every panel used to be named in three places - constructed, added to the
	// notebook, and ticked - so adding a feature meant three edits and omitting
	// the third produced a panel that simply never updated. `if constexpr` picks
	// up On10MsTimer/On100msTimer when the panel has one, so the tick list can
	// no longer disagree with the page list.
	template <typename PanelT, typename... Args>
	PanelT* CreatePage(bool enabled, const char* title, const wxArtID& icon, Args&&... args)
	{
		if(!enabled)
			return nullptr;

		PanelT* panel = new PanelT(std::forward<Args>(args)...);
		m_pendingPages.push_back({ panel, title, icon });

		if constexpr(requires(PanelT& p) { p.On10MsTimer(); })
			m_tick10ms.Register([panel] { panel->On10MsTimer(); });
		if constexpr(requires(PanelT& p) { p.On100msTimer(); })
			m_tick100ms.Register([panel] { panel->On100msTimer(); });

		/* Same move as the two ticks above: OnSize used to hard-code the panel
		   inventory - two levels deep for the notebook panels - and had already
		   forgotten one page (Backups never resized with the frame). A page
		   that needs more than SetSize implements OnFrameResized. */
		if constexpr(requires(PanelT& p, const wxSize& s) { p.OnFrameResized(s); })
			m_resizeTargets.push_back([panel](const wxSize& s) { panel->OnFrameResized(s); });
		else
			m_resizeTargets.push_back([panel](const wxSize& s) { panel->SetSize(s); });

		return panel;
	}

	/* The frame's own ticks, split around CreatePage so the order the two
	   timer handlers used to spell out is preserved exactly. */
	void RegisterLeadingFrameTicks();
	void RegisterTrailingFrameTicks();

	struct PendingPage
	{
		wxWindow* panel = nullptr;
		const char* title = nullptr;
		wxArtID icon;
	};
	std::vector<PendingPage> m_pendingPages;
	gui::TickRegistry m_tick10ms;
	gui::TickRegistry m_tick100ms;

	void On10msTimer(wxTimerEvent& event);

	// !\brief Main timer for frame
	void On100msTimer(wxTimerEvent& event);

	// !\brief Handles debug panel related updates
	void HandleDebugPanelUpdate();

	// !\brief Handles numlock to be always on
	void HandleAlwaysOnNumlock();		
	
	// !\brief Handles crypto price update
	void HandleCryptoPriceUpdate();

private:
	// !\brief Handles notifications
	void HandleNotifications();

	// !\brief Show notification

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

	gui::NotificationPresenter m_NotificationPresenter{ this };
	// !\brief One entry per created page, run by OnSize in creation order.
	std::vector<std::function<void(const wxSize&)>> m_resizeTargets;

	std::mutex m_notificationMutex;
	std::deque<AppNotification> m_pendingNotifications;
};
