#pragma once

#include "utils/CSingleton.hpp"

#include "Settings.hpp"

#include <inttypes.h>
#include <unordered_map>
#include <map>
#include <string>
#include <string_view>
#include <variant>
#include <array>
#include <filesystem>

#include "Logger.hpp"
#include <thread>

#include <boost/tokenizer.hpp>
#include <random>

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

#pragma pack(push, 1)
typedef struct
{
    uint8_t state;
    uint8_t lctrl;
    uint8_t lshift;
    uint8_t lalt;
    uint8_t lgui;
    uint8_t rctrl;
    uint8_t rshift;
    uint8_t ralt;
    uint8_t rgui;
    uint8_t keys[6];
    uint16_t crc;
} KeyData_t;
#pragma pack(pop)

enum MacroTypes : uint8_t
{
    BIND_NAME, KEY_SEQ, KEY_TYPE, DELAY, MOUSE_MOVE, MOUSE_INTERPOLATE, MOUSE_PRESS, MOUSE_RELEASE, MOUSE_CLICK, BASH, CMD, CMD_XML, CMD_FG, CMD_IMG, MAX
};

enum class TextFormat { Plain, Ini };

class IKey
{
public:
    IKey() = default;
    IKey(const IKey&) = default;
    virtual ~IKey() = default;

    virtual std::unique_ptr<IKey> Clone() const = 0;
    virtual void Execute() = 0;
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
    void Execute() override;
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
    void Execute() override;
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
    void Execute() override;
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
    void Execute() override;
    const char* GetName() const override { return "MOUSE MOVE"; }
};

class MouseInterpolate final : public MousePositionAction
{
public:
    MouseInterpolate(LPPOINT* pos_) : MousePositionAction(**pos_, "MOUSE_INTERPOLATE") {}
    MouseInterpolate(std::string&& str) : MousePositionAction(std::move(str), "MOUSE_INTERPOLATE") {}
    MouseInterpolate(const MouseInterpolate& from) : MousePositionAction(from) {}
    std::unique_ptr<IKey> Clone() const override { return std::make_unique<MouseInterpolate>(*this); }
    void Execute() override;
    const char* GetName() const override { return "MOUSE INTERPOLATE"; }
};

class MousePress final : public MouseButtonAction
{
public:
    MousePress(uint16_t key_) : MouseButtonAction(key_, "MOUSE_PRESS") {}
    MousePress(std::string&& str) : MouseButtonAction(std::move(str), "MOUSE_PRESS") {}
    MousePress(const MousePress& from) : MouseButtonAction(from) {}
    std::unique_ptr<IKey> Clone() const override { return std::make_unique<MousePress>(*this); }
    void Execute() override;
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
    void Execute() override;
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
    void Execute() override;
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
    void Execute() override;
    const char* GetName() const override { return "BASH"; }
};

class CommandExecute final : public StringCommand
{
public:
    CommandExecute(std::string cmd_) : StringCommand(std::move(cmd_), "CMD") {}
    CommandExecute(const CommandExecute& from) : StringCommand(from) {}
    std::unique_ptr<IKey> Clone() const override { return std::make_unique<CommandExecute>(*this); }
    void Execute() override;
    const char* GetName() const override { return "CMD"; }
};

class CommandXml final : public StringCommand
{
public:
    CommandXml(std::string cmd_) : StringCommand(std::move(cmd_), "CMD_XML") {}
    CommandXml(const CommandXml& from) : StringCommand(from) {}
    std::unique_ptr<IKey> Clone() const override { return std::make_unique<CommandXml>(*this); }
    void Execute() override;
    const char* GetName() const override { return "CMD_XML"; }
};

class KeyBringAppToForeground final : public IKey
{
public:
    KeyBringAppToForeground(std::string app_, std::string title_) : app(std::move(app_)), title(std::move(title_)) {}
    KeyBringAppToForeground(const KeyBringAppToForeground& from) : app(from.app), title(from.title) {}
    KeyBringAppToForeground(std::string&& str);
    std::unique_ptr<IKey> Clone() const override { return std::make_unique<KeyBringAppToForeground>(*this); }
    void Execute() override;
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
    void Execute() override;
    std::string GenerateText(TextFormat fmt) const override;
    const char* GetName() const override { return "CMD_IMG"; }
    std::filesystem::path GetImagePath() const { return image_path; }
    POINT GetOffset() const { return offset; }
private:
    std::filesystem::path image_path;
    POINT offset = {};
};

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

class CustomMacro : public CSingleton < CustomMacro >
{
    friend class CSingleton < CustomMacro >;

public:
    CustomMacro() = default;
    ~CustomMacro() = default;

    void ParseMacroKeys(size_t id, const std::string& key_code, std::string& str, std::unique_ptr<MacroAppProfile>& c, MacroFlags flags);
    void SimulateKeypress(const std::string& key, bool directly_execute_alarm = false);
    void ProcessReceivedData(const char* data, unsigned int len);

    std::vector<std::unique_ptr<MacroAppProfile>>& GetMacros() { return macros; }
    uint16_t GetKeyScanCode(const std::string& str);
    std::string GetKeyStringFromScanCode(int scancode);
    const std::unordered_map<std::string, int>& GetHidScanCodeMap() { return scan_codes; }

    bool use_per_app_macro = true;
    bool advanced_key_binding = true;
    std::string bring_to_foreground_key = "N/A";
    std::vector<std::unique_ptr<IKey>>* editing_macro = nullptr;
    IKey* editing_item = nullptr;

private:
    friend class Settings;

    bool IsKeyReserved(const std::string& key_code) const;
    void ExecuteKeypresses(bool directly_execute_alarm = false);
    void ExecuteForegroundKeypress();

    std::string pressed_keys;
    std::mutex executor_mtx;
    std::vector<std::unique_ptr<MacroAppProfile>> macros;
    static const std::unordered_map<std::string, int> scan_codes;
    static const std::unordered_map<int, std::string> hid_scan_codes;
};
