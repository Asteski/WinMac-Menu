#include "winui3_launcher.h"
#include <shellapi.h>
#include <shlwapi.h>
#include <tlhelp32.h>

#define WINUI3_BRIDGE_SHOW_MESSAGE 0x8057

static HANDLE g_launchProcess = NULL;

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

static BOOL get_config_path(const Config* cfg, WCHAR* out, DWORD cchOut) {
    if (!out || cchOut == 0) return FALSE;
    out[0] = 0;
    if (!cfg || !cfg->iniPath[0]) return TRUE;

    DWORD n = GetFullPathNameW(cfg->iniPath, cchOut, out, NULL);
    if (n == 0 || n >= cchOut) {
        lstrcpynW(out, cfg->iniPath, cchOut);
    }
    return TRUE;
}

static BOOL post_show_message(HWND bridgeHwnd, MenuTriggerType trigger) {
    if (!bridgeHwnd) return FALSE;
    AllowSetForegroundWindow(ASFW_ANY);
    return PostMessageW(bridgeHwnd, WINUI3_BRIDGE_SHOW_MESSAGE, (WPARAM)trigger, 0);
}

static void close_launch_process_handle(void) {
    if (g_launchProcess) {
        CloseHandle(g_launchProcess);
        g_launchProcess = NULL;
    }
}

static BOOL start_bridge(const Config* cfg, BOOL showMenu, MenuTriggerType trigger) {
    WCHAR exePath[MAX_PATH];
    if (!find_winui3_exe(exePath, ARRAYSIZE(exePath))) {
        OutputDebugStringW(L"WinUI3 menu helper not found; falling back to native menu.\n");
        return FALSE;
    }

    WCHAR exeDir[MAX_PATH];
    lstrcpynW(exeDir, exePath, ARRAYSIZE(exeDir));
    PathRemoveFileSpecW(exeDir);

    WCHAR configPath[MAX_PATH];
    get_config_path(cfg, configPath, ARRAYSIZE(configPath));

    WCHAR bridgeTitle[1024];
    build_bridge_title(configPath, bridgeTitle, ARRAYSIZE(bridgeTitle));

    HWND bridgeHwnd = FindWindowW(NULL, bridgeTitle);
    if (bridgeHwnd) {
        close_launch_process_handle();
        if (showMenu) post_show_message(bridgeHwnd, trigger);
        return TRUE;
    }

    if (g_launchProcess) {
        if (WaitForSingleObject(g_launchProcess, 0) == WAIT_OBJECT_0) {
            close_launch_process_handle();
        } else if (!showMenu) {
            return TRUE;
        }
    }

    WCHAR params[2048];
    if (configPath[0]) {
        wsprintfW(params, L"--bridge --parent-pid %lu --config \"%s\"",
                  GetCurrentProcessId(), configPath);
    } else {
        wsprintfW(params, L"--bridge --parent-pid %lu", GetCurrentProcessId());
    }

    SHELLEXECUTEINFOW sei = { sizeof(sei) };
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.lpFile = exePath;
    sei.lpParameters = params;
    sei.lpDirectory = exeDir;
    sei.nShow = SW_SHOWNORMAL;

    AllowSetForegroundWindow(ASFW_ANY);

    if (!g_launchProcess && !ShellExecuteExW(&sei)) {
        OutputDebugStringW(L"Failed to launch WinUI3 menu helper; falling back to native menu.\n");
        return FALSE;
    }
    if (!g_launchProcess) g_launchProcess = sei.hProcess;

    if (!showMenu) return TRUE;

    for (int i = 0; i < 80; ++i) {
        bridgeHwnd = FindWindowW(NULL, bridgeTitle);
        if (bridgeHwnd) {
            close_launch_process_handle();
            post_show_message(bridgeHwnd, trigger);
            return TRUE;
        }
        if (g_launchProcess && WaitForSingleObject(g_launchProcess, 0) == WAIT_OBJECT_0) {
            close_launch_process_handle();
            break;
        }
        Sleep(25);
    }

    OutputDebugStringW(L"WinUI3 menu helper did not create bridge window; falling back to native menu.\n");
    return FALSE;
}

BOOL LaunchWinUI3Menu(const Config* cfg, MenuTriggerType trigger) {
    return start_bridge(cfg, TRUE, trigger);
}

BOOL PreloadWinUI3Menu(const Config* cfg) {
    return start_bridge(cfg, FALSE, MENU_TRIGGER_OTHER);
}

void ShutdownWinUI3Menu(void) {
    close_launch_process_handle();
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return;

    PROCESSENTRY32W pe = { sizeof(pe) };
    if (Process32FirstW(snapshot, &pe)) {
        do {
            if (!lstrcmpiW(pe.szExeFile, L"WinMacMenuWinUI3.exe")) {
                HANDLE process = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
                if (process) {
                    TerminateProcess(process, 0);
                    CloseHandle(process);
                }
            }
        } while (Process32NextW(snapshot, &pe));
    }
    CloseHandle(snapshot);
}
