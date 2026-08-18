#include "pch.hpp"

#include "SettingsDialog.hpp"

MyFrame::~MyFrame()
{
	if(auto* sensors = Sensors::TryGet(); sensors != nullptr && main_panel != nullptr)
		sensors->RemoveObserver(main_panel);
	m_mgr.UnInit();  /* deinitialize the frame manager */
}
#ifdef WINDOWSHELPER_CMAKE_BUILD
#include "commitid.h"
#else
#include "../commitid.h"
#endif

#define HOTKEY_ID_TERMINAL 0x3000		/* any value between 0 and 0xBFFF */
//#define HOTKEY_ID_NUM_LOCK 0x3001		/* any value between 0 and 0xBFFF */

wxBEGIN_EVENT_TABLE(MyFrame, wxFrame)
EVT_MENU(ID_Help, MyFrame::OnHelp)
EVT_MENU(ID_About, MyFrame::OnAbout)
EVT_MENU(ID_Quit, MyFrame::OnQuit)
EVT_MENU(ID_CanLoadTxList, MyFrame::OnCanLoadTxList)
EVT_MENU(ID_CanSaveTxList, MyFrame::OnCanSaveTxList)
EVT_MENU(ID_CanLoadRxList, MyFrame::OnCanLoadRxList)
EVT_MENU(ID_CanSaveRxList, MyFrame::OnCanSaveRxList)
EVT_MENU(ID_CanLoadMapping, MyFrame::OnCanLoadMapping)
EVT_MENU(ID_CanSaveMapping, MyFrame::OnCanSaveMapping)
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
	wxMessageBox(wxString("WindowsHelper") + platform + " v" + COMMIT_TAG + " (" + COMMIT_ID + ")" + "\n\n"
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
	if(Settings::Get()->minimize_on_exit)
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
	/*
	else if(evt.GetId() == HOTKEY_ID_NUM_LOCK)
	{
		if(!wxGetKeyState(WXK_NUMLOCK))
		{
			int flags = evt.GetRawKeyFlags();
#ifdef _WIN32
			INPUT input = { 0 };
			input.type = INPUT_KEYBOARD;
			input.ki.wVk = VK_NUMLOCK;
			input.ki.dwFlags = 0;
			SendInput(1, &input, sizeof(input));
			input.ki.dwFlags = KEYEVENTF_KEYUP;
			SendInput(1, &input, sizeof(input));
#endif
		}
	}
	*/
}

void MyFrame::OnSize(wxSizeEvent& event)
{
	wxSize a = event.GetSize();
	if(main_panel)
	{
		ctrl->Freeze();
		ctrl->SetSize(a);
		ctrl->Thaw();

		if(main_panel)
			main_panel->SetSize(a);
		if(escape_panel)
			escape_panel->SetSize(a);
		if(debug_panel)
			debug_panel->SetSize(a);
		if(log_panel)
			log_panel->SetSize(a);
		if(file_panel)
			file_panel->SetSize(a);
		if(can_panel)
		{
			can_panel->SetSize(a);
			if(can_panel->m_notebook)
				can_panel->m_notebook->SetSize(a);
			if(can_panel->sender)
				can_panel->sender->SetSize(a);
			if(can_panel->log)
				can_panel->log->SetSize(a);
			if(can_panel->script)
				can_panel->script->SetSize(a);
			can_panel->m_notebook->Layout();
		}
		if (modbus_master_panel)
		{
			modbus_master_panel->SetSize(a);
			if (modbus_master_panel->m_notebook)
				modbus_master_panel->m_notebook->SetSize(a);
		}
		if(alarm_panel)
			alarm_panel->SetSize(a);
		if(timesheet_panel)
			timesheet_panel->SetSize(a);
		if(cmd_panel)
			cmd_panel->SetSize(a);
		if(did_panel)
			did_panel->SetSize(a);
	}
	event.Skip(true);
}


void MyFrame::OnCanLoadTxList(wxCommandEvent& event)
{
	if(can_panel)
		can_panel->LoadTxList();
}

void MyFrame::OnCanSaveTxList(wxCommandEvent& event)
{
	if(can_panel)
		can_panel->SaveTxList();
}

void MyFrame::OnCanLoadRxList(wxCommandEvent& event)
{
	if(can_panel)
		can_panel->LoadRxList();
}

void MyFrame::OnCanSaveRxList(wxCommandEvent& event)
{
	if(can_panel)
		can_panel->SaveRxList();
}

void MyFrame::OnCanLoadMapping(wxCommandEvent& event)
{
	if(can_panel)
		can_panel->LoadMapping();
}

void MyFrame::OnCanSaveMapping(wxCommandEvent& event)
{
	if(can_panel)
		can_panel->SaveMapping();
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
		MyFrame* frame = ((MyFrame*)(wxGetApp().GetTopWindow()));
		frame->PostNotification(FileSavedNotification{SavedFileKind::Commands, dif,
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
	Settings::Get()->SaveFile(false);
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

	MyFrame* frame = ((MyFrame*)(wxGetApp().GetTopWindow()));
	frame->PostNotification(SimpleNotification{SimpleNotificationKind::EverythingSaved});
}

void MyFrame::OnEditSettings(wxCommandEvent& WXUNUSED(event))
{
	SettingsDialog dialog(this);
	if(!dialog.IsReady() || dialog.ShowModal() != wxID_OK)
		return;

	Settings::Get()->LoadFile();
	SetCurrentPage(Settings::Get()->default_page);
	PostNotification(SimpleNotification{SimpleNotificationKind::SettingsSaved});
}

void MyFrame::OnReloadSettings(wxCommandEvent& WXUNUSED(event))
{
	Settings::Get()->LoadFile();
	SetCurrentPage(Settings::Get()->default_page);
	SetStatusText("Settings reloaded from settings.ini");
	LOG(LogLevel::Normal, "Settings reloaded from settings.ini");
}

void MyFrame::On10msTimer(wxTimerEvent& event)
{
	HandleAlwaysOnNumlock();
	if(can_panel)
		can_panel->On10MsTimer();
	if(modbus_master_panel)
		modbus_master_panel->On10MsTimer();
	if(alarm_panel)
		alarm_panel->On10MsTimer();	
	if(timesheet_panel)
		timesheet_panel->On10MsTimer();

	Logger::Get()->Tick();
}

void MyFrame::On100msTimer(wxTimerEvent& event)
{
	HandleDebugPanelUpdate();
	HandleNotifications();
	HandleBackupProgressDialog();
	HandleCryptoPriceUpdate();
	HandleDidPanelUpdate();
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

void MyFrame::HandleBackupProgressDialog()
{
	if(show_backup_dlg && backup_prog == NULL && !DirectoryBackup::Get()->IsCancelled())
	{
		backup_prog = new wxProgressDialog("Backing up files", 
			"Please wait while files being backed up\nIt can take a few minutes...Be patient", 100, 0, wxPD_CAN_ABORT | wxPD_ELAPSED_TIME | wxPD_SMOOTH);
		backup_prog->Show();
	}

	if(backup_prog != NULL)
	{
		try
		{
			std::string current_file = DirectoryBackup::Get()->GetCurrentFile();
			if(!current_file.empty())
				backup_prog->Pulse(wxString::Format("Please wait while files being backed up\nIt can take a few minutes...Be patient\nCurrent file: %s", current_file));
			if(backup_prog && backup_prog->WasCancelled())
			{
				backup_prog->Destroy();
				backup_prog = NULL;
				DirectoryBackup::Get()->RequestCancel();
			}
		}
		catch(const std::exception& e)
		{
			LOG(LogLevel::Error, "Exception: {}", e.what());
		}
	}

	if(!show_backup_dlg && backup_prog != NULL)
	{
		show_backup_dlg = false;
		backup_prog->Destroy();
		backup_prog = NULL;
	}
}

void MyFrame::HandleAlwaysOnNumlock()
{
#ifdef _WIN32
	if(Settings::Get()->always_on_numlock && !wxGetKeyState(WXK_NUMLOCK))  /* WXK_NUMLOCK causes assert failure on linux */
	{
		INPUT input = { 0 };
		input.type = INPUT_KEYBOARD;
		input.ki.wVk = VK_NUMLOCK;
		input.ki.dwFlags = 0;
		SendInput(1, &input, sizeof(input));
		input.ki.dwFlags = KEYEVENTF_KEYUP;
		SendInput(1, &input, sizeof(input));
	}
#endif
}

void MyFrame::HandleCryptoPriceUpdate()
{
	CryptoPrice::Get()->UpdatePrices();
	if(CryptoPrice::Get()->ConsumePending())
	{
		if(main_panel)
			main_panel->UpdateCryptoPrices(CryptoPrice::Get()->GetEthBuy(), CryptoPrice::Get()->GetEthSell(), CryptoPrice::Get()->GetBtcBuy(), CryptoPrice::Get()->GetBtcSell());
	}
}

void MyFrame::HandleDidPanelUpdate()
{
	if(did_panel)
		did_panel->On100msTimer();
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

MyFrame::MyFrame(const wxString& title)
	: wxFrame(NULL, wxID_ANY, title)
{
	tray = new TrayIcon();
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
	SetStatusText("WindowsHelper " + platform + " v" + COMMIT_TAG + " DEBUG BUILD " + COMMIT_ID);
#else
	SetStatusText("WindowsHelper " + platform + " v" + COMMIT_TAG);
#endif

	SetClientSize(Settings::Get()->window_size);

	UsedPages used_pages = Settings::Get()->used_pages;
	if(used_pages.main)
	{
		main_panel = new MainPanel(this);
		Sensors::Get()->AddObserver(main_panel);
	}
	if(used_pages.escaper)
		escape_panel = new EscaperPanel(this);
	if(used_pages.debug)
		debug_panel = new DebugPanel(this);
	if(used_pages.file_browser)
		file_panel = new FilePanel(this);
	if(used_pages.cmd_executor)
		cmd_panel = new CmdExecutorPanelBase(this, *wxGetApp().cmd_executor);
	if(used_pages.can)
		can_panel = new CanPanel(this);
	if(used_pages.did)
		did_panel = new DidPanel(this);
	if(used_pages.modbus_master)
		modbus_master_panel = new ModbusMasterPanel(this);
	if (used_pages.alarm_panel)
		alarm_panel = new AlarmPanel(this);
	if (used_pages.time_tracker)
		timesheet_panel = new TimeTrackerPanel(this);
	if(used_pages.log)
		log_panel = new LogPanel(this);
	Logger::Get()->AppendPreinitedEntries();
	
	wxSize client_size = GetClientSize();
	ctrl = new wxAuiNotebook(this, wxID_ANY, wxPoint(client_size.x, client_size.y), FromDIP(wxSize(430, 200)), wxAUI_NB_TOP | wxAUI_NB_TAB_SPLIT | wxAUI_NB_TAB_MOVE | wxAUI_NB_SCROLL_BUTTONS | wxAUI_NB_MIDDLE_CLICK_CLOSE | wxAUI_NB_TAB_EXTERNAL_MOVE | wxNO_BORDER);
	ctrl->Freeze();
	if(used_pages.main)
		ctrl->AddPage(main_panel, "Main Page", false, wxArtProvider::GetBitmap(wxART_GO_HOME, wxART_OTHER, FromDIP(wxSize(16, 16))));
	if(used_pages.escaper)
		ctrl->AddPage(escape_panel, "C StrEscape", false, wxArtProvider::GetBitmap(wxART_LIST_VIEW, wxART_OTHER, FromDIP(wxSize(16, 16))));
	if(used_pages.debug)
		ctrl->AddPage(debug_panel, "Debug Page", false, wxArtProvider::GetBitmap(wxART_HELP, wxART_OTHER, FromDIP(wxSize(16, 16))));
	if(used_pages.file_browser)
		ctrl->AddPage(file_panel, "File Browser", false, wxArtProvider::GetBitmap(wxART_FILE_OPEN, wxART_OTHER, FromDIP(wxSize(16, 16))));
	if(used_pages.cmd_executor)
		ctrl->AddPage(cmd_panel, "CMD Executor", false, wxArtProvider::GetBitmap(wxART_FILE_OPEN, wxART_OTHER, FromDIP(wxSize(16, 16))));
	if(used_pages.can)
		ctrl->AddPage(can_panel, "CAN Sender", false, wxArtProvider::GetBitmap(wxART_REMOVABLE, wxART_OTHER, FromDIP(wxSize(16, 16))));
	if(used_pages.did)
		ctrl->AddPage(did_panel, "DID", false, wxArtProvider::GetBitmap(wxART_FIND, wxART_OTHER, FromDIP(wxSize(16, 16))));
	if(used_pages.modbus_master)
		ctrl->AddPage(modbus_master_panel, "ModbusMaster", false, wxArtProvider::GetBitmap(wxART_PRINT, wxART_OTHER, FromDIP(wxSize(16, 16))));
	if (used_pages.alarm_panel)
		ctrl->AddPage(alarm_panel, "AlarmPanel", false, wxArtProvider::GetBitmap(wxART_TICK_MARK, wxART_OTHER, FromDIP(wxSize(16, 16))));	
	if (used_pages.time_tracker)
		ctrl->AddPage(timesheet_panel, "Timesheet", false, wxArtProvider::GetBitmap(wxART_PLUS, wxART_OTHER, FromDIP(wxSize(16, 16))));
	if(used_pages.log)
		ctrl->AddPage(log_panel, "Log", false, wxArtProvider::GetBitmap(wxART_TIP, wxART_OTHER, FromDIP(wxSize(16, 16))));
	ctrl->Thaw();

	SetCurrentPage(Settings::Get()->default_page);
	Show(!Settings::Get()->minimize_on_startup);

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

	std::visit([this](const auto& value) { HandleNotification(value); }, *notification);
}

void MyFrame::HandleNotification(const SimpleNotification& notification)
{
	wxString title;
	wxString message;
	int icon = wxICON_INFORMATION;
	switch(notification.kind)
	{
		case SimpleNotificationKind::ScreenshotSaveFailed:
			title = "Failed to save the screenshot!";
			message = "An error occurred while saving the screenshot.";
			icon = wxICON_ERROR;
			break;
		case SimpleNotificationKind::SettingsSaved:
			title = "Settings saved";
			message = "Settings have been successfully saved";
			break;
		case SimpleNotificationKind::StringEscaped:
			title = "String escaped";
			message = "String has been escaped and placed on the clipboard";
			break;
		case SimpleNotificationKind::TxListLoaded: title = message = "TX List Loaded"; break;
		case SimpleNotificationKind::TxListSaved: title = message = "TX List Saved"; break;
		case SimpleNotificationKind::RxListLoaded: title = message = "RX List Loaded"; break;
		case SimpleNotificationKind::RxListSaved: title = message = "RX List Saved"; break;
		case SimpleNotificationKind::FrameMappingLoaded: title = message = "Frame Mapping Loaded"; break;
		case SimpleNotificationKind::FrameMappingSaved: title = message = "Frame Mapping Saved"; break;
		case SimpleNotificationKind::TxListLoadError:
			title = message = "TX List Load failed"; icon = wxICON_ERROR; break;
		case SimpleNotificationKind::RxListLoadError:
			title = message = "RX List Load failed"; icon = wxICON_ERROR; break;
		case SimpleNotificationKind::FrameMappingLoadError:
			title = message = "Frame Mapping Load failed"; icon = wxICON_ERROR; break;
		case SimpleNotificationKind::DidUpdated:
			title = "DID updated"; message = "DID value has been updated!"; break;
		case SimpleNotificationKind::SelectedLogsCopied:
			title = "Logs copied"; message = "Selected logs copied to clipboard"; break;
		case SimpleNotificationKind::EverythingSaved:
			title = "Configurations saved"; message = "Every configuration has been saved"; break;
	}

	ShowNotificaiton(title, message, 3, icon, [kind = notification.kind](wxCommandEvent&)
	{
#ifdef _WIN32
		if(kind == SimpleNotificationKind::SettingsSaved)
		{
			wchar_t work_dir[1024]{};
			GetCurrentDirectoryW(WXSIZEOF(work_dir) - 1, work_dir);
			StrCatW(work_dir, L"\\settings.ini");
			ShellExecuteW(nullptr, L"open", work_dir, nullptr, nullptr, SW_SHOW);
		}
		else if(kind == SimpleNotificationKind::EverythingSaved)
		{
			wchar_t work_dir[1024]{};
			GetCurrentDirectoryW(WXSIZEOF(work_dir) - 1, work_dir);
			ShellExecuteW(nullptr, nullptr, work_dir, nullptr, nullptr, SW_SHOWNORMAL);
		}
#endif
	});
}

void MyFrame::HandleNotification(const FileSavedNotification& notification)
{
	wxString title;
	wxString subject;
	bool path_is_relative = false;
	switch(notification.kind)
	{
		case SavedFileKind::Screenshot: title = "Screenshot saved"; subject = "Screenshot"; break;
		case SavedFileKind::CanLog: title = "CAN Log saved"; subject = "CAN log"; path_is_relative = true; break;
		case SavedFileKind::ModbusLog: title = "Modbus Log saved"; subject = "Modbus log"; path_is_relative = true; break;
		case SavedFileKind::Commands: title = "Commands saved"; subject = "Commands"; break;
		case SavedFileKind::DidCache: title = "DIDs cache saved"; subject = "DIDs cache"; break;
	}

	ShowNotificaiton(title, wxString::Format("%s saved in %.3fms\nPath: %s", subject,
		static_cast<double>(notification.duration_ns) / 1'000'000.0, notification.filename),
		3, wxICON_INFORMATION, [filename = notification.filename, path_is_relative](wxCommandEvent&)
	{
#ifdef _WIN32
		std::string selected_file = filename;
		if(path_is_relative)
		{
			char work_dir[1024]{};
			GetCurrentDirectoryA(sizeof(work_dir) - 1, work_dir);
			selected_file = std::string(work_dir) + "\\" + selected_file;
			boost::algorithm::replace_all(selected_file, "/", "\\");
		}
		const std::string command_line = "/select,\"" + selected_file + "\"";
		ShellExecuteA(nullptr, "open", "explorer.exe", command_line.c_str(), nullptr, SW_NORMAL);
#endif
	});
}

void MyFrame::HandleNotification(const PathSeparatorsReplacedNotification& notification)
{
	ShowNotificaiton("Path separator replaced",
		wxString::Format("New form is in the clipboard:\n%s", notification.path.substr(0, 64)),
		3, wxICON_INFORMATION, [](wxCommandEvent&) {});
}

void MyFrame::HandleNotification(const BackupCompletedNotification& notification)
{
	ShowNotificaiton("Backup complete",
		wxString::Format("Backed up %zu files (%s) to %zu places in %.3fms", notification.file_count,
			utils::GetDataUnit(notification.bytes_copied), notification.destination_count,
			static_cast<double>(notification.duration_ns) / 1'000'000.0),
		3, wxICON_INFORMATION, [destination = notification.destination](wxCommandEvent&)
	{
#ifdef _WIN32
		ShellExecuteA(nullptr, nullptr, destination.generic_string().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#endif
	});
}

void MyFrame::HandleNotification(const BackupFailedNotification& notification)
{
	ShowNotificaiton("Backup failed!",
		"Backup failed due to wrong checksum values\nMake sure that your drive is not damaged\nCheck log file for more info",
		3, wxICON_ERROR, [destination = notification.destination](wxCommandEvent&)
	{
#ifdef _WIN32
		ShellExecuteA(nullptr, nullptr, destination.generic_string().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#endif
	});
}

void MyFrame::HandleNotification(const AlarmSetupNotification& notification)
{
	ShowNotificaiton(wxString::Format("Alarm setup - %s", notification.name),
		wxString::Format("Alarm has been set for %lld seconds", notification.duration.count()),
		3, wxICON_INFORMATION, [](wxCommandEvent&) {});
}

void MyFrame::HandleNotification(const AlarmTriggeredNotification& notification)
{
	ShowNotificaiton(wxString::Format("Alarm executed - %s", notification.name), "Alarm has been executed",
		3, wxICON_INFORMATION, [](wxCommandEvent&) {});
}

void MyFrame::HandleNotification(const WorktimeToggledNotification& notification)
{
	const std::string state = notification.working ? "Started" : "Stopped";
	std::string details = wxString::Format("Worktime has been %s", state).ToStdString();
	if(notification.duration != std::chrono::seconds::zero())
	{
		const auto total_seconds = notification.duration.count();
		details = wxString::Format("Worktime has been %s\nDuration: %02lld:%02lld:%02lld", state,
			total_seconds / 3600, (total_seconds / 60) % 60, total_seconds % 60).ToStdString();
	}
	ShowNotificaiton(wxString::Format("Worktime - %s", state), details,
		3, wxICON_INFORMATION, [](wxCommandEvent&) {});
}

template<typename T> void MyFrame::ShowNotificaiton(const wxString& title, const wxString& message, int timeout, int flags, T&& fptr)
{
	wxNotificationMessageBase* m_notif = new wxGenericNotificationMessage(title, message, this, flags);
	m_notif->Show(timeout);
	m_notif->Bind(wxEVT_NOTIFICATION_MESSAGE_CLICK, fptr);
}
