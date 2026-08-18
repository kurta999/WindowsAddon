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

bool MyApp::OnInit()
{
    if(!wxApp::OnInit())
        return false;

    ExceptionHandler::Register();

    can_entry = std::make_unique<CanEntryHandler>(xml, rx_xml, mapping_xml, *CanSerialPort::Get(), clock, this);
    cmd_executor = std::make_unique<CmdExecutor>(command_runner, command_text_resolver);
    did_handler = std::make_unique<DidHandler>(did_xml_loader, did_xml_chace_loader, can_entry.get());
    modbus_handler = std::make_unique<ModbusEntryHandler>(modbus_entry_loader, this);
    alarm_entry = std::make_unique<AlarmEntryHandler>(alarm_entry_loader);
    time_tracker = std::make_unique<TimeTracker>(
        std::make_unique<TimeTrackerStorage>("time_db.db"),
        [](const std::string& error) { LOG(LogLevel::Error, "{}", error); });
    script_launcher = std::make_unique<ScriptLauncher>(
        command_runner, file_system, script_command_resolver);

    DirectoryBackup::Get()->SetEventSink(this);

    Settings::Get()->Init();
    SerialPort::Get()->Init();
    CanSerialPort::Get()->Init();
    Server::Get()->Init();
    Sensors::Get()->Init();
    PrintScreenSaver::Get()->Init();
    DirectoryBackup::Get()->Init();
    SerialTcpBackend::Get()->Init();
    CorsairHid::Get()->Init();
    BsecHandler::Get()->Init();
    WorkingDays::Get()->Update();

    can_entry->Init();
    cmd_executor->Init();
    did_handler->Init();
    modbus_handler->Init();
    alarm_entry->Init();
    time_tracker->Init();

    if(!wxTaskBarIcon::IsAvailable())
        LOG(LogLevel::Warning, "There appears to be no system tray support in your current environment. This app may not behave as expected.");
    MyFrame* frame = new MyFrame(wxT("WindowsHelper"));
    SetTopWindow(frame);
    is_init_finished = true;
    modbus_handler->Start();
    TerminalHotkey::Get()->UpdateHotkeyRegistration();
    return true;
}

int MyApp::OnExit()
{
    is_init_finished = false;

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

    // Stop producers before their consumers. These are legacy service-locator
    // instances; new services are owned directly above and injected.
    DirectoryBackup::Get()->SetEventSink(nullptr);
    DirectoryBackup::CSingleton::Destroy();
    Server::CSingleton::Destroy();
    SerialTcpBackend::CSingleton::Destroy();
    CanSerialPort::CSingleton::Destroy();
    SerialPort::CSingleton::Destroy();
    CorsairHid::CSingleton::Destroy();
    CryptoPrice::CSingleton::Destroy();
    PrintScreenSaver::CSingleton::Destroy();

    // Stop the graph worker before releasing the sensor coordinator.
    DatabaseLogic::CSingleton::Destroy();
    Sensors::CSingleton::Destroy();
    BsecHandler::CSingleton::Destroy();
    CustomMacro::CSingleton::Destroy();
    TerminalHotkey::CSingleton::Destroy();
    PathSeparator::CSingleton::Destroy();
    WorkingDays::CSingleton::Destroy();

    // Restores CPU power and may log failures, so Logger must remain last.
    IdlePowerSaver::CSingleton::Destroy();
    Settings::CSingleton::Destroy();
    Logger::CSingleton::Destroy();
    return true;
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
            frame->show_backup_dlg = true;
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
        frame->show_backup_dlg = false;
    });
}
