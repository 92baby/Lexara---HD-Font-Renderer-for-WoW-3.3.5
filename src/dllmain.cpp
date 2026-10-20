// FontExact — standalone HD font DLL for WoW 3.3.5 (build 12340)
// Drop dinput8.dll in the WoW root folder. No addon required.
// Derived from Transmorpher 2.0.0 (StealthMorpher) — font subsystem only.

#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <string>
#include "ShutdownCheck.h"
extern "C" volatile bool g_isProcessTerminating = false;

#include <windows.h>
#include "Proxy.h"
#include "MSDF.h"

// Diagnostic log: <folder containing wow.exe>\\lexara.log (recreated on every start).
// Low volume (a few dozen lines at most), so the file is simply opened per call.
void Log(const char* fmt, ...) {
    static std::mutex s_mutex;
    static bool s_first = true;
    static wchar_t s_path[MAX_PATH * 2] = {};

    std::lock_guard<std::mutex> lock(s_mutex);
    if (!s_path[0]) {
        DWORD n = GetModuleFileNameW(nullptr, s_path, static_cast<DWORD>(std::size(s_path)));
        if (n == 0 || n >= std::size(s_path)) return;
        std::wstring p(s_path);
        size_t slash = p.find_last_of(L"\\/");
        p = (slash == std::wstring::npos ? std::wstring() : p.substr(0, slash + 1)) + L"lexara.log";
        wcsncpy_s(s_path, p.c_str(), _TRUNCATE);
    }

    // Keep the previous session's log: it is otherwise overwritten on every start,
    // which loses the evidence of a bad session as soon as the game is relaunched.
    if (s_first) {
        std::wstring prev(s_path);
        const size_t dot = prev.find_last_of(L'.');
        prev = (dot == std::wstring::npos ? prev : prev.substr(0, dot)) + L".prev.log";
        CopyFileW(s_path, prev.c_str(), FALSE);
    }

    FILE* f = nullptr;
    if (_wfopen_s(&f, s_path, s_first ? L"wb" : L"ab") != 0 || !f) return;
    s_first = false;

    SYSTEMTIME st;
    GetLocalTime(&st);
    fprintf(f, "%02d:%02d:%02d.%03d ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    va_list args;
    va_start(args, fmt);
    vfprintf(f, fmt, args);
    va_end(args);
    fputc('\n', f);
    fclose(f);
}

bool FontExact_OnAttach();

HMODULE g_hThisModule = nullptr;

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason, LPVOID /*lpReserved*/)
{
    switch (ul_reason)
    {
    case DLL_PROCESS_ATTACH:
        g_hThisModule = hModule;
        DisableThreadLibraryCalls(hModule);
        SetupProxy();
        FontExact_OnAttach();
        LoadLibraryA("LuxShoulderCam.dll"); // LuxShoulderCam mod
        break;

    case DLL_PROCESS_DETACH:
        g_isProcessTerminating = true;
        MSDF::shutdown();
        break;
    }
    return TRUE;
}