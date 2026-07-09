#include "winui3_launcher.h"
#include <shellapi.h>
#include <shlwapi.h>

#define WINUI3_BRIDGE_SHOW_MESSAGE 0x8057

static BOOL file_exists(const WCHAR* path) {
    DWORD attrs = GetFileAttributesW(path);
    return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

static BOOL find_winui3_exe(WCHAR* out, DWORD cchOut) {
    if (!out || cchOut == 0) return FALSE;
    out[0] = 0;

    WCHAR exeDir[MAX_PATH];
    if (!GetModuleFileNameW(NULL, exeDir, ARRAYSIZE(exeDir))) return FALSE;
    PathRemoveFileSpecW(exeDir);

    lstrcpynW(out, exeDir, cchOut);
    PathAppendW(out, L"winui3\\WinMacMenuWinUI3.exe");
    if (file_exists(out)) return TRUE;

    lstrcpynW(out, exeDir, cchOut);
    PathAppendW(out, L"WinMacMenuWinUI3.exe");
    if (file_exists(out)) return TRUE;

    return FALSE;
}

static void build_bridge_title(const WCHAR* configPath, WCHAR* out, DWORD cchOut) {
    if (!out || cchOut == 0) return;
    if (configPath && configPath[0]) {
        wsprintfW(out, L"WinMacMenuWinUI3::%s", configPath);
    } else {
        lstrcpynW(out, L"WinMacMenuWinUI3::", cchOut);
    }
}

BOOL LaunchWinUI3Menu(const Config* cfg) {
    WCHAR exePath[MAX_PATH];
    if (!find_winui3_exe(exePath, ARRAYSIZE(exePath))) {
        OutputDebugStringW(L"WinUI3 menu helper not found; falling back to native menu.\n");
        return FALSE;
    }

    WCHAR exeDir[MAX_PATH];
    lstrcpynW(exeDir, exePath, ARRAYSIZE(exeDir));
    PathRemoveFileSpecW(exeDir);

    WCHAR configPath[MAX_PATH] = L"";
    if (cfg && cfg->iniPath[0]) {
        DWORD n = GetFullPathNameW(cfg->iniPath, ARRAYSIZE(configPath), configPath, NULL);
        if (n == 0 || n >= ARRAYSIZE(configPath)) {
            lstrcpynW(configPath, cfg->iniPath, ARRAYSIZE(configPath));
        }
    }

    WCHAR bridgeTitle[1024];
    build_bridge_title(configPath, bridgeTitle, ARRAYSIZE(bridgeTitle));

    HWND bridgeHwnd = FindWindowW(NULL, bridgeTitle);
    if (bridgeHwnd) {
        AllowSetForegroundWindow(ASFW_ANY);
        PostMessageW(bridgeHwnd, WINUI3_BRIDGE_SHOW_MESSAGE, 0, 0);
        return TRUE;
    }

    WCHAR params[2048];
    if (configPath[0]) {
        wsprintfW(params, L"--bridge --parent-pid %lu --config \"%s\"",
                  GetCurrentProcessId(), configPath);
    } else {
        wsprintfW(params, L"--bridge --parent-pid %lu", GetCurrentProcessId());
    }

    SHELLEXECUTEINFOW sei = { sizeof(sei) };
    sei.lpFile = exePath;
    sei.lpParameters = params;
    sei.lpDirectory = exeDir;
    sei.nShow = SW_SHOWNORMAL;

    AllowSetForegroundWindow(ASFW_ANY);

    if (!ShellExecuteExW(&sei)) {
        OutputDebugStringW(L"Failed to launch WinUI3 menu helper; falling back to native menu.\n");
        return FALSE;
    }

    for (int i = 0; i < 80; ++i) {
        bridgeHwnd = FindWindowW(NULL, bridgeTitle);
        if (bridgeHwnd) {
            AllowSetForegroundWindow(ASFW_ANY);
            PostMessageW(bridgeHwnd, WINUI3_BRIDGE_SHOW_MESSAGE, 0, 0);
            return TRUE;
        }
        Sleep(25);
    }

    OutputDebugStringW(L"WinUI3 menu helper did not create bridge window; falling back to native menu.\n");
    return FALSE;
}
