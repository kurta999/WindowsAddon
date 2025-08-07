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

    can_entry = std::make_unique<CanEntryHandler>(xml, rx_xml, mapping_xml);
    cmd_executor = std::make_unique<CmdExecutor>();
    did_handler = std::make_unique<DidHandler>(did_xml_loader, did_xml_chace_loader, can_entry.get());
    alarm_entry = std::make_unique<AlarmEntryHandler>(alarm_entry_loader);
    time_tracker = std::make_unique<TimeTracker>();

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
    alarm_entry->Init();
    time_tracker->Init();

    if(!wxTaskBarIcon::IsAvailable())
        LOG(LogLevel::Warning, "There appears to be no system tray support in your current environment. This app may not behave as expected.");
    MyFrame* frame = new MyFrame(wxT("WindowsAddon"));
    SetTopWindow(frame);
    is_init_finished = true;
    TerminalHotkey::Get()->UpdateHotkeyRegistration();
    return true;
}

int MyApp::OnExit()
{
    is_init_finished = false;
    
    CanSerialPort::CSingleton::Destroy();

    did_handler.reset(nullptr);  /* First this has to be destructed, because it uses CanEntryHandler */
    can_entry.reset(nullptr);
    cmd_executor.reset(nullptr);

    IdlePowerSaver::CSingleton::Destroy();  /* Restore CPU power to 100%, this has to be destructed before Logger */
    Settings::CSingleton::Destroy();
    CustomMacro::CSingleton::Destroy();
    Server::CSingleton::Destroy();
    Sensors::CSingleton::Destroy();
    DatabaseLogic::CSingleton::Destroy();
    PrintScreenSaver::CSingleton::Destroy();
    DirectoryBackup::CSingleton::Destroy();
    DatabaseLogic::CSingleton::Destroy();
    SerialTcpBackend::CSingleton::Destroy();
    SerialPort::CSingleton::Destroy();
    CorsairHid::CSingleton::Destroy();
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