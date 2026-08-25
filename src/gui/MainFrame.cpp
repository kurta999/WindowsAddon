#include "pch.hpp"
#include "platform/KeyboardInput.hpp"

#include "SettingsDialog.hpp"

MyFrame::~MyFrame()
{
	if(auto* sensors = Sensors::TryGet(); sensors != nullptr && main_panel != nullptr)
		sensors->RemoveObserver(main_panel);

	/* Before the children go: the view the logger points at is one of them. */
	if(auto* logger = Logger::TryGet(); logger != nullptr && log_panel != nullptr)
		logger->SetLogHelper(nullptr);
	m_mgr.UnInit();  /* deinitialize the frame manager */
}
/* commitid.h is generated into src/, which only the CMake include path
   carries; MainFrameAccess.hpp sits next to this file and belongs outside the
   switch - leaving it inside broke the MSBuild build with C3861. */
#ifdef WINDOWSHELPER_CMAKE_BUILD
#include "commitid.h"
#else
#include "../commitid.h"
#endif

#include "MainFrameAccess.hpp"
#include <lodepng.h>

#define HOTKEY_ID_TERMINAL 0x3000		/* any value between 0 and 0xBFFF */
//#define HOTKEY_ID_NUM_LOCK 0x3001		/* any value between 0 and 0xBFFF */

wxBEGIN_EVENT_TABLE(MyFrame, wxFrame)
EVT_MENU(ID_Help, MyFrame::OnHelp)
EVT_MENU(ID_About, MyFrame::OnAbout)
EVT_MENU(ID_Quit, MyFrame::OnQuit)
EVT_MENU(ID_CmdExecutorSave, MyFrame::OnSaveCmdExecutor)
EVT_MENU(ID_BsecSaveCache, MyFrame::OnSaveBsecCache)
EVT_MENU(ID_SaveEverything, MyFrame::OnSaveEverything)
EVT_MENU(ID_EditSettings, MyFrame::OnEditSettings)
EVT_MENU(ID_ReloadSettings, MyFrame::OnReloadSettings)
EVT_SIZE(MyFrame::OnSize)
EVT_CLOSE(MyFrame::OnClose)
//EVT_CHAR_HOOK(MyFrame::OnKeyDown)
#ifdef _WIN32
EVT_HOTKEY(HOTKEY_ID_TERMINAL, MyFrame::OnHotkey)
#endif
//EVT_HOTKEY(HOTKEY_ID_NUM_LOCK, MyFrame::OnHotkey)  /* Numlock toggling doesn't work in this way */
wxEND_EVENT_TABLE()

void MyFrame::OnHelp(wxCommandEvent& event)
{
	wxMessageBox("This is a personal project for myself to improve my daily computer usage, particularly with programming and testing\n\
I've implemented things what I really needed to be more productive and accomplish things faster\n\
It's open source, because why not, maybe somebody will benefit from it one day.", "Help");
}

void MyFrame::OnAbout(wxCommandEvent& event)
{
	wxString platform = (sizeof(void*) == 4 ? " x86" : " x64");
	std::string wxwidgets_version = std::format("{}.{}.{}", wxMAJOR_VERSION, wxMINOR_VERSION, wxRELEASE_NUMBER);
	wxMessageBox(wxString("WindowsAddon") + platform + " v" + COMMIT_TAG + " (" + COMMIT_ID + ")" + "\n\n"
"MIT License\n\
\n\
Copyright (c) 2021 - 2025 Attila Kiss \"kurta999\" <nmsstulaj@gmail.com>\n\
\n\
Permission is hereby granted, free of charge, to any person obtaining a copy\n\
of this software and associated documentation files (the \"Software\"), to deal\n\
in the Software without restriction, including without limitation the rights\n\
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell\n\
copies of the Software, and to permit persons to whom the Software is\n\
furnished to do so, subject to the following conditions:\n\
\n\
The above copyright notice and this permission notice shall be included in all\n\
copies or substantial portions of the Software.\n\
\n\
THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR\n\
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,\n\
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE\n\
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER\n\
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,\n\
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE\n\
SOFTWARE." + "\n\nUsed 3rd party libraries:\n"
"SQLite: " + SQLITE_VERSION + "\n" +
"wxWidgets: " + wxwidgets_version + "\n" +
"boost: " + BOOST_LIB_VERSION + "\n" +
"lodepng: " + LODEPNG_VERSION_STRING + "\n" +
"HIDAPI: " + HID_API_VERSION_STR + "\n" +
//"opencv: " + CV_VERSION + "\n" +
"Build info:\n" +
"Compiler: " + BOOST_COMPILER + "\n"
"Built on: " + __TIMESTAMP__, "OK");
}

void MyFrame::OnQuit(wxCommandEvent& event)
{
	wxExit();
}

void MyFrame::OnClose(wxCloseEvent& event)
{
	if(m_Settings.minimize_on_exit)
		Hide();
	else
		wxExit();
}

void MyFrame::OnKeyDown(wxKeyEvent& event)
{
	LOG(LogLevel::Notification, "OnKeyDown");
}

void MyFrame::OnHotkey(wxKeyEvent& evt)
{
	if(evt.GetId() == HOTKEY_ID_TERMINAL)
	{
		TerminalHotkey::Get()->Process();
	}
}

void MyFrame::OnSize(wxSizeEvent& event)
{
	/* Was twenty-two hand-written SetSize lines naming every panel - and
	   reaching two levels into the notebook ones - which meant a new page had
	   to be added here too, and one (Backups) never was. CreatePage registers
	   each page as it is built; a panel that needs more than SetSize
	   implements OnFrameResized. The old code was also guarded on main_panel,
	   so disabling the Main page silently stopped every other page from
	   resizing; the guard is now "any page exists". */
	const wxSize size = event.GetSize();
	if(!m_resizeTargets.empty())
	{
		ctrl->Freeze();
		ctrl->SetSize(size);
		ctrl->Thaw();

		for(const auto& resize : m_resizeTargets)
			resize(size);
	}
	event.Skip(true);
}


void MyFrame::OnSaveCmdExecutor(wxCommandEvent& event)
{
	std::chrono::steady_clock::time_point t1 = std::chrono::steady_clock::now();
	std::unique_ptr<CmdExecutor>& cmd = wxGetApp().cmd_executor;
	bool ret = cmd->Save();
	if(ret)
	{
		std::chrono::steady_clock::time_point t2 = std::chrono::steady_clock::now();
		int64_t dif = std::chrono::duration_cast<std::chrono::nanoseconds>(t2 - t1).count();

		char work_dir[1024] = {};
#ifdef _WIN32
		GetCurrentDirectoryA(sizeof(work_dir) - 1, work_dir);
#endif
		PostAppNotification(FileSavedNotification{SavedFileKind::Commands, dif,
			std::string(work_dir) + "\\Cmds.xml"});
	}
}

void MyFrame::OnSaveBsecCache(wxCommandEvent& event)
{
	std::chrono::steady_clock::time_point t1 = std::chrono::steady_clock::now();
	std::unique_ptr<CmdExecutor>& cmd = wxGetApp().cmd_executor;
	bool ret = cmd->Save();
}

void MyFrame::OnSaveEverything(wxCommandEvent& event)
{
	m_Settings.SaveFile(false);
	std::unique_ptr<CanEntryHandler>& can_handler = wxGetApp().can_entry;

	if(can_handler)
	{
		std::filesystem::path path = "TxList.xml";
		can_handler->SaveTxList(path);
		path = "RxList.xml";
		can_handler->SaveRxList(path);
		path = "FrameMapping.xml";
		can_handler->SaveMapping(path);
	}

	std::unique_ptr<CmdExecutor>& cmd_executor = wxGetApp().cmd_executor;
	if(cmd_executor)
		cmd_executor->Save();

	PostAppNotification(SimpleNotification{SimpleNotificationKind::EverythingSaved});
}

void MyFrame::OnEditSettings(wxCommandEvent& WXUNUSED(event))
{
	SettingsDialog dialog(this);
	if(!dialog.IsReady() || dialog.ShowModal() != wxID_OK)
		return;

	m_Settings.LoadFile();
	SetCurrentPage(m_Settings.default_page);
	PostNotification(SimpleNotification{SimpleNotificationKind::SettingsSaved});
}

void MyFrame::OnReloadSettings(wxCommandEvent& WXUNUSED(event))
{
	m_Settings.LoadFile();
	SetCurrentPage(m_Settings.default_page);
	SetStatusText("Settings reloaded from settings.ini");
	LOG(LogLevel::Normal, "Settings reloaded from settings.ini");
}

void MyFrame::On10msTimer(wxTimerEvent& event)
{
	m_tick10ms.TickAll();
}

void MyFrame::On100msTimer(wxTimerEvent& event)
{
	m_tick100ms.TickAll();
}

// !\brief What the frame drives that ran before the panels did.
//
// Panels register themselves in CreatePage, so the frame's own work is split
// around that call to keep the original tick order: numlock and the 100 ms
// housekeeping ran before every panel, and only Logger::Tick ran after.
void MyFrame::RegisterLeadingFrameTicks()
{
	m_tick10ms.Register([this] { HandleAlwaysOnNumlock(); });

	m_tick100ms.Register([this] { HandleDebugPanelUpdate(); });
	m_tick100ms.Register([this] { HandleNotifications(); });
	m_tick100ms.Register([this] { backup_progress.Tick(); });
	m_tick100ms.Register([this] { HandleCryptoPriceUpdate(); });
}

// !\brief What the frame drives after every panel has ticked.
void MyFrame::RegisterTrailingFrameTicks()
{
	m_tick10ms.Register([] { Logger::Get()->Tick(); });
}

void MyFrame::HandleDebugPanelUpdate()
{
	int sel = ctrl->GetSelection();
#ifdef _WIN32
	HWND foreground = GetForegroundWindow();
#else
	bool foreground = true;
#endif
	if((sel == ctrl->FindPage(debug_panel)) && foreground)
	{
		if(debug_panel)
			debug_panel->HandleUpdate();
	}
}

void MyFrame::HandleAlwaysOnNumlock()
{
#ifdef _WIN32
	/* The frame decides (a setting and a wx key-state read); pressing the key
	   is the desktop's business. WXK_NUMLOCK asserts on linux, hence the
	   guard around the whole decision. */
	if(m_Settings.always_on_numlock && !wxGetKeyState(WXK_NUMLOCK))
		platform::TapNumlock();
#endif
}

void MyFrame::HandleCryptoPriceUpdate()
{
	/* The page that shows the prices asks for them. The frame used to fetch
	   all four and hand them over, which meant it knew the shape of a coin
	   price as well as owning the clock. */
	if(main_panel)
		main_panel->RefreshCryptoPrices();
}

void MyFrame::RegisterTerminalHotkey(int vkey)
{
#ifdef _WIN32
	wxWindow::UnregisterHotKey(HOTKEY_ID_TERMINAL);
	if(vkey != 0xFFFF)  /* Register hotkey only if specified key is valid */
	{
		bool ret = wxWindow::RegisterHotKey(HOTKEY_ID_TERMINAL, wxMOD_NONE, vkey);
		if(!ret)
			LOG(LogLevel::Error, "Failed to register terminal hotkey!");
	}
#endif
}

void MyFrame::ToggleForegroundVisibility()
{
	bool is_iconized = IsIconized();
	bool is_shown = IsShown();
	if(is_iconized)
	{
		Iconize(false);
		SetFocus();
		Raise();
		Show(true);
	}
	else
	{
		Show(!is_shown);
	}

	if(is_shown)
		Raise();
}

void MyFrame::SetCurrentPage(uint8_t page_id)
{
	if(page_id > ctrl->GetPageCount() - 1)
		page_id = ctrl->GetPageCount() - 1;

	ctrl->SetSelection(page_id);
}

void MyFrame::SetIconTooltip(const wxString &str)
{
#ifdef _WIN32
	if(!tray->SetIcon(wxIcon(wxT("aaaa")), str))
	{
		LOG(LogLevel::Error, "Could not set tray icon.");
	}
	SetIcon(wxICON(aaaa));
#else

#endif
}

MyFrame::MyFrame(const wxString& title, PrintScreenSaver& screenshots,
	DirectoryBackup& backups, Settings& settings)
	: wxFrame(NULL, wxID_ANY, title), m_Screenshots(screenshots),
	  m_Backups(backups), m_Settings(settings), backup_progress(this, backups)
{
	tray = new TrayIcon(m_Screenshots, m_Backups, m_Settings);
	tray->SetMainFrame(this);
	SetIconTooltip(wxT("No measurements"));
	
	m_mgr.SetManagedWindow(this);
	SetMinSize(wxSize(800, 600));

	m_mgr.SetFlags(wxAUI_MGR_ALLOW_FLOATING);
	wxMenu* menuFile = new wxMenu;
	menuFile->Append(ID_Quit, "E&xit\tCtrl-E", "Close program")->SetBitmap(wxArtProvider::GetBitmap(wxART_QUIT, wxART_OTHER, FromDIP(wxSize(16, 16))));
	menuFile->Append(wxID_OPEN, "&Open file\tCtrl-O", "Open file")->SetBitmap(wxArtProvider::GetBitmap(wxART_FILE_OPEN, wxART_OTHER, FromDIP(wxSize(16, 16))));
	menuFile->Append(wxID_SAVE, "&Save file\tCtrl-S", "Save file")->SetBitmap(wxArtProvider::GetBitmap(wxART_FILE_SAVE, wxART_OTHER, FromDIP(wxSize(16, 16))));
	menuFile->Append(wxID_SAVEAS, "&Save file As\tCtrl-S", "Save file As other")->SetBitmap(wxArtProvider::GetBitmap(wxART_FILE_SAVE_AS, wxART_OTHER, FromDIP(wxSize(16, 16))));
	menuFile->Append(ID_DestroyAll, "&Destroy all widgets\tCtrl-W", "Destroy all widgets")->SetBitmap(wxArtProvider::GetBitmap(wxART_GO_HOME, wxART_OTHER, FromDIP(wxSize(16, 16))));
	wxMenu* menuCan = new wxMenu;
	menuCan->Append(ID_CanLoadTxList, "&Load TX List", "Load CAN TX List")->SetBitmap(wxArtProvider::GetBitmap(wxART_FILE_OPEN, wxART_OTHER, FromDIP(wxSize(16, 16))));
	menuCan->Append(ID_CanSaveTxList, "&Save TX List", "Save CAN TX List")->SetBitmap(wxArtProvider::GetBitmap(wxART_FILE_SAVE, wxART_OTHER, FromDIP(wxSize(16, 16))));
	menuCan->Append(ID_CanLoadRxList, "&Load RX List", "Load CAN RX List")->SetBitmap(wxArtProvider::GetBitmap(wxART_FILE_OPEN, wxART_OTHER, FromDIP(wxSize(16, 16))));
	menuCan->Append(ID_CanSaveRxList, "&Save RX List", "Save CAN RX List")->SetBitmap(wxArtProvider::GetBitmap(wxART_FILE_SAVE, wxART_OTHER, FromDIP(wxSize(16, 16))));
	menuCan->Append(ID_CanLoadMapping, "&Load CAN mapping", "Load CAN mapping")->SetBitmap(wxArtProvider::GetBitmap(wxART_FILE_SAVE, wxART_OTHER, FromDIP(wxSize(16, 16))));
	menuCan->Append(ID_CanSaveMapping, "&Save CAN mapping", "Save CAN mapping")->SetBitmap(wxArtProvider::GetBitmap(wxART_FILE_SAVE, wxART_OTHER, FromDIP(wxSize(16, 16))));

	/* Six menu items, one page, nothing of the frame's in between. These
	   were six named one-line forwarders plus six event-table rows and six
	   declarations. */
	const auto bind_can_menu = [this](int id, void (CanPanel::*action)())
	{
		Bind(wxEVT_MENU, [this, action](wxCommandEvent&)
		{
			if(can_panel)
				(can_panel->*action)();
		}, id);
	};
	bind_can_menu(ID_CanLoadTxList, &CanPanel::LoadTxList);
	bind_can_menu(ID_CanSaveTxList, &CanPanel::SaveTxList);
	bind_can_menu(ID_CanLoadRxList, &CanPanel::LoadRxList);
	bind_can_menu(ID_CanSaveRxList, &CanPanel::SaveRxList);
	bind_can_menu(ID_CanLoadMapping, &CanPanel::LoadMapping);
	bind_can_menu(ID_CanSaveMapping, &CanPanel::SaveMapping);
	menuCan->Append(ID_CanSaveAll, "&Save all CAN", "Save TX,RX List & CAN mapping")->SetBitmap(wxArtProvider::GetBitmap(wxART_FILE_SAVE, wxART_OTHER, FromDIP(wxSize(16, 16))));
	menuCan->Append(ID_CmdExecutorSave, "&Save CMDs", "Save commands from CMD Executor")->SetBitmap(wxArtProvider::GetBitmap(wxART_FILE_SAVE, wxART_OTHER, FromDIP(wxSize(16, 16))));
	menuCan->Append(ID_BsecSaveCache, "&Save BSEC", "Save BSEC Cache")->SetBitmap(wxArtProvider::GetBitmap(wxART_FILE_SAVE, wxART_OTHER, FromDIP(wxSize(16, 16))));
	menuCan->Append(ID_SaveEverything, "&Save everything", "Save everything (CAN, CmdExecutor, Settings, etc)")->SetBitmap(wxArtProvider::GetBitmap(wxART_FILE_SAVE, wxART_OTHER, FromDIP(wxSize(16, 16))));
	wxMenu* menuSettings = new wxMenu;
	menuSettings->Append(ID_EditSettings, "&Edit settings...\tCtrl-,", "Edit values from settings.ini")->SetBitmap(wxArtProvider::GetBitmap(wxART_HELP_SETTINGS, wxART_OTHER, FromDIP(wxSize(16, 16))));
	menuSettings->Append(ID_ReloadSettings, "&Reload from settings.ini", "Discard runtime setting changes and reload settings.ini")->SetBitmap(wxArtProvider::GetBitmap(wxART_REDO, wxART_OTHER, FromDIP(wxSize(16, 16))));
	wxMenu* menuHelp = new wxMenu;
	menuHelp->Append(ID_About, "&About", "Read license")->SetBitmap(wxArtProvider::GetBitmap(wxART_HELP_PAGE, wxART_OTHER, FromDIP(wxSize(16, 16))));
	menuHelp->Append(ID_Help, "&Read help\tCtrl-H", "Read description about this program")->SetBitmap(wxArtProvider::GetBitmap(wxART_HELP, wxART_OTHER, FromDIP(wxSize(16, 16))));
	wxMenuBar* menuBar = new wxMenuBar;
	menuBar->Append(menuFile, "&File");
	menuBar->Append(menuCan, "&Edit");
	menuBar->Append(menuSettings, "&Settings");
	menuBar->Append(menuHelp, "&Help");
	SetMenuBar(menuBar);

	CreateStatusBar();
	wxString platform = (sizeof(void*) == 4 ? "x86" : "x64");
#ifdef DEBUG
	SetStatusText("WindowsAddon " + platform + " v" + COMMIT_TAG + " DEBUG BUILD " + COMMIT_ID);
#else
	SetStatusText("WindowsAddon " + platform + " v" + COMMIT_TAG);
#endif

	SetClientSize(wxSize(m_Settings.window_size.width, m_Settings.window_size.height));

	/* One line per page: construct it, remember its notebook entry, and pick up
	   whichever timer ticks the panel declares. */
	RegisterLeadingFrameTicks();

	UsedPages used_pages = m_Settings.used_pages;

	/* Three pages own a notebook, and each sized it from the configured window
	   less fifty pixels, spelling the same expression out. Once, here. */
	const wxSize notebook_size(m_Settings.window_size.width - 50, m_Settings.window_size.height - 50);

	/* Both wanted twice below: once for the main page's ports and once for
	   the page or observer that follows it. */
	CustomMacro* macros = CustomMacro::Get();
	Sensors* sensors = Sensors::Get();

	/* One lookup for the whole list. Nine of them stood here, and the moment a
	   page needed two services the line asked twice. */
	MyApp& app = wxGetApp();
	/* Named rather than positional: thirteen references in a row is a list
	   nobody can check by eye, and two of the same type in the wrong order
	   would compile. */
	main_panel          = CreatePage<MainPanel>(used_pages.main(), "Main Page", wxART_GO_HOME, this,
		MainPanelPorts{
			.crypto_price = *app.crypto_price,
			.path_separator = *app.path_separator,
			.screenshots = m_Screenshots,
			.corsair_hid = *app.corsair_hid,
			.can_port = *app.can_port,
			.sensors = *sensors,
			.database = *DatabaseLogic::Get(),
			.bsec = *BsecHandler::Get(),
			.settings = m_Settings,
			.server = *Server::Get(),
			.keyboard_port = *SerialPort::Get(),
			.macros = *macros,
		});
	escape_panel        = CreatePage<EscaperPanel>(used_pages.escaper(), "C StrEscape", wxART_LIST_VIEW, this);
	debug_panel         = CreatePage<DebugPanel>(used_pages.debug(), "Debug Page", wxART_HELP, this,
		m_Screenshots, *app.idle_power_saver, *macros);
	file_panel          = CreatePage<FilePanel>(used_pages.file_browser(), "File Browser", wxART_FILE_OPEN, this);
	cmd_panel           = CreatePage<CmdExecutorPanelBase>(used_pages.cmd_executor(), "CMD Executor", wxART_FILE_OPEN, this, *app.cmd_executor, notebook_size);
	can_panel           = CreatePage<CanPanel>(used_pages.can(), "CAN Sender", wxART_REMOVABLE, this, *app.can_entry, *app.can_port, notebook_size);
	did_panel           = CreatePage<DidPanel>(used_pages.did(), "DID", wxART_FIND, this, *app.can_entry, *app.did_handler);
	modbus_master_panel = CreatePage<ModbusMasterPanel>(used_pages.modbus_master(), "ModbusMaster", wxART_PRINT, this, *app.modbus_handler, notebook_size);
	alarm_panel         = CreatePage<AlarmPanel>(used_pages.alarm_panel(), "AlarmPanel", wxART_TICK_MARK, this, *app.alarm_entry);
	timesheet_panel     = CreatePage<TimeTrackerPanel>(used_pages.time_tracker(), "Timesheet", wxART_PLUS, this,
		*app.time_tracker, *app.working_days);
	backup_panel        = CreatePage<BackupPanel>(used_pages.backup(), "Backups", wxART_HARDDISK, this, m_Backups);
	Logger* logger = Logger::Get();
	log_panel           = CreatePage<LogPanel>(used_pages.log(), "Log", wxART_TIP, this, *logger);

	/* The frame wires the view to the logger. The panel used to register
	   itself from its own constructor, which inverted ILogHelper: the port
	   exists so the logger does not have to know what a panel is. */
	if(log_panel)
		logger->SetLogHelper(log_panel);

	if(main_panel)
		sensors->AddObserver(main_panel);

	RegisterTrailingFrameTicks();
	logger->AppendPreinitedEntries();
	
	wxSize client_size = GetClientSize();
	ctrl = new wxAuiNotebook(this, wxID_ANY, wxPoint(client_size.x, client_size.y), FromDIP(wxSize(430, 200)), wxAUI_NB_TOP | wxAUI_NB_TAB_SPLIT | wxAUI_NB_TAB_MOVE | wxAUI_NB_SCROLL_BUTTONS | wxAUI_NB_MIDDLE_CLICK_CLOSE | wxAUI_NB_TAB_EXTERNAL_MOVE | wxNO_BORDER);
	ctrl->Freeze();
	for(const PendingPage& page : m_pendingPages)
	{
		ctrl->AddPage(page.panel, page.title, false,
			wxArtProvider::GetBitmap(page.icon, wxART_OTHER, FromDIP(wxSize(16, 16))));
	}
	m_pendingPages.clear();
	ctrl->Thaw();

	SetCurrentPage(m_Settings.default_page);
	Show(!m_Settings.minimize_on_startup);

	m_10msTimer = new wxTimer(this, ID_10msTimer);
	Connect(m_10msTimer->GetId(), wxEVT_TIMER, wxTimerEventHandler(MyFrame::On10msTimer), NULL, this);
	m_10msTimer->Start(100, false);	
	m_100msTimer = new wxTimer(this, ID_100msTimer);
	Connect(m_100msTimer->GetId(), wxEVT_TIMER, wxTimerEventHandler(MyFrame::On100msTimer), NULL, this);
	m_100msTimer->Start(100, false);

	//wxWindow::RegisterHotKey(HOTKEY_ID_NUM_LOCK, wxMOD_NONE, VK_NUMLOCK);
	//SetClientSize(800, 600);

	is_initialized = true;
}

void MyFrame::PostNotification(AppNotification notification)
{
	std::scoped_lock lock(m_notificationMutex);
	m_pendingNotifications.push_back(std::move(notification));
}

void MyFrame::HandleNotifications()
{
	std::optional<AppNotification> notification;
	{
		std::scoped_lock lock(m_notificationMutex);
		if(m_pendingNotifications.empty())
			return;
		notification = std::move(m_pendingNotifications.front());
		m_pendingNotifications.pop_front();
	}

	m_NotificationPresenter.Present(*notification);
}
