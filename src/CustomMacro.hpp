#pragma once

#include "utils/CSingleton.hpp"

#include "Settings.hpp"

#include <inttypes.h>
#include <array>
#include <filesystem>
#include <functional>
#include <iosfwd>
#include <map>
#include <random>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <variant>

#include <boost/tokenizer.hpp>

#include "HotkeyRegistry.hpp"
#include "Logger.hpp"
#include "interface/ISettingsBinding.hpp"
#include "interface/IKeySink.hpp"

/* MacroSettings derives from ISettingsBinding and CustomMacro holds a
   HotkeyRegistry*, on every platform. Those two headers, <functional> and
   <iosfwd> used to sit inside this #ifdef alongside <windows.h>, so a non-
   Windows build only compiled because the precompiled header happened to pull
   them in first. Only the genuinely Windows-only headers are guarded. */
#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

#ifndef _WINDEF_
typedef struct tagPOINT
{
    long  x;
    long  y;
} POINT, * PPOINT, *NPPOINT, * LPPOINT;
#endif

/* The keypad frame layout used to live here as a packed struct the received
   bytes were reinterpreted as. It is decoded field by field in
   macro_key_frame instead, which does not depend on the host's byte order and
   can be tested and fuzzed without the GUI. */

enum class TextFormat { Plain, Ini };

class SettingsWriter;
class ICmdExecutor;
class IScreenAutomation;
class IMeasurementSink;

// !\brief What a macro action needs from the rest of the application.
//
// Command objects used to reach the running application directly, which is why
// a macro action could not be executed, or tested, without a wxWidgets app
// object. The collaborators are handed in instead; an action that needs none
// simply ignores the context.
struct MacroContext
{
    ICmdExecutor* command_executor = nullptr;

    /* The screen and the sensor pipeline, for the two actions that need them.
       Reaching ImageRecognition and Sensors directly was the only dependency
       pointing from the macro engine up towards a feature. */
    IScreenAutomation* screen = nullptr;
    IMeasurementSink* measurements = nullptr;
};

class IKey
{
public:
    IKey() = default;
    IKey(const IKey&) = default;
    virtual ~IKey() = default;

    virtual std::unique_ptr<IKey> Clone() const = 0;
    virtual void Execute(const MacroContext& context) = 0;
    virtual std::string GenerateText(TextFormat fmt) const = 0;
    virtual const char* GetName() const = 0;
};

// Base for MousePress / MouseRelease / MouseClick — shared string parsing and INI text generation
class MouseButtonAction : public IKey
{
public:
    uint16_t GetKey() const { return key; }
    std::string GenerateText(TextFormat fmt) const override;
protected:
    MouseButtonAction(uint16_t key_, std::string_view ini_prefix) : key(key_), ini_prefix_(ini_prefix) {}
    MouseButtonAction(std::string&& str, std::string_view ini_prefix);
    MouseButtonAction(const MouseButtonAction& from) : key(from.key), ini_prefix_(from.ini_prefix_) {}
    static std::string ButtonToString(uint16_t key);
    uint16_t key = 0;
private:
    std::string_view ini_prefix_;
};

// Base for MouseMovement / MouseInterpolate — shared x,y string parsing and INI text generation
class MousePositionAction : public IKey
{
public:
    POINT& GetPos() { return m_pos; }
    std::string GenerateText(TextFormat fmt) const override;
protected:
    MousePositionAction(std::string&& str, std::string_view ini_prefix);
    MousePositionAction(const POINT& pos, std::string_view ini_prefix) : m_pos(pos), ini_prefix_(ini_prefix) {}
    MousePositionAction(const MousePositionAction& from) : m_pos(from.m_pos), ini_prefix_(from.ini_prefix_) {}
    POINT m_pos = {};
private:
    std::string_view ini_prefix_;
};

// Base for BashCommand / CommandExecute / CommandXml — shared string storage and INI text generation
class StringCommand : public IKey
{
public:
    const std::string& GetCmd() const { return cmd; }
    std::string GenerateText(TextFormat fmt) const override;
protected:
    explicit StringCommand(std::string&& cmd_, std::string_view ini_prefix) : cmd(std::move(cmd_)), ini_prefix_(ini_prefix) {}
    StringCommand(const StringCommand& from) : cmd(from.cmd), ini_prefix_(from.ini_prefix_) {}
    std::string cmd;
private:
    std::string_view ini_prefix_;
};

// --- Concrete macro action types ---

class KeyText final : public IKey
{
public:
    KeyText(std::string&& keys) : seq(std::move(keys)) {}
    KeyText(const KeyText& from) : seq(from.seq) {}
    std::unique_ptr<IKey> Clone() const override { return std::make_unique<KeyText>(*this); }
    void Execute(const MacroContext& context) override;
    std::string GenerateText(TextFormat fmt) const override;
    const char* GetName() const override { return "TEXT"; }
    std::string& GetString() { return seq; }
private:
#ifdef _WIN32
    void TypeCharacter(uint16_t character);
#endif
    std::string seq;
};

class KeyCombination final : public IKey
{
public:
    KeyCombination(std::vector<uint16_t>&& keys) : seq(std::move(keys)) {}
    KeyCombination(std::string&& str);
    KeyCombination(const KeyCombination& from) : seq(from.seq) {}
    std::unique_ptr<IKey> Clone() const override { return std::make_unique<KeyCombination>(*this); }
    void Execute(const MacroContext& context) override;
    std::string GenerateText(TextFormat fmt) const override;
    const char* GetName() const override { return "SEQUENCE"; }
    std::vector<uint16_t>& GetVec() { return seq; }
private:
    void PressReleaseKey(uint16_t scancode, bool press = true);
    std::vector<uint16_t> seq;
};

class KeyDelay final : public IKey
{
public:
    KeyDelay(uint32_t delay_) : delay(delay_) {}
    KeyDelay(uint32_t start_, uint32_t end_) : delay(std::array<uint32_t, 2>{start_, end_}) {}
    KeyDelay(std::string&& str);
    KeyDelay(const KeyDelay& from) : delay(from.delay) {}
    std::unique_ptr<IKey> Clone() const override { return std::make_unique<KeyDelay>(*this); }
    void Execute(const MacroContext& context) override;
    std::string GenerateText(TextFormat fmt) const override;
    const char* GetName() const override { return std::holds_alternative<uint32_t>(delay) ? "DELAY" : "DELAY RANDOM"; }
    std::variant<uint32_t, std::array<uint32_t, 2>>& GetDelay() { return delay; }
private:
    std::variant<uint32_t, std::array<uint32_t, 2>> delay;
};

class MouseMovement final : public MousePositionAction
{
public:
    MouseMovement(LPPOINT* pos_) : MousePositionAction(**pos_, "MOUSE_MOVE") {}
    MouseMovement(std::string&& str) : MousePositionAction(std::move(str), "MOUSE_MOVE") {}
    MouseMovement(const MouseMovement& from) : MousePositionAction(from) {}
    std::unique_ptr<IKey> Clone() const override { return std::make_unique<MouseMovement>(*this); }
    void Execute(const MacroContext& context) override;
    const char* GetName() const override { return "MOUSE MOVE"; }
};

class MouseInterpolate final : public MousePositionAction
{
public:
    MouseInterpolate(LPPOINT* pos_) : MousePositionAction(**pos_, "MOUSE_INTERPOLATE") {}
    MouseInterpolate(std::string&& str) : MousePositionAction(std::move(str), "MOUSE_INTERPOLATE") {}
    MouseInterpolate(const MouseInterpolate& from) : MousePositionAction(from) {}
    std::unique_ptr<IKey> Clone() const override { return std::make_unique<MouseInterpolate>(*this); }
    void Execute(const MacroContext& context) override;
    const char* GetName() const override { return "MOUSE INTERPOLATE"; }
};

class MousePress final : public MouseButtonAction
{
public:
    MousePress(uint16_t key_) : MouseButtonAction(key_, "MOUSE_PRESS") {}
    MousePress(std::string&& str) : MouseButtonAction(std::move(str), "MOUSE_PRESS") {}
    MousePress(const MousePress& from) : MouseButtonAction(from) {}
    std::unique_ptr<IKey> Clone() const override { return std::make_unique<MousePress>(*this); }
    void Execute(const MacroContext& context) override;
    const char* GetName() const override { return "MOUSE PRESS"; }
private:
    void PressMouse(uint16_t mouse_button);
};

class MouseRelease final : public MouseButtonAction
{
public:
    MouseRelease(uint16_t key_) : MouseButtonAction(key_, "MOUSE_RELEASE") {}
    MouseRelease(std::string&& str) : MouseButtonAction(std::move(str), "MOUSE_RELEASE") {}
    MouseRelease(const MouseRelease& from) : MouseButtonAction(from) {}
    std::unique_ptr<IKey> Clone() const override { return std::make_unique<MouseRelease>(*this); }
    void Execute(const MacroContext& context) override;
    const char* GetName() const override { return "MOUSE RELEASE"; }
private:
    void ReleaseMouse(uint16_t mouse_button);
};

class MouseClick final : public MouseButtonAction
{
public:
    MouseClick(uint16_t key_) : MouseButtonAction(key_, "MOUSE_CLICK") {}
    MouseClick(std::string&& str) : MouseButtonAction(std::move(str), "MOUSE_CLICK") {}
    MouseClick(const MouseClick& from) : MouseButtonAction(from) {}
    std::unique_ptr<IKey> Clone() const override { return std::make_unique<MouseClick>(*this); }
    void Execute(const MacroContext& context) override;
    const char* GetName() const override { return "MOUSE CLICK"; }
private:
    void PressReleaseMouse(uint16_t mouse_button);
};

class BashCommand final : public StringCommand
{
public:
    BashCommand(std::string cmd_) : StringCommand(std::move(cmd_), "BASH") {}
    BashCommand(const BashCommand& from) : StringCommand(from) {}
    std::unique_ptr<IKey> Clone() const override { return std::make_unique<BashCommand>(*this); }
    void Execute(const MacroContext& context) override;
    const char* GetName() const override { return "BASH"; }
};

class CommandExecute final : public StringCommand
{
public:
    CommandExecute(std::string cmd_) : StringCommand(std::move(cmd_), "CMD") {}
    CommandExecute(const CommandExecute& from) : StringCommand(from) {}
    std::unique_ptr<IKey> Clone() const override { return std::make_unique<CommandExecute>(*this); }
    void Execute(const MacroContext& context) override;
    const char* GetName() const override { return "CMD"; }
};

class CommandXml final : public StringCommand
{
public:
    CommandXml(std::string cmd_) : StringCommand(std::move(cmd_), "CMD_XML") {}
    CommandXml(const CommandXml& from) : StringCommand(from) {}
    std::unique_ptr<IKey> Clone() const override { return std::make_unique<CommandXml>(*this); }
    void Execute(const MacroContext& context) override;
    const char* GetName() const override { return "CMD_XML"; }
};

class KeyBringAppToForeground final : public IKey
{
public:
    KeyBringAppToForeground(std::string app_, std::string title_) : app(std::move(app_)), title(std::move(title_)) {}
    KeyBringAppToForeground(const KeyBringAppToForeground& from) : app(from.app), title(from.title) {}
    KeyBringAppToForeground(std::string&& str);
    std::unique_ptr<IKey> Clone() const override { return std::make_unique<KeyBringAppToForeground>(*this); }
    void Execute(const MacroContext& context) override;
    std::string GenerateText(TextFormat fmt) const override;
    const char* GetName() const override { return "CMD_FG"; }
    const std::string& GetApp() const { return app; }
    const std::string& GetTitle() const { return title; }
private:
    std::string app;
    std::string title;
};

class KeyFindImageOnScreen final : public IKey
{
public:
    KeyFindImageOnScreen(std::string&& str);
    KeyFindImageOnScreen(const KeyFindImageOnScreen& from) : image_path(from.image_path), offset(from.offset) {}
    std::unique_ptr<IKey> Clone() const override { return std::make_unique<KeyFindImageOnScreen>(*this); }
    void Execute(const MacroContext& context) override;
    std::string GenerateText(TextFormat fmt) const override;
    const char* GetName() const override { return "CMD_IMG"; }
    std::filesystem::path GetImagePath() const { return image_path; }
    POINT GetOffset() const { return offset; }
private:
    std::filesystem::path image_path;
    POINT offset = {};
};

namespace macro_keywords
{
// !\brief One row per keyword the macro language understands.
//
// This lived in CustomMacro.cpp as a token-and-factory pair, while
// MacroSettings restated the same fourteen keywords by hand as sixteen ini
// comment lines - a list whose own comment said it was waiting to be linked to
// this one. Adding a keyword was two edits in two files with nothing checking
// they agreed. It is one row now.
//
// The order is the order the ini help is written in, which is why KEY_TYPE
// comes before KEY_SEQ: it is the order settings.example.ini already ships.
// Parsing does not depend on it - every token is distinct, and a line that
// matches none or more than one is rejected either way.
struct Keyword
{
    // !\brief The bracketed token that introduces this keyword.
    std::string_view token;

    // !\brief Builds the action. Null for BIND_NAME, which names the macro
    // rather than adding an action to it.
    std::unique_ptr<IKey> (*create)(std::string&&);

    // !\brief The help line written into settings.ini.
    std::string_view help;

    // !\brief A second help line, for the one keyword that takes two forms.
    std::string_view help_alternate = {};
};

template<class T>
std::unique_ptr<IKey> MakeAction(std::string&& text)
{
    return std::make_unique<T>(std::move(text));
}

inline constexpr std::string_view kBindNameToken = "BIND_NAME[";

inline constexpr std::array kAll{
    Keyword{ kBindNameToken, nullptr,
        "BIND_NAME[binding name] = Set the name if macro. Should be used as first" },
    Keyword{ "KEY_TYPE[", &MakeAction<KeyText>,
        "KEY_TYPE[text] = Press & release given keys in sequence to type a text" },
    Keyword{ "KEY_SEQ[", &MakeAction<KeyCombination>,
        "KEY_SEQ[CTRL+C] = Press all given keys after each other and release it when each was pressed - ideal for key shortcats" },
    Keyword{ "DELAY[", &MakeAction<KeyDelay>,
        "DELAY[time in ms] = Waits for given milliseconds",
        "DELAY[min ms - max ms] = Waits randomly between min ms and max ms" },
    Keyword{ "MOUSE_MOVE[", &MakeAction<MouseMovement>,
        "MOUSE_MOVE[x,y] = Move mouse to given coordinates" },
    Keyword{ "MOUSE_INTERPOLATE[", &MakeAction<MouseInterpolate>,
        "MOUSE_INTERPOLATE[x,y] = Move mouse with interpolation to given coordinates" },
    Keyword{ "MOUSE_PRESS[", &MakeAction<MousePress>,
        "MOUSE_PRESS[key] = Press given mouse key" },
    Keyword{ "MOUSE_RELEASE[", &MakeAction<MouseRelease>,
        "MOUSE_RELEASE[key] = Release given mouse key" },
    Keyword{ "MOUSE_CLICK[", &MakeAction<MouseClick>,
        "MOUSE_CLICK[key] = Click (press and release) with mouse" },
    Keyword{ "BASH[", &MakeAction<BashCommand>,
        "BASH[key] = Execute specified command(s) with command line and keeps terminal shown" },
    Keyword{ "CMD[", &MakeAction<CommandExecute>,
        "CMD[key] = Execute specified command(s) with command line without terminal" },
    Keyword{ "CMD_XML[", &MakeAction<CommandXml>,
        "CMD_XML[PageName+CommandName] = Execute predefined command from Cmds.xml" },
    Keyword{ "CMD_FG[", &MakeAction<KeyBringAppToForeground>,
        "CMD_FG[app_name.exe,Window title name] = Bring specified app with given title to the foreground" },
    Keyword{ "CMD_IMG[", &MakeAction<KeyFindImageOnScreen>,
        "CMD_IMG[path_to_image,offset x,offset y] = Scan for given image on screen and clicks on it if found" },
};
}

enum class MacroFlags
{
    None,
    Alarm,
};

struct MacroAppProfile
{
    MacroAppProfile() = default;
    explicit MacroAppProfile(std::string&& name) : app_name(std::move(name)) {}

    std::map<std::string, std::vector<std::unique_ptr<IKey>>> key_vec;
    std::map<std::string, std::string> bind_name;
    std::map<std::string, MacroFlags> flags;
    std::string app_name;
};

class CustomMacro : public CSingleton < CustomMacro >, public IKeySink
{
    friend class CSingleton < CustomMacro >;

public:

    CustomMacro() = default;
    ~CustomMacro() = default;

    // IKeySink - the two operations a keypad driver needs, named for the port
    // so a driver does not have to know what is behind them.
    void OnKeypadData(std::string_view data) override { ProcessReceivedData(data); }
    void OnKeyPressed(std::string_view key) override { SimulateKeypress(std::string(key)); }

    void ParseMacroKeys(size_t id, const std::string& key_code, std::string& str, std::unique_ptr<MacroAppProfile>& c, MacroFlags flags);

    // !\brief The global hotkey chain consulted before any macro runs.
    //
    // Both "does this key already belong to a feature" and "which feature
    // handles this press" are answered from it, so the two can no longer give
    // different answers.
    void SetHotkeyRegistry(HotkeyRegistry* registry) { m_Hotkeys = registry; }

    // !\brief Collaborators a macro action may need. Supplied by the
    // composition root rather than looked up from inside the action.
    void SetMacroContext(MacroContext context) { m_Context = context; }

    // !\brief Where a macro flagged as an alarm is sent instead of executed.
    using AlarmHandler = std::function<void(const std::string&)>;
    void SetAlarmHandler(AlarmHandler handler) { m_AlarmHandler = std::move(handler); }

    // !\brief What BringToForegroundKey does. Owning a window is not this
    // class's job, so the action is handed in.
    using ForegroundToggle = std::function<void()>;
    void SetForegroundToggle(ForegroundToggle toggle) { m_ForegroundToggle = std::move(toggle); }
    void SimulateKeypress(const std::string& key, bool directly_execute_alarm = false);
    void ProcessReceivedData(std::string_view data);

    std::vector<std::unique_ptr<MacroAppProfile>>& GetMacros() { return macros; }
    [[nodiscard]] const std::vector<std::unique_ptr<MacroAppProfile>>& GetMacros() const { return macros; }

    // !\brief Replace the loaded macro set. The settings binding uses these, so
    // Settings no longer needs to be a friend of this class.
    void ResetMacros() { macros.clear(); }
    void AddMacroProfile(std::unique_ptr<MacroAppProfile> profile) { macros.push_back(std::move(profile)); }
    /* Both read the static scan-code table and nothing else, so a macro action
       translating a key name does not need an instance - which is what it was
       fetching the singleton for. */
    static uint16_t GetKeyScanCode(const std::string& str);
    static std::string GetKeyStringFromScanCode(int scancode);

    bool use_per_app_macro = true;
    bool advanced_key_binding = true;
    std::string bring_to_foreground_key = "N/A";

private:
    bool IsKeyReserved(const std::string& key_code) const;

    // !\brief Run the macro bound to `keys`.
    //
    // Takes the key rather than reading the pressed_keys accumulator, and must
    // NOT be called with executor_mtx held. A macro's actions include DELAY[]
    // sleeps of up to several seconds and CreateProcess; running them under the
    // lock that guards the accumulator made a keypress from one device wait for
    // a macro started by another.
    // !\return The key whose alarm the caller should arm, or empty when the press
    //          was fully handled here. Arming blocks on a modal dialog and
    //          reaches the alarm handler's own lock, so it stays with the caller.
    [[nodiscard]] std::string ExecuteKeypresses(const std::string& keys,
        bool directly_execute_alarm = false);
    void ExecuteForegroundKeypress();

    HotkeyRegistry* m_Hotkeys = nullptr;
    MacroContext m_Context;
    AlarmHandler m_AlarmHandler;
    ForegroundToggle m_ForegroundToggle;
    // !\brief The keys held down, accumulated across keypad frames.
    //
    // Guarded by executor_mtx, which the keypad's serial thread and the Corsair
    // HID thread both reach. That is all executor_mtx guards: `macros` is
    // published through GetMacros/ResetMacros/AddMacroProfile without it.
    std::string pressed_keys;
    std::mutex executor_mtx;
    std::vector<std::unique_ptr<MacroAppProfile>> macros;
    static const std::unordered_map<std::string, int> scan_codes;
    static const std::unordered_map<int, std::string> hid_scan_codes;
};

class ScriptLauncher;

// !\brief The [Macro_Config], [Keys_Global] and [Keys_MacroN] blocks.
//
// The macro keys belong to CustomMacro and ScriptLauncherKey to ScriptLauncher,
// so the binding is a small collaborator of both rather than a member of either.
class MacroSettings : public ISettingsBinding
{
public:
    MacroSettings(CustomMacro& macros, ScriptLauncher& launcher) :
        m_Macros(macros), m_Launcher(launcher) {}

    [[nodiscard]] std::string_view SettingsSection() const override { return "Macro_Config"; }
    void LoadSettings(SettingsReader& reader) override;
    void SaveSettings(std::ostream& out) const override;
    void SaveDefaultSettings(std::ostream& out) const override;

private:
    // !\brief The keyword help and the [Macro_Config] block, which both
    // spellings of this binding write identically.
    void SaveMacroConfig(SettingsWriter& writer) const;

    CustomMacro& m_Macros;
    ScriptLauncher& m_Launcher;
};
