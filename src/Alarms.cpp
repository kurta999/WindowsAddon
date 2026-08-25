#include "pch_core.hpp"
#include "Alarms.hpp"
#include "utils/XmlDocument.hpp"
#include "Logger.hpp"
#include "Utils.hpp"
#include "utils/InterruptibleSleep.hpp"
#include "utils/EnumNameTable.hpp"

using namespace std::chrono_literals;

/* The reading half was an if/else chain and the writing half a switch, so the
   capital in "Invalid" - which no reader ever matched - could sit here
   unnoticed. It is kept: it is what is already in every Alarms.xml, and it
   reads back as Invalid either way. */
namespace
{
inline constexpr utils::EnumNameTable kAlarmTriggerNames{
    AlarmTrigger::Invalid,
    std::array<utils::EnumName<AlarmTrigger>, 3>{{
        { AlarmTrigger::Invalid, "Invalid" },
        { AlarmTrigger::Macro, "macro" },
        { AlarmTrigger::Gui, "gui" },
    }}
};
}

AlarmTrigger AlarmStringToTrigger(const std::string& in)
{
    return kAlarmTriggerNames.FromName(in);
}

std::string AlarmTriggerToString(AlarmTrigger trigger)
{
    return std::string(kAlarmTriggerNames.NameOf(trigger));
}

std::chrono::seconds ParseAlarmDuration(const std::string& input)
{
    int hours = 0;
    int minutes = 0;
    int seconds = 0;
    char unit = 0;

    /* Longest form first: every pattern below is a prefix of the ones above it,
       so a shorter one would happily match half of a longer input. */

    /* "hh:mm:ss" and "hh:mm" - the format the duration dialog asks for. */
    if(sscanf(input.c_str(), "%d:%d:%d", &hours, &minutes, &seconds) == 3)
        return std::chrono::hours(hours) + std::chrono::minutes(minutes) + std::chrono::seconds(seconds);
    if(sscanf(input.c_str(), "%d:%d", &hours, &minutes) == 2)
        return std::chrono::hours(hours) + std::chrono::minutes(minutes);

    /* "1h30m15s", "1h30m", "30m15s" */
    if(sscanf(input.c_str(), "%dh%dm%ds", &hours, &minutes, &seconds) == 3)
        return std::chrono::hours(hours) + std::chrono::minutes(minutes) + std::chrono::seconds(seconds);
    if(sscanf(input.c_str(), "%dh%dm", &hours, &minutes) == 2)
        return std::chrono::hours(hours) + std::chrono::minutes(minutes);
    if(sscanf(input.c_str(), "%dm%ds", &minutes, &seconds) == 2)
        return std::chrono::minutes(minutes) + std::chrono::seconds(seconds);

    /* "3 hours", "20 minutes", "90 sec" */
    if(sscanf(input.c_str(), "%d", &hours) == 1 && input.find("hour") != std::string::npos)
        return std::chrono::hours(hours);
    if(sscanf(input.c_str(), "%d", &minutes) == 1 && input.find("min") != std::string::npos)
        return std::chrono::minutes(minutes);
    if(sscanf(input.c_str(), "%d", &seconds) == 1 && input.find("sec") != std::string::npos)
        return std::chrono::seconds(seconds);

    /* A single unit on its own: "90s", "30m", "2h". */
    if(sscanf(input.c_str(), "%d%c", &hours, &unit) == 2)
    {
        if(unit == 'h' || unit == 'H')
            return std::chrono::hours(hours);
        if(unit == 'm' || unit == 'M')
            return std::chrono::minutes(hours);
        if(unit == 's' || unit == 'S')
            return std::chrono::seconds(hours);
    }

    return std::chrono::seconds{};
}

bool XmlAlarmEntryLoader::Load(const std::filesystem::path& path, std::vector<std::unique_ptr<AlarmEntry>>& e)
{
    return utils::xml::LoadEntries(path, "AlarmsXml", [&e](const boost::property_tree::ptree::value_type& v)
    {
        std::string name = v.second.get_child("Name").get_value<std::string>();

        auto name_cnt = std::ranges::count(e, name, &AlarmEntry::name);
        if (name_cnt != 0)
        {
            LOG(LogLevel::Warning, "Alarm with name {} has been already added to the list, skipping this one", name);
            return;
        }

        std::string trigger_str = v.second.get_child("Trigger").get_value<std::string>();
        std::string key = v.second.get_child("Key").get_value<std::string>();
        std::string execute = v.second.get_child("Execute").get_value<std::string>();
        bool show_dialog = v.second.get_child("ShowDialog").get_value<bool>();

        AlarmTrigger trigger = AlarmStringToTrigger(trigger_str);
        e.push_back(std::make_unique<AlarmEntry>(name, trigger, key, execute, show_dialog));
    });
}

bool XmlAlarmEntryLoader::Save(const std::filesystem::path& path, std::vector<std::unique_ptr<AlarmEntry>>& e) const
{
    return utils::xml::SaveEntries(path, "AlarmsXml", [&e](boost::property_tree::ptree& root_node)
    {
        for(auto& i : e)
        {
            auto& alarm_node = root_node.add_child("Alarm", boost::property_tree::ptree{});
            alarm_node.add("Name", i->name);
            alarm_node.add("Trigger", AlarmTriggerToString(i->trigger));
            alarm_node.add("Key", i->trigger_key);
            alarm_node.add("Execute", i->execute);
            alarm_node.add("ShowDialog", i->show_dialog);
        }
    });
}

AlarmEntryHandler::AlarmEntryHandler(IAlarmEntryLoader& loader, IAlarmPrompt* prompt,
    IAlarmEventSink* event_sink) :
    m_AlarmEntryLoader(loader), m_Prompt(prompt), m_EventSink(event_sink)
{
}

AlarmEntryHandler::~AlarmEntryHandler()
{
    if(m_worker)
        m_worker->request_stop();
    {
        std::unique_lock lock{ m };
        m_cv.notify_all();
    }

    m_worker.reset();
}

void AlarmEntryHandler::Init()
{
    Load();

    /* Started here rather than in the constructor so a caller that only wants
       to load or inspect alarms - and anything driving Tick() itself - does not
       get a second countdown running underneath it. */
    m_worker = utils::StartNamedWorker("AlarmEntryHandler",
        std::bind_front(&AlarmEntryHandler::WorkerThread, this));
}

bool AlarmEntryHandler::Load()
{
    return LoadAlarms(default_alarms);
}

bool AlarmEntryHandler::LoadAlarms(std::filesystem::path& path)
{
    std::scoped_lock lock{ m };
    if (path.empty())
        path = default_alarms;

    entries.clear();
    bool ret = m_AlarmEntryLoader.Load(path, entries);
    if (ret)
    {
        LOG(LogLevel::Debug, "Alarms loaded, total: {}", entries.size());
        if(m_EventSink)
            for(const auto& entry : entries)
                m_EventSink->OnAlarmLoaded(*entry);
    }
    return ret;
}

bool AlarmEntryHandler::SaveAlarms(std::filesystem::path& path)
{
    if (path.empty())
        path = default_alarms;
    return m_AlarmEntryLoader.Save(path, entries);
}

void AlarmEntryHandler::ArmFromPrompt(AlarmEntry* entry, bool force_timer_call)
{
    if(!entry || !m_Prompt)
        return;

    /* Asked for outside the lock: the prompt blocks until the user answers, and
       the countdown must keep running in the meantime. */
    const std::chrono::seconds duration = ParseAlarmDuration(m_Prompt->AskForDuration(force_timer_call));

    {
        std::scoped_lock lock{ m };
        entry->is_armed = true;
        entry->duration = duration;
    }

    if(m_EventSink)
        m_EventSink->OnAlarmArmed(entry->name, duration);
}

void AlarmEntryHandler::SetupAlarm(uint8_t id)
{
    if (id >= entries.size())
    {
        LOG(LogLevel::Error, "No alarm with index {} exists", id);
        return;
    }
    SetupAlarm(entries[id].get());
}

void AlarmEntryHandler::SetupAlarm(AlarmEntry* entry)
{
    /* Armed from the GUI, which is not inside the dialog's own timer, so the
       prompt has to be pumped once before it can be waited on. */
    ArmFromPrompt(entry, true);
}

void AlarmEntryHandler::CancelAlarm(AlarmEntry* entry)
{
    if(!entry)
        return;

    std::scoped_lock lock{ m };
    entry->duration = 0s;
    entry->is_armed = false;
}

void AlarmEntryHandler::HandleKeypress(const std::string& key, bool force_timer_call)
{
    auto it = std::ranges::find_if(entries, [&key](const std::unique_ptr<AlarmEntry>& e) { return e->trigger_key == key; });
    if(it == entries.end())
        return;

    ArmFromPrompt(it->get(), force_timer_call);
}

void AlarmEntryHandler::Tick()
{
    /* What fired is collected under the lock and announced after it, the way
       ArmFromPrompt already treats its prompt. Announcing while holding the
       lock meant a sink could not call back into the handler, and the macro
       sink does exactly that: OnAlarmMacroRequested runs the macro, whose
       engine takes its own lock and can arm alarms through HandleKeypress.
       Held both ways round, those two locks could close on each other. */
    struct FiredAlarm
    {
        std::string trigger_key;
        std::string name;
    };
    std::vector<FiredAlarm> fired;

    {
        std::scoped_lock lock{ m };

        for(auto& a : entries)
        {
            if(!a->is_armed)
                continue;

            a->duration--;
            if(a->duration.count() > 0)
                continue;

            if(a->trigger == AlarmTrigger::Macro)
                fired.emplace_back(a->trigger_key, a->name);  /* copied: entries may be reloaded */
            a->is_armed = false;
        }
    }

    if(!m_EventSink)
        return;

    for(const FiredAlarm& alarm : fired)
    {
        m_EventSink->OnAlarmMacroRequested(alarm.trigger_key);
        m_EventSink->OnAlarmTriggered(alarm.name);
    }
}

void AlarmEntryHandler::WorkerThread(std::stop_token token)
{
    while(!token.stop_requested())
    {
        Tick();

        utils::InterruptibleSleep(m_cv, m, token, 1000ms);
    }
}
