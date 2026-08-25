#include "pch_core.hpp"
#include "TerminalHotkey.hpp"
#include "Logger.hpp"
#include "Utils.hpp"
#include "SettingsReader.hpp"
#include "SettingsWriter.hpp"
#include <ostream>

void TerminalHotkey::SetKey(const std::string& key_str)
{
#ifdef _WIN32
	vkey = utils::GetVirtualKeyFromString(key_str);
	if(vkey == 0xFFFF)
	{
		LOG(LogLevel::Warning, "Invalid hotkey was specified for TerminalHotkey: {}", vkey);
		/* This is a member function: the field is its own. */
		is_enabled = false;
	}
#endif
	UpdateHotkeyRegistration();
}

std::string TerminalHotkey::GetKey() const
{
	return utils::GetKeyStringFromVirtualKey(vkey);
}

void TerminalHotkey::UpdateHotkeyRegistration()
{
	/* Registering a system-wide hotkey needs a window handle, which only the
	   GUI has. The composition root supplies the registrar. */
	if(m_Registrar)
		m_Registrar(vkey);
}

void TerminalHotkey::Process()
{
#ifdef _WIN32 /* This is already done in Linux, so this implementation is Windows only */
	if(is_enabled)
	{
		std::wstring str = utils::GetDestinationPathFromFileExplorer();
		if(!str.empty())  /* User is in file explorer */
		{
			std::filesystem::path p(str);
			if(p.has_extension() && std::filesystem::is_regular_file(p))  /* If file is selected in explorer, it has to be removed */
				p.remove_filename();
			str = p.generic_wstring();
		}
		else
		{
			HWND foreground = GetForegroundWindow();
			if(foreground)
			{
				char window_title[256];
				GetWindowTextA(foreground, window_title, sizeof(window_title));
				if(!strncmp(window_title, "Program Manager", 16) || std::strlen(window_title) == 0)  /* User has desktop in focus or clicked on system tray*/
				{
					std::string dekstop_str = getenv("USERPROFILE") + std::string("/Desktop");
					str += std::wstring(dekstop_str.begin(), dekstop_str.end());
				}

			}
		}

		if(!str.empty())
			OpenTerminal(str);
	}
#endif
}

void TerminalHotkey::OpenTerminal(std::wstring& path)
{
#ifdef _WIN32
	/* The path comes from whatever Explorer window had focus, so it is quoted
	   into a shell command line here. A quote (or a PowerShell brace) in a
	   directory name would otherwise end the argument early. */
	std::wstring safe_path;
	safe_path.reserve(path.size());
	for(const wchar_t character : path)
	{
		if(character == L'"' || character < 0x20)
			continue;
		safe_path.push_back(character);
	}
	if(safe_path.empty())
	{
		LOG(LogLevel::Warning, "Refusing to open a terminal for an empty path");
		return;
	}

	switch(type)
	{
		case TerminalType::COMMAND_LINE:
		{
			ShellExecute(NULL, L"open", L"cmd", std::format(L"/k cd /d \"{}\"", safe_path).c_str(), NULL, SW_SHOW);
			break;
		}		
		case TerminalType::POWER_SHELL:
		{
			ShellExecute(NULL, L"open", L"powershell", std::format(L"-NoExit -command \"& {{Set-Location -LiteralPath '{}'}}\"", safe_path).c_str(), NULL, SW_SHOW);
			break;
		}		
		case TerminalType::BASH_TERMINAL:
		{
			ShellExecute(NULL, L"open", L"wsl", std::format(L"--cd \"{}\"", safe_path).c_str(), NULL, SW_SHOW);
			break;
		}
		default:  /* WINDOWS_TERMINAL */
		{
			ShellExecute(NULL, L"open", L"wt", std::format(L"/d \"{}\"", safe_path).c_str(), NULL, SW_SHOW);
			break;
		}
	}
#endif
}

void TerminalHotkey::LoadSettings(SettingsReader& reader)
{
    is_enabled = utils::stob(reader.Required("TerminalHotkey", "Enable"));
    SetKey(reader.Required("TerminalHotkey", "Key"));
    type = static_cast<TerminalType>(utils::stoi<uint8_t>(reader.Required("TerminalHotkey", "Type")));
}

void TerminalHotkey::SaveSettings(std::ostream& out) const
{
    SettingsWriter(out, "TerminalHotkey")
        .Key("Enable", is_enabled)
        .Key("Key", GetKey())
        .Key("Type", static_cast<uint32_t>(type), "0 = WINDOWS_TERMINAL, 1 = cmd.exe, 2 = POWER_SHELL, 3 = BASH_TERMINAL")
        .Blank();
}
