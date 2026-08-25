#include "SystemCommandRunner.hpp"

#include <cstdlib>
#include <string>
#include <thread>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <spawn.h>
#include <sys/types.h>
#include <sys/wait.h>

extern char** environ;
#endif

bool SystemCommandRunner::Start(std::string_view command, bool hidden)
{
#ifdef _WIN32
    STARTUPINFOA startup_info{};
    startup_info.cb = sizeof(startup_info);
    startup_info.dwFlags = STARTF_USESHOWWINDOW;
    startup_info.wShowWindow = hidden ? SW_HIDE : SW_SHOW;

    PROCESS_INFORMATION process_info{};
    std::string command_line = "C:\\windows\\system32\\cmd.exe /c ";
    command_line.append(command);

    const DWORD creation_flags = hidden ? CREATE_NO_WINDOW : NORMAL_PRIORITY_CLASS;
    /* No handles are inherited: the child needs none, and inheriting every
       inheritable handle in the process leaks them into an arbitrary command. */
    const BOOL started = CreateProcessA(nullptr, command_line.data(), nullptr, nullptr, FALSE,
        creation_flags, nullptr, nullptr, &startup_info, &process_info);
    if(!started)
        return false;

    CloseHandle(process_info.hProcess);
    CloseHandle(process_info.hThread);
    return true;
#else
    (void)hidden;
    std::string command_copy(command);
    char shell[] = "/bin/sh";
    char option[] = "-c";
    char* arguments[] = {shell, option, command_copy.data(), nullptr};
    pid_t process_id{};
    if(posix_spawn(&process_id, shell, nullptr, nullptr, arguments, environ) != 0)
        return false;

    // Reap the child asynchronously. This preserves ICommandRunner's
    // non-blocking contract without leaking a zombie process.
    std::thread([process_id]
    {
        int status = 0;
        while(waitpid(process_id, &status, 0) == -1 && errno == EINTR)
        {
        }
    }).detach();
    return true;
#endif
}
