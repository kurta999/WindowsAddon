#pragma once

#include "IAlarmEntryLoader.hpp"
#include "IAlarmEventSink.hpp"
#include "IAlarmPrompt.hpp"
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

enum class AlarmTrigger
{
    Macro,
    Gui,
    Invalid,
};

AlarmTrigger AlarmStringToTrigger(const std::string& in);
std::string AlarmTriggerToString(AlarmTrigger trigger);

// !\brief Turns the duration a user typed into seconds.
// Accepts "1h30m15s", "1h30m", "30m15s", and a bare number followed by
// "hour", "min" or "sec". Anything else is zero.
std::chrono::seconds ParseAlarmDuration(const std::string& input);

class AlarmEntry
{
public:
    AlarmEntry(const std::string& name_, AlarmTrigger trigger_, const std::string& trigger_key_, const std::string& execute_, bool show_dialog_) :
        name(name_), trigger(trigger_), trigger_key(trigger_key_), execute(execute_), show_dialog(show_dialog_)
    {

    }

    std::string name;
    AlarmTrigger trigger;
    std::string trigger_key;
    std::string execute;
    bool show_dialog;
    bool is_armed{ false };
    std::chrono::seconds duration{ 0 };
};

class XmlAlarmEntryLoader : public IAlarmEntryLoader
{
public:
    virtual ~XmlAlarmEntryLoader() = default;

    bool Load(const std::filesystem::path& path, std::vector<std::unique_ptr<AlarmEntry>>& e) override;
    bool Save(const std::filesystem::path& path, std::vector<std::unique_ptr<AlarmEntry>>& e) const override;
};

class AlarmEntryHandler
{
public:
    // !\param prompt [in] Asks the user for a duration. Without one an alarm
    // can still be armed programmatically, but never from a key press.
    // !\param event_sink [in] Receives everything the handler wants to tell the
    // application. Optional: the handler works without it.
    AlarmEntryHandler(IAlarmEntryLoader& loader, IAlarmPrompt* prompt = nullptr,
        IAlarmEventSink* event_sink = nullptr);
    ~AlarmEntryHandler();

    // !\brief Loads the alarms and starts the countdown worker.
    void Init();

    void WorkerThread(std::stop_token token);

    // !\brief One second of alarm time: counts every armed alarm down and fires
    // the ones that reached zero. The worker calls this once a second, and a
    // handler that was never Init()ed can be driven by hand instead.
    void Tick();

    bool Load();

    bool LoadAlarms(std::filesystem::path& path);

    bool SaveAlarms(std::filesystem::path& path);

    // !\brief Asks the user for a duration and arms the alarm with the answer.
    void SetupAlarm(uint8_t id);
    void SetupAlarm(AlarmEntry* entry);
    void CancelAlarm(AlarmEntry* entry);

    // !\brief Arms the alarm bound to this key, after asking for a duration.
    // !\param key [in] The trigger key that was pressed
    // !\param force_timer_call [in] Passed on to the prompt; see IAlarmPrompt
    void HandleKeypress(const std::string& key, bool force_timer_call = false);

    // !\brief Default alarms XML name
    std::filesystem::path default_alarms = "Alarms.xml";

    // !\brief The alarm list, under the handler's own lock.
    //
    // `entries` was public while the worker thread armed and fired entries
    // under `m` - and the panel iterated it, the tray menu indexed it and the
    // tests sized it, all without the lock. Same treatment as DidHandler and
    // CanEntryHandler: taking the lock stops being something callers remember.
    struct Model
    {
        std::vector<std::unique_ptr<AlarmEntry>>& entries;
    };

    // !\brief Run `fn` over the alarm list with the lock held.
    //
    // `fn` must not call back into a handler method that takes the same lock,
    // and must not keep a pointer it is given past a reload.
    template <typename F> decltype(auto) WithModel(F&& fn)
    {
        std::scoped_lock lock(m);
        Model model{ entries };
        return std::forward<F>(fn)(model);
    }

private:
    // !\brief Vector of Alarm entries
    std::vector<std::unique_ptr<AlarmEntry>> entries;

    // !\brief Arms one entry with a duration the prompt supplies.
    void ArmFromPrompt(AlarmEntry* entry, bool force_timer_call);

    // !\brief Reference to Alarm entry loader
    IAlarmEntryLoader& m_AlarmEntryLoader;

    IAlarmPrompt* m_Prompt = nullptr;

    IAlarmEventSink* m_EventSink = nullptr;

    // !\brief Worker thread
    std::unique_ptr<std::jthread> m_worker;

    // !\brief Conditional variable for main thread exiting
    std::condition_variable_any m_cv;

    // !\brief Mutex for entry handler
    std::mutex m;
};
