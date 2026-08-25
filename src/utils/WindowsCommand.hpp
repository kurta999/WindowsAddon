#pragma once

#ifdef _WIN32

#include <atlstr.h>
#include <cstdint>
#include <limits>

// !\brief Running a command line without showing a console window.
//
// This declaration used to sit in Utils.hpp, which meant <atlstr.h> - ATL, for
// one CStringA return type - was compiled into all 41 translation units that
// wanted utils::stoi. Three of them actually call this.
namespace utils
{
// !\brief Run `cmd` with no console window and return its captured output.
// !\param timeout Milliseconds to wait; the default means "no timeout".
CStringA ExecuteCmdWithoutWindow(const wchar_t* cmd,
    uint32_t timeout = std::numeric_limits<uint32_t>::min());
}

#endif
