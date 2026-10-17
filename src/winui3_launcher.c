#include "winui3_launcher.h"
#include <shellapi.h>
#include <shlwapi.h>
#include <strsafe.h>

#define WINUI3_BRIDGE_SHOW_MESSAGE 0x8057
#define WINUI3_BRIDGE_RELOAD_MESSAGE 0x8058

static HANDLE g_launchProcess = NULL;
static HWND g_owner, g_bridgeWindow;
static BOOL g_showPending;
static MenuTriggerType g_pendingTrigger;
static ULONGLONG g_deadline;
static WCHAR g_pendingTitle[1024];
#define BRIDGE_POLL_TIMER 0x5755
static BOOL is_owned_bridge(HWND bridge) {
    DWORD windowPid = 0;
    if (!bridge || !g_launchProcess ||
        WaitForSingleObject(g_launchProcess, 0) != WAIT_TIMEOUT) return FALSE;
    GetWindowThreadProcessId(bridge, &windowPid);
    return windowPid != 0 && windowPid == GetProcessId(g_launchProcess);
}

static HWND find_owned_bridge(const WCHAR* title) {
    HWND bridge = FindWindowW(NULL, title);
    return is_owned_bridge(bridge) ? bridge : NULL;
}

void SetWinUI3OwnerWindow(HWND owner) { g_owner = owner; }
BOOL PollWinUI3Menu(UINT_PTR timerId) {
    if (timerId != BRIDGE_POLL_TIMER) return FALSE;
    HWND bridge = find_owned_bridge(g_pendingTitle);
    if (bridge && g_showPending) {
        g_bridgeWindow = bridge;
        AllowSetForegroundWindow(ASFW_ANY);
        if (!PostMessageW(bridge, WINUI3_BRIDGE_SHOW_MESSAGE, (WPARAM)g_pendingTrigger, 0))
            PostMessageW(g_owner, WM_APP + 0x60, (WPARAM)g_pendingTrigger, 0);
        g_showPending = FALSE;
    } else if (g_showPending && (GetTickCount64() >= g_deadline ||
               (g_launchProcess && WaitForSingleObject(g_launchProcess, 0) == WAIT_OBJECT_0))) {
        g_showPending = FALSE;
        OutputDebugStringW(L"WinUI helper unavailable; displaying native fallback.\n");
        PostMessageW(g_owner, WM_APP + 0x60, (WPARAM)g_pendingTrigger, 0);
    }
    if (!g_showPending) KillTimer(g_owner, BRIDGE_POLL_TIMER);
    return TRUE;
}

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
        StringCchPrintfW(out, cchOut, L"WinMacMenuWinUI3::%lu::%s", GetCurrentProcessId(), configPath);
    } else {
        StringCchPrintfW(out, cchOut, L"WinMacMenuWinUI3::%lu::", GetCurrentProcessId());
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

static BOOL post_reload_message(HWND bridgeHwnd) {
    if (!bridgeHwnd) return FALSE;
    return PostMessageW(bridgeHwnd, WINUI3_BRIDGE_RELOAD_MESSAGE, 0, 0);
}

static void close_launch_process_handle(void) {
    if (g_launchProcess) {
        CloseHandle(g_launchProcess);
        g_launchProcess = NULL;
    }
}

static BOOL start_bridge(const Config* cfg, BOOL showMenu, MenuTriggerType trigger, DWORD timeoutMs) {
    if (!cfg) return FALSE;
    if (!cfg->runInBackground && !showMenu) return TRUE;
    if (showMenu && g_showPending) {
        g_showPending = FALSE;
        if (g_owner) KillTimer(g_owner, BRIDGE_POLL_TIMER);
        return TRUE; // second trigger cancels a still-pending open
    }
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

    HWND bridgeHwnd = find_owned_bridge(bridgeTitle);
    if (bridgeHwnd) {
        g_bridgeWindow = bridgeHwnd;
        if (showMenu) return post_show_message(bridgeHwnd, trigger);
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
    if (!cfg->runInBackground) {
        StringCchPrintfW(params, ARRAYSIZE(params), L"--oneshot --show-on-launch --config \"%s\"", configPath);
        WCHAR command[4096];
        StringCchPrintfW(command, ARRAYSIZE(command), L"\"%s\" %s", exePath, params);
        STARTUPINFOW startup = {sizeof(startup)};
        PROCESS_INFORMATION process = {0};
        if (!CreateProcessW(exePath, command, NULL, NULL, FALSE, 0, NULL, exeDir, &startup, &process)) return FALSE;
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        return TRUE; // one-shot helper owns its lifetime after the native host exits
    }
    if (configPath[0]) {
        StringCchPrintfW(params, ARRAYSIZE(params), L"--bridge --parent-pid %lu --config \"%s\"",
                  GetCurrentProcessId(), configPath);
    } else {
        StringCchPrintfW(params, ARRAYSIZE(params), L"--bridge --parent-pid %lu", GetCurrentProcessId());
    }

    AllowSetForegroundWindow(ASFW_ANY);

    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = {0};
    WCHAR cmdLine[4096];
    StringCchPrintfW(cmdLine, ARRAYSIZE(cmdLine), L"\"%s\" %s", exePath, params);

    if (!g_launchProcess && !CreateProcessW(exePath, cmdLine, NULL, NULL, FALSE, 0, NULL, exeDir, &si, &pi)) {
        OutputDebugStringW(L"Failed to launch WinUI3 menu helper; falling back to native menu.\n");
        return FALSE;
    }
    if (!g_launchProcess) {
        g_launchProcess = pi.hProcess;
        CloseHandle(pi.hThread);
    }

    if (!showMenu) return TRUE;

    // Resident requests complete through the normal message loop; no polling
    // sleep on the thread that owns the keyboard/mouse hooks.
    if (g_owner && cfg->runInBackground) {
        lstrcpynW(g_pendingTitle, bridgeTitle, ARRAYSIZE(g_pendingTitle));
        g_pendingTrigger = trigger;
        g_deadline = GetTickCount64() + timeoutMs;
        g_showPending = TRUE;
        if (SetTimer(g_owner, BRIDGE_POLL_TIMER, 25, NULL)) return TRUE;
        g_showPending = FALSE;
        return FALSE;
    }

    // One-shot mode has no resident loop yet. Pump messages during startup.
    ULONGLONG deadline = GetTickCount64() + timeoutMs;
    do {
        bridgeHwnd = find_owned_bridge(bridgeTitle);
        if (bridgeHwnd) { g_bridgeWindow = bridgeHwnd; return post_show_message(bridgeHwnd, trigger); }
        if (g_launchProcess && WaitForSingleObject(g_launchProcess, 0) == WAIT_OBJECT_0) break;
        MsgWaitForMultipleObjectsEx(0, NULL, 25, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        MSG msg;
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) { PostQuitMessage((int)msg.wParam); return FALSE; }
            TranslateMessage(&msg); DispatchMessageW(&msg);
        }
    } while (GetTickCount64() < deadline);
    return FALSE;
}

BOOL LaunchWinUI3Menu(const Config* cfg, MenuTriggerType trigger) {
    return start_bridge(cfg, TRUE, trigger, 2000);
}

BOOL LaunchWinUI3MenuAndWait(const Config* cfg, MenuTriggerType trigger, DWORD timeoutMs) {
    return start_bridge(cfg, TRUE, trigger, timeoutMs);
}

BOOL PreloadWinUI3Menu(const Config* cfg) {
    return start_bridge(cfg, FALSE, MENU_TRIGGER_OTHER, 0);
}

BOOL RefreshWinUI3MenuConfig(const Config* cfg) {
    WCHAR configPath[MAX_PATH];
    WCHAR bridgeTitle[1024];
    HWND bridgeHwnd;

    if (!cfg || !cfg->useWinUI3Menu) return TRUE;

    get_config_path(cfg, configPath, ARRAYSIZE(configPath));
    build_bridge_title(configPath, bridgeTitle, ARRAYSIZE(bridgeTitle));
    bridgeHwnd = find_owned_bridge(bridgeTitle);
    if (bridgeHwnd) {
        return post_reload_message(bridgeHwnd);
    }

    return PreloadWinUI3Menu(cfg);
}

void ShutdownWinUI3Menu(void) {
    g_showPending = FALSE;
    if (g_owner) KillTimer(g_owner, BRIDGE_POLL_TIMER);
    // Only this process's retained child handle grants shutdown authority.
    if (g_launchProcess) {
        DWORD pid = GetProcessId(g_launchProcess), windowPid = 0;
        if (g_bridgeWindow) GetWindowThreadProcessId(g_bridgeWindow, &windowPid);
        if (pid && pid == windowPid)
            PostMessageW(g_bridgeWindow, WINUI3_BRIDGE_SHOW_MESSAGE + 2, 0, 0);
        if (WaitForSingleObject(g_launchProcess, 250) == WAIT_TIMEOUT)
            TerminateProcess(g_launchProcess, 0);
        close_launch_process_handle();
    }
    g_bridgeWindow = NULL;
}
