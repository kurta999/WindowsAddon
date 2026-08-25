#include "pch_core.hpp"
#include "utils/WindowsCommand.hpp"

/* The definition for the declaration next door. It sat in Utils.cpp, which
   is why Utils.cpp had to include this header and so pulled <atlstr.h> in
   with it - the very cost the header was split out to avoid. Nothing else in
   Utils.cpp used CStringA. */

#ifdef _WIN32

namespace utils
{
	CStringA ExecuteCmdWithoutWindow(const wchar_t* cmd, uint32_t timeout)
	{
		CStringA strResult;
		HANDLE hPipeRead, hPipeWrite;

		SECURITY_ATTRIBUTES saAttr = { sizeof(SECURITY_ATTRIBUTES) };
		saAttr.bInheritHandle = TRUE; // Pipe handles are inherited by child process.
		saAttr.lpSecurityDescriptor = NULL;

		// Create a pipe to get results from child's stdout.
		if(!CreatePipe(&hPipeRead, &hPipeWrite, &saAttr, 0))
			return strResult;

		STARTUPINFOW si = { sizeof(STARTUPINFOW) };
		si.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
		si.hStdOutput = hPipeWrite;
		si.hStdError = hPipeWrite;
		si.wShowWindow = SW_HIDE; // Prevents cmd window from flashing.
								  // Requires STARTF_USESHOWWINDOW in dwFlags.

		PROCESS_INFORMATION pi = { 0 };
		/* CreateProcessW is documented to modify lpCommandLine in place, so it
		   never receives the caller's buffer - which is often a string literal. */
		std::wstring mutable_cmd(cmd ? cmd : L"");
		BOOL fSuccess = CreateProcessW(L"C:\\windows\\system32\\cmd.exe", mutable_cmd.data(), NULL, NULL, TRUE, NORMAL_PRIORITY_CLASS | CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
		if(!fSuccess)
		{
			CloseHandle(hPipeWrite);
			CloseHandle(hPipeRead);
			return strResult;
		}

		if(timeout != std::numeric_limits<uint32_t>::min())
		{
			bool bProcessEnded = false;
			for(; !bProcessEnded;)
			{
				// Give some timeslice (50 ms), so we won't waste 100% CPU.
				bProcessEnded = WaitForSingleObject(pi.hProcess, 50) == WAIT_OBJECT_0;

				// Even if process exited - we continue reading, if
				// there is some data available over pipe.
				for(;;)
				{
					char buf[1024];
					DWORD dwRead = 0;
					DWORD dwAvail = 0;

					if(!::PeekNamedPipe(hPipeRead, NULL, 0, NULL, &dwAvail, NULL))
						break;

					if(!dwAvail) // No data available, return
						break;

					if(!::ReadFile(hPipeRead, buf, std::min((DWORD)sizeof(buf) - 1, dwAvail), &dwRead, NULL) || !dwRead)
						// Error, the child process might ended
						break;

					buf[dwRead] = 0;
					strResult += buf;
				}
			}
		}

		CloseHandle(hPipeWrite);
		CloseHandle(hPipeRead);
		CloseHandle(pi.hProcess);
		CloseHandle(pi.hThread);
		return strResult;
	} //ExecCmd
}

#endif
