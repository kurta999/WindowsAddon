#pragma once

#include "platform/ScreenAutomation.hpp"

#ifdef _WIN32
#include "resource.h"
#endif

#include <wx/wx.h>
#include "CanXmlLoaders.hpp"
#include "CanEntryHandler.hpp"
#include "CustomMacro.hpp"
#include "HotkeyRegistry.hpp"
#include "gui/WxClipboard.hpp"
#include "CmdExecutor.hpp"
#include "DidHandler.hpp"
#include "ModbusHandler.hpp"
#include "Alarms.hpp"
#include "TimeTracker.hpp"
#include "TimeTrackerStorage.hpp"
#include "ScriptLauncher.hpp"
#include "automation/ScriptCommandResolver.hpp"
#include "automation/CommandTextResolver.hpp"
#include "interface/IAlarmEventSink.hpp"
#include "interface/IAlarmPrompt.hpp"
#include "interface/IBackupEventSink.hpp"
#include "interface/ICanEventSink.hpp"
#include "interface/IModbusEventSink.hpp"
#include "platform/StandardFileSystem.hpp"
#include "platform/SystemClock.hpp"
#include "platform/SystemCommandRunner.hpp"

// !\brief The time tracker's toggle key.
//
// It lives here rather than on TimeTracker because the action is "toggle the
// worktime on the time tracker panel", which is a window operation; the tracker
// itself stays free of the GUI.
class WorkingDays;
class CryptoPrice;
class PathSeparator;
class IdlePowerSaver;
class PrintScreenSaver;
class CorsairHid;
class DirectoryBackup;
class CanSerialPort;

class TimeTrackerHotkey : public IHotkeyHandler
{
public:
    [[nodiscard]] std::string HotkeyBinding() const override;
    [[nodiscard]] std::string_view HotkeyOwner() const override { return "TimeTracker"; }
    void OnHotkeyPressed() override;
    [[nodiscard]] bool HotkeyNeedsUiThread() const override { return true; }
};

// !\brief Releases the logger when the application object goes.
//
// Every other service is torn down inside MyApp::OnExit, which runs before the
// application object is destroyed - so a member of MyApp outlives all of them.
// "Logger must be destroyed last" used to be a comment above the last line of
// OnExit, held in place by nothing; it is the language's rule about member
// destruction order now.
//
// It cannot stop a resurrection: CSingleton::Get() constructs on demand, so
// anything that logs after this point silently gets a fresh logger with default
// settings. What it does guarantee is that nothing this application shuts down
// deliberately can be the thing that does it.
struct LoggerLifetime
{
    ~LoggerLifetime();
};

class MyApp : public wxApp, public ICanEventSink, public IBackupEventSink, public IModbusEventSink,
    public IAlarmEventSink, public IAlarmPrompt
{
public:
    bool OnInit() override;
    int OnExit() override;
    void OnUnhandledException() override;

    void OnCanFrameTransmitted(std::uint32_t frame_id, std::size_t count) override;
    void OnCanRecordingSaved(const std::filesystem::path& path, std::int64_t duration_ns) override;
    void OnBackupStarted() override;
    void OnBackupFinished(const BackupSummary& summary) override;
    void OnModbusRecordingSaved(const std::filesystem::path& path, std::int64_t duration_ns) override;
    void OnAlarmLoaded(const AlarmEntry& entry) override;
    void OnAlarmArmed(const std::string& name, std::chrono::seconds duration) override;
    void OnAlarmTriggered(const std::string& name) override;
    void OnAlarmMacroRequested(const std::string& trigger_key) override;
    std::string AskForDuration(bool pump_timer_once) override;

    /* First member, so it is destroyed last. Nothing may be declared above it. */
    LoggerLifetime m_LoggerLifetime;

    /* The macro engine, kept from the wiring pass. The alarm sinks below run
       long after it and used to look it up again on every alarm. */
    CustomMacro* m_Macros = nullptr;

    // !\brief Helper variable for logger to avoid crash when inserting log messages befor logger frame is created
    bool is_init_finished = false;

    XmlCanEntryLoader xml;
    XmlCanRxEntryLoader rx_xml;
    XmlCanMappingLoader mapping_xml;
    XmlDidLoader did_xml_loader;
    XmlDidCacheLoader did_xml_chace_loader;
    HybridModbusEntryLoader modbus_entry_loader;
    XmlAlarmEntryLoader alarm_entry_loader;
    SystemClock clock;
    StandardFileSystem file_system;
    SystemCommandRunner command_runner;
    CommandTextResolver command_text_resolver;
    ScriptCommandResolver script_command_resolver;
    std::unique_ptr<CanEntryHandler> can_entry;
    std::unique_ptr<CmdExecutor> cmd_executor;
    std::unique_ptr<DidHandler> did_handler;
    std::unique_ptr<ModbusEntryHandler> modbus_handler;
    std::unique_ptr<AlarmEntryHandler> alarm_entry;
    std::unique_ptr<TimeTracker> time_tracker;
    std::unique_ptr<ScriptLauncher> script_launcher;
    std::unique_ptr<WorkingDays> working_days;
    std::unique_ptr<CryptoPrice> crypto_price;
    std::unique_ptr<PathSeparator> path_separator;
    std::unique_ptr<IdlePowerSaver> idle_power_saver;
    std::unique_ptr<PrintScreenSaver> screenshots;
    std::unique_ptr<CorsairHid> corsair_hid;
    std::unique_ptr<DirectoryBackup> directory_backup;

    /* Constructed before can_entry, which holds it as an ICanTransport&. */
    std::unique_ptr<CanSerialPort> can_port;

private:
    // !\brief Register every subsystem's settings block with Settings.
    void RegisterSettingsBindings();

    // !\brief Fill the global hotkey chain.
    void RegisterHotkeyHandlers();

    // !\brief Hand the services that talk to the user their GUI ports.
    void ConnectGuiPorts(MyFrame& frame);

    /* The screen, as the macro engine's IScreenAutomation port. */

    platform::ScreenAutomation m_ScreenAutomation;

    WxClipboard m_Clipboard;

    HotkeyRegistry m_Hotkeys;
    TimeTrackerHotkey m_TimeTrackerHotkey;

    // Bindings for the two sections that span more than one object.
    std::unique_ptr<MacroSettings> m_MacroSettings;
    std::unique_ptr<CanSenderSettings> m_CanSenderSettings;
};

DECLARE_APP(MyApp);
