#include "pch.hpp"

IMPLEMENT_APP(MyApp)

#pragma pack(push, 1)
typedef struct
{
    uint16_t time[2];
    uint16_t date[2];
    uint16_t event_data[4];
    uint16_t error_id;
    uint64_t index;
    uint16_t crc;
    uint8_t padding[4];
} EventlogEntry_t;
#pragma pack(pop, 1)

constexpr size_t EVENTLOG_ENTRY_SIZE = sizeof(EventlogEntry_t);

// !\brief Wire the settings blocks, in the order they appear in settings.ini.
//
// This is the only place that knows which subsystems have settings. What each
// one stores is the subsystem's own business, which is why Settings no longer
// carries a 580-line load/save switchboard.
void MyApp::RegisterSettingsBindings()
{
    m_MacroSettings = std::make_unique<MacroSettings>(*CustomMacro::Get(), *script_launcher);
    m_CanSenderSettings = std::make_unique<CanSenderSettings>(*can_port, *can_entry);

    Settings* settings = Settings::Get();
    settings->ClearBindings();
    settings->RegisterBinding(*m_MacroSettings);
    settings->RegisterBinding(*Sensors::Get());
    settings->RegisterBinding(*SerialPort::Get());
    settings->RegisterBinding(*SerialTcpBackend::Get());
    settings->RegisterBinding(*m_CanSenderSettings);
    settings->RegisterBinding(*modbus_handler);
    settings->RegisterBinding(*settings);
    settings->RegisterBinding(*corsair_hid);
    settings->RegisterBinding(*screenshots);
    settings->RegisterBinding(*path_separator);
    settings->RegisterBinding(*TerminalHotkey::Get());
    settings->RegisterBinding(*idle_power_saver);
    settings->RegisterBinding(*directory_backup);
    settings->RegisterBinding(*DatabaseLogic::Get());
    settings->RegisterBinding(*time_tracker);

    settings->SetWindowSizeProvider([]() -> LogicalSize
    {
        if(auto* frame = dynamic_cast<wxTopLevelWindow*>(wxGetApp().GetTopWindow()))
            return LogicalSize{ frame->GetSize().x, frame->GetSize().y };
        return {};
    });
}

// !\brief Fill the global hotkey chain, in priority order.
//
// This is the only place that knows which features claim a key. CustomMacro
// used to hold that list twice - once to dispatch and once to warn about
// conflicts - and adding a feature meant editing both.
void MyApp::RegisterHotkeyHandlers()
{
    m_Hotkeys.Clear();
    m_Hotkeys.SetUiMarshaller([](std::function<void()> action)
    {
        wxGetApp().CallAfter(std::move(action));
    });

    m_Hotkeys.Register(*screenshots);
    m_Hotkeys.Register(*path_separator);
    m_Hotkeys.Register(*script_launcher);
    m_Hotkeys.Register(m_TimeTrackerHotkey);

    CustomMacro* macros = CustomMacro::Get();
    m_Macros = macros;
    macros->SetHotkeyRegistry(&m_Hotkeys);
    macros->SetMacroContext(MacroContext{ cmd_executor.get(), &m_ScreenAutomation, Sensors::Get() });
    macros->SetAlarmHandler([this](const std::string& key)
    {
        if(alarm_entry)
            alarm_entry->HandleKeypress(key);
    });
    macros->SetForegroundToggle([]
    {
        if(auto* frame = dynamic_cast<MyFrame*>(wxGetApp().GetTopWindow()))
            frame->ToggleForegroundVisibility();
    });

    /* The two keypad drivers deliver into the macro engine. They used to fetch
       it themselves, from their own receive threads. */
    SerialPort::Get()->SetKeySink(macros);
    corsair_hid->SetKeySink(macros);
}

std::string TimeTrackerHotkey::HotkeyBinding() const
{
    const auto& tracker = wxGetApp().time_tracker;
    return tracker ? tracker->GetToggleKey() : std::string{};
}

void TimeTrackerHotkey::OnHotkeyPressed()
{
    if(auto* frame = dynamic_cast<MyFrame*>(wxGetApp().GetTopWindow()))
    {
        if(frame->timesheet_panel)
            frame->timesheet_panel->ToggleWorktime();
    }
}

// !\brief Give the services that report to the user their route to the window.
//
// Each of these used to downcast wxGetApp().GetTopWindow() at the moment it had
// something to say, which is undefined behaviour whenever the top window is a
// dialog or the frame is already gone.
void MyApp::ConnectGuiPorts(MyFrame& frame)
{
    screenshots->SetNotificationSink(&frame);
    path_separator->SetNotificationSink(&frame);
    path_separator->SetClipboard(&m_Clipboard);
    TerminalHotkey::Get()->SetHotkeyRegistrar([&frame](int vkey) { frame.RegisterTerminalHotkey(vkey); });
}

bool MyApp::OnInit()
{
    if(!wxApp::OnInit())
        return false;

    ExceptionHandler::Register();

    can_port = std::make_unique<CanSerialPort>();
    corsair_hid = std::make_unique<CorsairHid>();
    directory_backup = std::make_unique<DirectoryBackup>();
    can_entry = std::make_unique<CanEntryHandler>(xml, rx_xml, mapping_xml, *can_port, clock, this);
    cmd_executor = std::make_unique<CmdExecutor>(command_runner, command_text_resolver);
    did_handler = std::make_unique<DidHandler>(did_xml_loader, did_xml_chace_loader, can_entry.get());
    modbus_handler = std::make_unique<ModbusEntryHandler>(modbus_entry_loader, this);
    alarm_entry = std::make_unique<AlarmEntryHandler>(alarm_entry_loader, this, this);
    time_tracker = std::make_unique<TimeTracker>(
        std::make_unique<TimeTrackerStorage>("time_db.db"),
        [](const std::string& error) { LOG(LogLevel::Error, "{}", error); });
    script_launcher = std::make_unique<ScriptLauncher>(
        command_runner, file_system, script_command_resolver);
    working_days = std::make_unique<WorkingDays>();
    Settings* settings_service = Settings::Get();

    crypto_price = std::make_unique<CryptoPrice>();
    crypto_price->SetSettings(*settings_service);
    path_separator = std::make_unique<PathSeparator>();
    idle_power_saver = std::make_unique<IdlePowerSaver>();
    screenshots = std::make_unique<PrintScreenSaver>();

    directory_backup->SetEventSink(this);

    RegisterSettingsBindings();
    RegisterHotkeyHandlers();

    /* The two log keys in [App] belong to the logger, which reads and
       writes them itself. */
    settings_service->SetLogger(*Logger::Get());
    settings_service->Init();
    SerialPort::Get()->Init();
    can_port->Init();
    /* The sensor coordinator is the sink for anything the TCP server
       accepts; Round 3 unpicks the rest of this pair. */
    Server* server = Server::Get();
    Sensors* sensors = Sensors::Get();
    server->SetMeasurementSink(*sensors);
    server->SetSharedDriveLetterSource([settings_service]
    {
        return settings_service->shared_drive_letter;
    });
    DatabaseLogic* database = DatabaseLogic::Get();
    sensors->SetServer(*server);
    sensors->SetDatabase(*database);
    sensors->SetBsec(*BsecHandler::Get());
    server->SetGraphRefresh([sensors, database]
    {
        database->GenerateGraphs(sensors->GetGraphResolution());
    });
    server->Init();
    sensors->Init();
    screenshots->Init();
    directory_backup->Init();
    SerialTcpBackend* serial_tcp = SerialTcpBackend::Get();
    serial_tcp->SetReceptionSink([port = SerialPort::Get()](const char* data, unsigned int len)
    {
        port->SimulateDataReception(data, len);
    });
    serial_tcp->Init();
    corsair_hid->Init();
    BsecHandler::Get()->Init();
    working_days->Update();

    can_entry->Init();
    cmd_executor->Init();
    did_handler->Init();
    modbus_handler->Init();
    alarm_entry->Init();
    time_tracker->Init();

    if(!wxTaskBarIcon::IsAvailable())
        LOG(LogLevel::Warning, "There appears to be no system tray support in your current environment. This app may not behave as expected.");
    MyFrame* frame = new MyFrame(wxT("WindowsAddon"), *screenshots, *directory_backup,
        *settings_service);
    SetTopWindow(frame);
    ConnectGuiPorts(*frame);
    is_init_finished = true;
    modbus_handler->SetReady(true);
    modbus_handler->Start();
    TerminalHotkey::Get()->UpdateHotkeyRegistration();
    return true;
}

int MyApp::OnExit()
{
    is_init_finished = false;
    if(modbus_handler)
        modbus_handler->SetReady(false);

    // Release injected services before the adapters they reference.
    alarm_entry.reset();
    did_handler.reset();
    can_entry.reset();
    if(modbus_handler)
        modbus_handler->Shutdown();
    modbus_handler.reset();
    cmd_executor.reset();
    script_launcher.reset();
    time_tracker.reset();
    working_days.reset();
    crypto_price.reset();
    path_separator.reset();
    screenshots.reset();
    corsair_hid.reset();
    /* The destructor joins a running backup worker; the sink goes first so
       that worker cannot call back into an app that is mid-teardown. */
    directory_backup->SetEventSink(nullptr);
    directory_backup.reset();

    /* After can_entry, which holds a reference to it. */
    can_port.reset();

    // Stop producers before their consumers. These are legacy service-locator
    // instances; new services are owned directly above and injected.
    Server::CSingleton::Destroy();
    SerialTcpBackend::CSingleton::Destroy();
    SerialPort::CSingleton::Destroy();

    // Stop the graph worker before releasing the sensor coordinator.
    DatabaseLogic::CSingleton::Destroy();
    Sensors::CSingleton::Destroy();
    BsecHandler::CSingleton::Destroy();
    CustomMacro::CSingleton::Destroy();
    TerminalHotkey::CSingleton::Destroy();

    // Restores CPU power and may log failures, so both of these still run
    // before the logger, which m_LoggerLifetime releases after OnExit returns.
    idle_power_saver.reset();
    Settings::CSingleton::Destroy();
    return true;
}

LoggerLifetime::~LoggerLifetime()
{
    Logger::CSingleton::Destroy();
}

void MyApp::OnUnhandledException()
{
    try
    {
        throw;
    }
    catch(const std::exception& e)
    {
#ifdef _WIN32
        MessageBoxA(NULL, e.what(), "std::exception caught", MB_OK);
#endif
    }
    catch(...)
    {
#ifdef _WIN32
        MessageBoxA(NULL, "Unknown exception", "exception caught", MB_OK);
#endif
    }
}

void MyApp::OnCanFrameTransmitted(std::uint32_t frame_id, std::size_t count)
{
    CallAfter([this, frame_id, count]
    {
        if(!is_init_finished)
            return;
        auto* frame = dynamic_cast<MyFrame*>(GetTopWindow());
        if(frame && frame->is_initialized && frame->can_panel && frame->can_panel->sender)
            frame->can_panel->sender->can_grid_tx->UpdateTxCounter(frame_id, count);
    });
}

void MyApp::OnCanRecordingSaved(const std::filesystem::path& path, std::int64_t duration_ns)
{
    CallAfter([this, path, duration_ns]
    {
        if(auto* frame = dynamic_cast<MyFrame*>(GetTopWindow()))
            frame->PostNotification(FileSavedNotification{SavedFileKind::CanLog, duration_ns, path.generic_string()});
    });
}

void MyApp::OnAlarmLoaded(const AlarmEntry& entry)
{
    /* An alarm is armed by a macro key, so its trigger has to be known to the
       macro engine. The alarm loader used to do this itself, which is what tied
       reading an XML file to the macro singleton. */
    if(m_Macros == nullptr)
        return;

    auto& macros = m_Macros->GetMacros();
    if(macros.empty())
    {
        LOG(LogLevel::Error, "No macro profile exists yet, alarm '{}' will not be triggerable", entry.name);
        return;
    }
    /* ParseMacroKeys consumes the command text through a mutable reference. */
    std::string execute = entry.execute;
    m_Macros->ParseMacroKeys(0, entry.trigger_key, execute, macros[0], MacroFlags::Alarm);
}

void MyApp::OnAlarmArmed(const std::string& name, std::chrono::seconds duration)
{
    if(auto* frame = dynamic_cast<MyFrame*>(GetTopWindow()))
        frame->PostNotification(AlarmSetupNotification{name, duration});
}

void MyApp::OnAlarmTriggered(const std::string& name)
{
    if(auto* frame = dynamic_cast<MyFrame*>(GetTopWindow()))
        frame->PostNotification(AlarmTriggeredNotification{name});
}

void MyApp::OnAlarmMacroRequested(const std::string& trigger_key)
{
    if(m_Macros != nullptr)
        m_Macros->SimulateKeypress(trigger_key, true);
}

std::string MyApp::AskForDuration(bool pump_timer_once)
{
    auto* frame = dynamic_cast<MyFrame*>(GetTopWindow());
    if(!frame || !frame->alarm_panel)
        return {};

    frame->alarm_panel->ShowAlarmDialog();
    if(pump_timer_once)
        frame->alarm_panel->On10MsTimer();
    frame->alarm_panel->WaitForAlarmSemaphore();
    return frame->alarm_panel->GetAlarmTime();
}

void MyApp::OnModbusRecordingSaved(const std::filesystem::path& path, std::int64_t duration_ns)
{
    CallAfter([this, path, duration_ns]
    {
        if(auto* frame = dynamic_cast<MyFrame*>(GetTopWindow()))
            frame->PostNotification(FileSavedNotification{SavedFileKind::ModbusLog, duration_ns, path.generic_string()});
    });
}

void MyApp::OnBackupStarted()
{
    CallAfter([this]
    {
        if(auto* frame = dynamic_cast<MyFrame*>(GetTopWindow()))
            frame->backup_progress.SetWanted(true);
    });
}

void MyApp::OnBackupFinished(const BackupSummary& summary)
{
    CallAfter([this, summary]
    {
        auto* frame = dynamic_cast<MyFrame*>(GetTopWindow());
        if(!frame)
            return;

        if(summary.success)
        {
            frame->PostNotification(BackupCompletedNotification{
                summary.duration_ns, summary.file_count, summary.bytes_copied,
                summary.destination_count, summary.destination});
        }
        else
        {
            frame->PostNotification(BackupFailedNotification{summary.destination});
        }
        frame->backup_progress.SetWanted(false);
    });
}
