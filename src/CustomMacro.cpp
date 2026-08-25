#include "pch_core.hpp"
#include "CustomMacro.hpp"
#include "interface/IMeasurementSink.hpp"
#include "Logger.hpp"
#include "Utils.hpp"
#include "MacroKeyFrame.hpp"

/* The macro engine: parsing a macro file into actions, watching the keypad
   stream, and running the actions a key is bound to.

   The action classes themselves live in MacroActions.cpp, the two scan-code
   tables in KeyScanCodes.cpp, and the ini binding in MacroSettings.cpp. This
   file was 1079 lines holding all four.
*/

bool CustomMacro::IsKeyReserved(const std::string& key_code) const
{
    if(key_code == bring_to_foreground_key)
    {
        LOG(LogLevel::Warning, "Key \"{}\" is already assigned to BringToForeground!", key_code);
        return true;
    }
    if(!m_Hotkeys)
        return false;

    const std::string_view owner = m_Hotkeys->OwnerOfBinding(key_code);
    if(owner.empty())
        return false;

    LOG(LogLevel::Warning, "Key \"{}\" is already assigned to {}!", key_code, owner);
    return true;
}

void CustomMacro::ParseMacroKeys(size_t id, const std::string& key_code, std::string& str, std::unique_ptr<MacroAppProfile>& c, MacroFlags flags)
{
    if(IsKeyReserved(key_code))
        return;

    size_t pos = 1;
    while(pos < str.length() - 1)
    {
        const size_t first_end = str.find("]", pos + 1);
        const std::string head = str.substr(0, first_end);

        /* Exactly one keyword may introduce this element; two means the line is
           ambiguous and none means it is malformed. */
        const macro_keywords::Keyword* matched = nullptr;
        size_t matched_pos = std::string::npos;
        size_t match_count = 0;
        for(const macro_keywords::Keyword& keyword : macro_keywords::kAll)
        {
            const size_t found = head.find(keyword.token, pos - 1);
            if(found == std::string::npos)
                continue;
            matched = &keyword;
            matched_pos = found;
            ++match_count;
        }

        if(match_count != 1)
        {
            LOG(LogLevel::Error, "Error with config file macro formatting: {}", str);
            return;
        }

        pos = first_end;
        std::string sequence = utils::extract_string(str, matched_pos, first_end, matched->token.size());

        if(matched->token == macro_keywords::kBindNameToken)
        {
            c->bind_name[key_code] = std::move(sequence);
            continue;
        }

        try
        {
            c->key_vec[key_code].push_back(matched->create(std::move(sequence)));
        }
        catch(const std::exception& e)
        {
            LOG(LogLevel::Error, "Invalid argument for {}: {}", matched->token, e.what());
        }
    }
    c->flags[key_code] = flags;

    if(c->bind_name[key_code].empty())
    {
        c->bind_name[key_code] = "Unknown macro"; /* wxTreeList won\'t show empty string as row */
        LOG(LogLevel::Warning, "Macro name for key {} missing. Giving it \'Unknown macro\', feel free to change it.", key_code);
    }
}
namespace
{
/* One row per modifier, in the order they are appended to the key string.
   This was two four-arm else-if chains, and the chains had two consequences
   nothing wrote down:

     - a second modifier on the same side was dropped, so LCTRL+LSHIFT held
       together produced "LSHIFT" and a binding naming both could never fire;
     - one modifier from each side was appended with no separator between them,
       so LSHIFT+RSHIFT produced "LSHIFTRSHIFT", which is not a key name
       anything can bind.

   The order within each side - shift, alt, gui, ctrl - and left before right
   is the order the chains appended in, and key strings are order sensitive. */
constexpr std::pair<bool macro_key_frame::Frame::*, std::string_view> kModifierNames[] = {
    { &macro_key_frame::Frame::lshift, "LSHIFT" },
    { &macro_key_frame::Frame::lalt,   "LALT" },
    { &macro_key_frame::Frame::lgui,   "LGUI" },
    { &macro_key_frame::Frame::lctrl,  "LCTRL" },
    { &macro_key_frame::Frame::rshift, "RSHIFT" },
    { &macro_key_frame::Frame::ralt,   "RALT" },
    { &macro_key_frame::Frame::rgui,   "RGUI" },
    { &macro_key_frame::Frame::rctrl,  "RCTRL" },
};

// !\brief Append `token` to `keys`, inserting the separator when one is due.
void AppendKeyToken(std::string& keys, std::string_view token)
{
    if(!keys.empty() && keys.back() != '+')
        keys += '+';
    keys += token;
}
}

void CustomMacro::SimulateKeypress(const std::string& key, bool directly_execute_alarm)
{
    /* A simulated press is already a whole key, so it does not go through the
       accumulator - which also stops it destroying a keypad combination that
       happens to be half-built. It used to assign pressed_keys, run under the
       lock, and clear. */
    const std::string alarm_key = ExecuteKeypresses(key, directly_execute_alarm);

    if(!alarm_key.empty() && m_AlarmHandler)
        m_AlarmHandler(alarm_key);
}

void CustomMacro::ProcessReceivedData(std::string_view data)
{
    /* Everything here is bounded by data.size(): these bytes come off a serial
       port, so the length is whatever the read delivered. The buffer behind
       them is reused between frames, and reading past the frame used to pick up
       the tail of the previous one. See ADR-0003. */
    const auto log_buffer = [&data] {
        std::string hex;
        utils::ConvertHexBufferToString(data.data(), data.size(), hex);
        LOG(LogLevel::Verbose, "Full Data buffer: {}", hex);
    };

    if(macro_key_frame::IsMeasurementFrame(data))
    {
        if(m_Context.measurements)
            m_Context.measurements->HandleIncomingMeasurements(data.data(), data.size(), "SERIAL");
        return;
    }

    const auto parsed = macro_key_frame::Parse(data);
    if(parsed.error == macro_key_frame::ParseError::WrongLength)
    {
        LOG(LogLevel::Verbose, "Data received with invalid length! ({}), expected: {}",
            data.size(), macro_key_frame::frame_size);
        log_buffer();
        return;
    }

    /* The accumulator is shared - the keypad's serial thread and the Corsair
       HID thread both arrive here - so building it up is a critical section.
       Running what it names is not, and used to be inside the same one: a macro
       can hold DELAY[3000] and a CreateProcess, so a keypress from one device
       waited on a macro started from the other. The lock now covers the
       accumulation and hands out a snapshot.

       nullopt means there is nothing to run, which is what the early exits below
       used to say by returning an empty alarm key. An empty *string* still runs,
       because that is what the old code did when no scan code matched. */
    const std::optional<std::string> keys_to_run = [&]() -> std::optional<std::string>
    {
        std::scoped_lock lock(executor_mtx);

        if(macro_key_frame::IsResetFrame(data)) /* in case of sudden STM32 reset */
        {
            LOG(LogLevel::Verbose, "Reset received");
            pressed_keys.clear();
        }

        if(parsed.error == macro_key_frame::ParseError::Crc)
        {
            LOG(LogLevel::Verbose, "CRC mismatch, received {:X} != {:X}",
                parsed.received_crc, parsed.computed_crc);
            log_buffer();
            return std::nullopt;
        }

        const auto& k = parsed.frame;
        if(macro_key_frame::IsAllReleased(k))
        {
            pressed_keys.clear();
            return std::nullopt;
        }

        if(!pressed_keys.empty() && pressed_keys[pressed_keys.length() - 1] != '+')
            pressed_keys += '+';

        if(advanced_key_binding)
        {
            /* stop when only modifier keys are pressed */
            if(std::ranges::all_of(k.keys, [](std::uint8_t key) { return key == 0; }))
                return std::nullopt;
        }

        for(const auto& [flag, name] : kModifierNames)
        {
            if(k.*flag)
                AppendKeyToken(pressed_keys, name);
        }

        /* One addon key, not k.keys.size() of them: 700+ macros per
           application is enough and nothing has asked for more. This was a
           for(int i = 0; i != 1; i++) loop, which said the same thing by
           running exactly once. */
        const auto key_str = hid_scan_codes.find(k.keys[0]);
        if(key_str != hid_scan_codes.end())
            AppendKeyToken(pressed_keys, key_str->second);
        return pressed_keys;
    }();

    if(!keys_to_run)
        return;

    const std::string alarm_key = ExecuteKeypresses(*keys_to_run);
    if(!alarm_key.empty() && m_AlarmHandler)
        m_AlarmHandler(alarm_key);
}

uint16_t CustomMacro::GetKeyScanCode(const std::string& str)
{
    auto it = scan_codes.find(str);
    return it != scan_codes.end() ? static_cast<uint16_t>(it->second) : uint16_t{0xFFFF};
}

std::string CustomMacro::GetKeyStringFromScanCode(int scancode)
{
    auto it = std::ranges::find_if(scan_codes, [scancode](const auto& pair) { return pair.second == scancode; });
    return it != scan_codes.end() ? it->first : "INVALID";
}

std::string CustomMacro::ExecuteKeypresses(const std::string& keys, bool directly_execute_alarm)
{
    /* Runs without executor_mtx, from a snapshot the caller took under it. */

    /* Features get first refusal on the key. This used to be an if-chain naming
       six services; the chain is now a registry the composition root fills. */
    if(m_Hotkeys && m_Hotkeys->Dispatch(keys))
        return {};

    if(keys == bring_to_foreground_key)
    {
        ExecuteForegroundKeypress();
        return {};
    }

    /* Handed back for the caller to run once executor_mtx is released. Arming
       an alarm asks the user for a duration, which blocks on a modal dialog,
       and it reaches the alarm handler's own lock - neither belongs under this
       one. */
    std::string alarm_key;

    auto ExecuteGlobalMacro = [this, &keys, &directly_execute_alarm, &alarm_key]()
    {
        if(macros.empty())
            return;

        const auto it = macros[0]->key_vec.find(keys);
        if(it != macros[0]->key_vec.end())
        {
            if(macros[0]->flags[keys] == MacroFlags::Alarm && !directly_execute_alarm)
            {
                alarm_key = keys;
                return;
            }
            for(const auto& i : it->second)
                i->Execute(m_Context);
        }
    };

    /* True when an application profile claimed the key, which is the only
       case where the global macros are not consulted. */
    auto RunPerAppMacro = [this, &keys]() -> bool
    {
        if(!use_per_app_macro)
            return false;

#ifdef _WIN32
        HWND foreground = GetForegroundWindow();
        if(!foreground)
            return false;

        std::array<char, 256> window_title{};
        GetWindowTextA(foreground, window_title.data(), static_cast<int>(window_title.size()));
        const std::string_view title{ window_title.data() };

        for(auto& m : macros)
        {
            if(m->app_name.length() <= 2 || title.find(m->app_name) == std::string_view::npos)
                continue;

            /* Unlike the global path below, this does not consult flags, so an
               alarm bound inside an application profile executes immediately
               instead of being handed back to be armed. That is what it did
               before this was untangled; it does not look deliberate. */
            const auto it = m->key_vec.find(keys);
            if(it != m->key_vec.end())
            {
                for(const auto& i : it->second)
                    i->Execute(m_Context);
            }

            /* Claimed by matching the window, whether or not this key was
               bound in it - which is what the app_found flag meant. */
            return true;
        }
        return false;
#else
        return false;
#endif
    };

    /* ExecuteGlobalMacro used to be called from four places, all of them
       meaning the same thing: no application profile claimed this key. They
       were reached when no profile matched the foreground window, when there
       was no foreground window, when the platform is not Windows, and when
       per-application macros are switched off. */
    if(!RunPerAppMacro())
        ExecuteGlobalMacro();

    return alarm_key;
}

void CustomMacro::ExecuteForegroundKeypress()
{
    /* Showing or hiding the window is the application's business, not the macro
       engine's; the composition root supplies the action. */
    if(m_ForegroundToggle)
        m_ForegroundToggle();
}
