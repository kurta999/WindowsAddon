#include "pch_core.hpp"
#include "CustomMacro.hpp"
#include "interface/IScreenAutomation.hpp"
#include "interface/ICmdExecutor.hpp"
#include "utils/WindowsCommand.hpp"
#include "Logger.hpp"
#include "Utils.hpp"

/* The macro action classes: one per keyword the macro language understands.
   They share only the IKey interface - each parses its own bracketed argument
   in its constructor and does its own thing in Execute - so they had no reason
   to sit in the same file as the engine that runs them. */

// --- MouseButtonAction base ---

MouseButtonAction::MouseButtonAction(std::string&& str, std::string_view ini_prefix) : ini_prefix_(ini_prefix)
{
    uint16_t mouse_button = 0xFFFF;
#ifdef _WIN32
    if(str == "L" || str == "LEFT")
        mouse_button = MOUSEEVENTF_LEFTDOWN;
    else if(str == "R" || str == "RIGHT")
        mouse_button = MOUSEEVENTF_RIGHTDOWN;
    else if(str == "M" || str == "MIDDLE")
        mouse_button = MOUSEEVENTF_MIDDLEDOWN;
#else
    mouse_button = 0;
#endif
    if(mouse_button != 0xFFFF)
        key = mouse_button;
    else
        throw std::invalid_argument(std::format("Invalid mouse button input: {}", str));
}

std::string MouseButtonAction::ButtonToString(uint16_t key)
{
#ifdef _WIN32
    switch(key)
    {
    case MOUSEEVENTF_LEFTDOWN:   return "LEFT";
    case MOUSEEVENTF_RIGHTDOWN:  return "RIGHT";
    case MOUSEEVENTF_MIDDLEDOWN: return "MIDDLE";
    default: assert(0); return "INVALID";
    }
#else
    return "INVALID";
#endif
}

std::string MouseButtonAction::GenerateText(TextFormat fmt) const
{
    std::string text = ButtonToString(key);
    return fmt == TextFormat::Ini ? std::format(" {}[{}]", ini_prefix_, text) : text;
}

// --- MousePositionAction base ---

MousePositionAction::MousePositionAction(std::string&& str, std::string_view ini_prefix) : ini_prefix_(ini_prefix)
{

    /* Strip spaces first: taking the index before compacting the string left it
       pointing into the middle of the second number, and could run past the end. */
    boost::erase_all(str, " ");

    const size_t separator_pos = str.find(',');
    if(separator_pos != std::string::npos && separator_pos + 1 < str.size())
    {
        m_pos.x = utils::stoi<decltype(m_pos.x)>(str.substr(0, separator_pos));
        m_pos.y = utils::stoi<decltype(m_pos.y)>(str.substr(separator_pos + 1));
    }
    else
        throw std::invalid_argument(std::format("Invalid mouse position input: {}", str));
}

std::string MousePositionAction::GenerateText(TextFormat fmt) const
{
    return fmt == TextFormat::Ini
        ? std::format(" {}[{},{}]", ini_prefix_, m_pos.x, m_pos.y)
        : std::format("{},{}", m_pos.x, m_pos.y);
}

// --- StringCommand base ---

std::string StringCommand::GenerateText(TextFormat fmt) const
{
    return fmt == TextFormat::Ini ? std::format(" {}[{}]", ini_prefix_, cmd) : cmd;
}

// --- KeyText ---

void KeyText::Execute(const MacroContext& context)
{
#ifdef _WIN32
    for(char c : seq)
        TypeCharacter(static_cast<uint8_t>(c));
#else
    system(fmt::format("xte 'str {}'", seq).c_str());
#endif
}

std::string KeyText::GenerateText(TextFormat fmt) const
{
    return fmt == TextFormat::Ini ? std::format(" KEY_TYPE[{}]", seq) : seq;
}

#ifdef _WIN32
void KeyText::TypeCharacter(uint16_t character)
{
    int count = MultiByteToWideChar(CP_ACP, 0, reinterpret_cast<const char*>(&character), 1, nullptr, 0);
    wchar_t wide_char;
    MultiByteToWideChar(CP_ACP, 0, reinterpret_cast<const char*>(&character), 1, &wide_char, count);
    INPUT input = { 0 };
    input.type = INPUT_KEYBOARD;
    input.ki.wScan = wide_char;
    input.ki.dwFlags = KEYEVENTF_UNICODE;
    if((wide_char & 0xFF00) == 0xE000)
        input.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
    SendInput(1, &input, sizeof(input));
    input.ki.dwFlags |= KEYEVENTF_KEYUP;
    SendInput(1, &input, sizeof(input));
}
#endif

// --- KeyCombination ---

KeyCombination::KeyCombination(std::string&& str)
{
    boost::erase_all(str, " ");
    boost::char_separator<char> sep("+");
    boost::tokenizer<boost::char_separator<char>> tok(str, sep);
    for(const auto& token : tok) /* do not throw on invalid key! */
    {
        std::string key_code = token;
        uint16_t key = CustomMacro::GetKeyScanCode(key_code);
        if(key == 0xFFFF)
            LOG(LogLevel::Error, "Invalid key found in settings.ini: {}", key_code);
        seq.push_back(key);
    }
}

void KeyCombination::Execute(const MacroContext& context)
{
    for(uint16_t key : seq)
        PressReleaseKey(key);
    for(uint16_t key : seq)
        PressReleaseKey(key, false);
}

std::string KeyCombination::GenerateText(TextFormat fmt) const
{
    std::string text;
    for(uint16_t key : seq)
    {
        if(!text.empty()) text += '+';
        text += CustomMacro::GetKeyStringFromScanCode(key);
    }
    return fmt == TextFormat::Ini ? std::format(" KEY_SEQ[{}]", text) : text;
}

void KeyCombination::PressReleaseKey(uint16_t scancode, bool press)
{
#ifdef _WIN32
    INPUT input = { 0 };
    input.type = INPUT_KEYBOARD;
    input.ki.wScan = scancode;
    input.ki.dwFlags = (press ? 0 : KEYEVENTF_KEYUP) | KEYEVENTF_SCANCODE;
    if((scancode & 0xFF00) == 0xE000)
        input.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
    SendInput(1, &input, sizeof(input));
#else

#endif
}

// --- KeyDelay ---

KeyDelay::KeyDelay(std::string&& str)
{

    /* Same ordering trap as MousePositionAction: compact before indexing. */
    boost::erase_all(str, " ");

    const size_t separator_pos = str.find('-');
    if(separator_pos != std::string::npos && separator_pos + 1 < str.size())
    {
        uint32_t delay_start = utils::stoi<uint32_t>(str.substr(0, separator_pos));
        uint32_t delay_end = utils::stoi<uint32_t>(str.substr(separator_pos + 1));
        delay = std::array<uint32_t, 2>{delay_start, delay_end};
    }
    else
    {
        delay = utils::stoi<uint32_t>(str);
    }
}

void KeyDelay::Execute(const MacroContext& context)
{
    std::visit([](auto&& arg)
        {
            using T = std::decay_t<decltype(arg)>;
            if constexpr(std::is_same_v<T, uint32_t>)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(arg));
            }
            else if constexpr(std::is_same_v<T, std::array<uint32_t, 2>>)
            {
                int ret = utils::random_mt(arg[0], arg[1]);
                std::this_thread::sleep_for(std::chrono::milliseconds(ret));
            }
            else
                static_assert(always_false_v<T>, "KeyDelay::Execute Bad visitor!");
        }, delay);
}

std::string KeyDelay::GenerateText(TextFormat fmt) const
{
    if(std::holds_alternative<uint32_t>(delay))
    {
        uint32_t d = std::get<uint32_t>(delay);
        return fmt == TextFormat::Ini ? std::format(" DELAY[{}]", d) : boost::lexical_cast<std::string>(d);
    }
    const auto& delays = std::get<std::array<uint32_t, 2>>(delay);
    return fmt == TextFormat::Ini
        ? std::format(" DELAY[{}-{}]", delays[0], delays[1])
        : boost::lexical_cast<std::string>(delays[0]) + "-" + boost::lexical_cast<std::string>(delays[1]);
}

// --- MouseMovement ---

void MouseMovement::Execute(const MacroContext& context)
{
#ifdef _WIN32
    POINT to_screen = m_pos;
    HWND hwnd = GetForegroundWindow();
    ClientToScreen(hwnd, &to_screen);
    ShowCursor(FALSE);
    SetCursorPos(to_screen.x, to_screen.y);
    ShowCursor(TRUE);
#else
    system(fmt::format("xte 'mousemove {} {}'", 0, 0).c_str());
#endif
}

// --- MouseInterpolate ---

void MouseInterpolate::Execute(const MacroContext& context)
{
#ifdef _WIN32
    POINT to_screen = m_pos;
    HWND hwnd = GetForegroundWindow();
    ClientToScreen(hwnd, &to_screen);

    POINT curr_pos;
    GetCursorPos(&curr_pos);

    const int steps = utils::random_mt(1000, 3000);
    ShowCursor(FALSE);
    for(int i = 0; i <= steps; i++)
    {
        const float t = static_cast<float>(i) / static_cast<float>(steps);
        const int pos_x = static_cast<int>(std::round(std::lerp(static_cast<float>(curr_pos.x), static_cast<float>(to_screen.x), t)));
        const int pos_y = static_cast<int>(std::round(std::lerp(static_cast<float>(curr_pos.y), static_cast<float>(to_screen.y), t)));
        SetCursorPos(pos_x, pos_y);
        std::this_thread::sleep_for(std::chrono::nanoseconds(100));
    }
    ShowCursor(TRUE);
#else
    system(fmt::format("xte 'mousemove {} {}'", 0, 0).c_str());
#endif
}

// --- MousePress ---

void MousePress::Execute(const MacroContext& context)
{
    PressMouse(key);
}

void MousePress::PressMouse(uint16_t mouse_button)
{
#ifdef _WIN32
    INPUT input = { 0 };
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = mouse_button;
    SendInput(1, &input, sizeof(input));
#else

#endif
}

// --- MouseRelease ---

void MouseRelease::Execute(const MacroContext& context)
{
    ReleaseMouse(key);
}

void MouseRelease::ReleaseMouse(uint16_t mouse_button)
{
#ifdef _WIN32
    INPUT input = { 0 };
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = static_cast<DWORD>(mouse_button) << 1;
    SendInput(1, &input, sizeof(input));
#else

#endif
}

// --- MouseClick ---

void MouseClick::Execute(const MacroContext& context)
{
    PressReleaseMouse(key);
}

void MouseClick::PressReleaseMouse(uint16_t mouse_button)
{
#ifdef _WIN32
    INPUT input = { 0 };
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = mouse_button;
    SendInput(1, &input, sizeof(input));
    input.mi.dwFlags = mouse_button << (uint16_t)1;
    SendInput(1, &input, sizeof(input));
#else

#endif
}

// --- BashCommand ---

void BashCommand::Execute(const MacroContext& context)
{
#ifdef _WIN32
    std::wstring param(cmd.begin(), cmd.end());
    std::wstring command = L"/k " + param;
    ShellExecuteW(NULL, L"open", L"cmd", command.c_str(), NULL, SW_NORMAL);
#else

#endif
}

// --- CommandExecute ---

void CommandExecute::Execute(const MacroContext& context)
{
#ifdef _WIN32
    std::wstring param(cmd.begin(), cmd.end());
    std::wstring command = L"/k " + param;
    utils::ExecuteCmdWithoutWindow(command.c_str());
#else

#endif
}

// --- CommandXml ---

void CommandXml::Execute(const MacroContext& context)
{
    std::vector<std::string> params;
    boost::split(params, cmd, boost::is_any_of("+"));

    if(params.size() != 2)
    {
        LOG(LogLevel::Warning, "Invalid input: {}", cmd);
        return;
    }
    if(!context.command_executor)
    {
        LOG(LogLevel::Warning, "No command executor is wired up; cannot run {}", cmd);
        return;
    }
    context.command_executor->ExecuteByName(params[0], params[1]);
}

// --- KeyBringAppToForeground ---

KeyBringAppToForeground::KeyBringAppToForeground(std::string&& str)
{
    std::vector<std::string> params;
    boost::split(params, str, boost::is_any_of(","));
    if(params.size() == 2)
    {
        app = std::move(params[0]);
        title = std::move(params[1]);
    }
    else
        LOG(LogLevel::Warning, "Invalid input: {}", str);
}

void KeyBringAppToForeground::Execute(const MacroContext& context)
{
    if(context.screen)
        context.screen->BringWindowToForeground(app, title);
}

std::string KeyBringAppToForeground::GenerateText(TextFormat fmt) const
{
    return fmt == TextFormat::Ini ? std::format(" CMD_FG[{},{}]", app, title) : std::format("{},{}", app, title);
}

// --- KeyFindImageOnScreen ---

KeyFindImageOnScreen::KeyFindImageOnScreen(std::string&& str)
{
    std::vector<std::string> params;
    boost::split(params, str, boost::is_any_of(","));
    if(params.size() == 3)
    {
        image_path = std::move(params[0]);

        /* The catch left whichever offset had not been assigned at its default
           and carried on, so a malformed pair produced a half-applied offset.
           Both are read before either is stored. */
        const std::optional<int> x = utils::TryParse<int>(params[1]);
        const std::optional<int> y = utils::TryParse<int>(params[2]);
        if(x && y)
        {
            offset.x = *x;
            offset.y = *y;
        }
        else
            LOG(LogLevel::Warning, "Invalid offset, expected two numbers: {}", str);
    }
    else
        LOG(LogLevel::Warning, "Invalid input: {}", str);
}

void KeyFindImageOnScreen::Execute(const MacroContext& context)
{
    if(!context.screen)
        return;

    int x = 0, y = 0;
    if(context.screen->FindImageOnScreen(image_path.generic_string(), x, y))
        context.screen->MoveCursorAndClick(x, y);
    else
        LOG(LogLevel::Verbose, "Image isn't found on screen");
}

std::string KeyFindImageOnScreen::GenerateText(TextFormat fmt) const
{
    return fmt == TextFormat::Ini
        ? std::format(" CMD_IMG[{},{},{}]", image_path.generic_string(), offset.x, offset.y)
        : std::format("{},{},{}", image_path.generic_string(), offset.x, offset.y);
}
