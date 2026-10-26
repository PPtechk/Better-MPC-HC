/*
 * MSBuild.exe forwarder for the MPC-HC build.
 * The VS 2015 vcvarsall.bat puts the .NET 4.0 MSBuild first in the PATH when MSBuild 14
 * isn't installed. The build scripts call "MSBuild.exe" from their working directory,
 * so this forwarder placed there passes every argument to the VS 2019 MSBuild.
 */
#include <windows.h>
#include <stdio.h>
#include <wchar.h>

static const wchar_t TARGET[] =
    L"C:\\Program Files (x86)\\Microsoft Visual Studio\\2019\\BuildTools\\MSBuild\\Current\\Bin\\MSBuild.exe";

int wmain(void)
{
    /* keep the original argument string, only drop our own program name */
    const wchar_t* args = GetCommandLineW();
    if (*args == L'"') {
        args++;
        while (*args && *args != L'"') {
            args++;
        }
        if (*args) {
            args++;
        }
    } else {
        while (*args && *args != L' ' && *args != L'\t') {
            args++;
        }
    }

    size_t len = wcslen(TARGET) + wcslen(args) + 4;
    wchar_t* cmdline = (wchar_t*)HeapAlloc(GetProcessHeap(), 0, len * sizeof(wchar_t));
    if (!cmdline) {
        return 1;
    }
    swprintf(cmdline, len, L"\"%ls\"%ls", TARGET, args);

    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    if (!CreateProcessW(TARGET, cmdline, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
        fwprintf(stderr, L"MSBuild forwarder: cannot start %ls (error %lu)\n", TARGET, GetLastError());
        return 9009;
    }

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return (int)exitCode;
}
