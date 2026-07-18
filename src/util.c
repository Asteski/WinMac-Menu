#include "util.h"
#include <shellapi.h>
#include <shlwapi.h>
#include <powrprof.h>
#include <processthreadsapi.h>
#include <winternl.h>
#include <wtsapi32.h>
#pragma comment(lib, "PowrProf.lib")
// Lock/logoff don't need extra libs beyond user32/advapi32 which are already linked
#include <shlwapi.h>
#include <strsafe.h>

#pragma comment(lib, "Advapi32.lib")

static void trim_token_inplace(WCHAR* s) {
    if (!s || !s[0]) return;
    WCHAR* start = s;
    while (*start == L' ' || *start == L'\t') start++;
    if (start != s) {
        MoveMemory(s, start, (lstrlenW(start) + 1) * sizeof(WCHAR));
    }

    int len = lstrlenW(s);
    while (len > 0 && (s[len - 1] == L' ' || s[len - 1] == L'\t')) {
        s[len - 1] = 0;
        len--;
    }
}

static void to_lower_inplace(WCHAR* s) {
    if (!s) return;
    for (; *s; ++s) {
        if (*s >= L'A' && *s <= L'Z') *s = (WCHAR)(*s - L'A' + L'a');
    }
}

static BOOL token_matches_process_name(const WCHAR* token, const WCHAR* exeBaseLower, const WCHAR* exeStemLower) {
    WCHAR t[260];
    lstrcpynW(t, token ? token : L"", ARRAYSIZE(t));
    trim_token_inplace(t);
    if (!t[0]) return FALSE;
    to_lower_inplace(t);

    if (!lstrcmpiW(t, exeBaseLower)) return TRUE;
    if (!lstrcmpiW(t, exeStemLower)) return TRUE;

    WCHAR tStem[260];
    lstrcpynW(tStem, t, ARRAYSIZE(tStem));
    WCHAR* dot = wcsrchr(tStem, L'.');
    if (dot && !lstrcmpiW(dot, L".exe")) {
        *dot = 0;
        if (!lstrcmpiW(tStem, exeStemLower)) return TRUE;
    }

    return FALSE;
}

static BOOL is_process_excluded(LPCWSTR exclusionCsv, const WCHAR* exeBaseLower, const WCHAR* exeStemLower) {
    if (!exclusionCsv || !exclusionCsv[0]) return FALSE;

    WCHAR csv[1024];
    lstrcpynW(csv, exclusionCsv, ARRAYSIZE(csv));

    WCHAR* ctx = NULL;
    WCHAR* tok = wcstok_s(csv, L",;", &ctx);
    while (tok) {
        if (token_matches_process_name(tok, exeBaseLower, exeStemLower)) return TRUE;
        tok = wcstok_s(NULL, L",;", &ctx);
    }
    return FALSE;
}

static BOOL is_window_fullscreen(HWND hwnd) {
    RECT wr = {0}, mr = {0};
    MONITORINFO mi;
    HMONITOR mon;

    if (!GetWindowRect(hwnd, &wr)) return FALSE;
    mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    if (!mon) return FALSE;

    ZeroMemory(&mi, sizeof(mi));
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(mon, &mi)) return FALSE;
    mr = mi.rcMonitor;

    // Fullscreen windows are usually flush with monitor bounds. Use a tiny tolerance
    // for DPI rounding and non-client frame quirks.
    return (abs(wr.left - mr.left) <= 2 &&
            abs(wr.top - mr.top) <= 2 &&
            abs(wr.right - mr.right) <= 2 &&
            abs(wr.bottom - mr.bottom) <= 2);
}

static BOOL is_shell_surface_window(HWND hwnd) {
    WCHAR cls[64];
    if (!hwnd) return FALSE;
    if (!GetClassNameW(hwnd, cls, ARRAYSIZE(cls))) return FALSE;
    return !lstrcmpiW(cls, L"Progman") ||
           !lstrcmpiW(cls, L"WorkerW") ||
           !lstrcmpiW(cls, L"SHELLDLL_DefView") ||
           !lstrcmpiW(cls, L"Shell_TrayWnd") ||
           !lstrcmpiW(cls, L"Shell_SecondaryTrayWnd");
}

void open_uri(LPCWSTR uri) {
    ShellExecuteW(NULL, L"open", uri, NULL, NULL, SW_SHOWNORMAL);
}

void open_shell_known(LPCWSTR verb, LPCWSTR file, LPCWSTR params) {
    SHELLEXECUTEINFOW sei = { sizeof(sei) };
    sei.fMask = SEE_MASK_FLAG_LOG_USAGE;
    sei.lpVerb = verb;
    sei.lpFile = file;
    sei.lpParameters = params;
    sei.nShow = SW_SHOWNORMAL;
    ShellExecuteExW(&sei);
}

void open_shell_item(LPCWSTR path) {
    ShellExecuteW(NULL, L"open", path, NULL, NULL, SW_SHOWNORMAL);
}

void system_sleep() {
    // Request privileges may be required for some power actions; use SetSuspendState
    // Use powrprof SetSuspendState; requires SE_SHUTDOWN_NAME for force? Usually not.
    // FALSE: Suspend, FALSE: forceCritical, FALSE: disableWakeEvent
    SetSuspendState(FALSE, FALSE, FALSE);
}

static BOOL acquire_shutdown_privilege() {
    HANDLE hToken;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken))
        return FALSE;
    TOKEN_PRIVILEGES tp = {0};
    LookupPrivilegeValue(NULL, SE_SHUTDOWN_NAME, &tp.Privileges[0].Luid);
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(tp), NULL, NULL);
    CloseHandle(hToken);
    return GetLastError() == ERROR_SUCCESS;
}

void system_shutdown(BOOL reboot) {
    acquire_shutdown_privilege();
    UINT flags = EWX_SHUTDOWN | EWX_FORCEIFHUNG;
    if (reboot) flags = EWX_REBOOT | EWX_FORCEIFHUNG;
    ExitWindowsEx(flags, SHTDN_REASON_MAJOR_OTHER);
}

void system_lock() {
    // Lock the workstation; user32 exports this
    LockWorkStation();
}

void system_logoff() {
    acquire_shutdown_privilege();
    ExitWindowsEx(EWX_LOGOFF | EWX_FORCEIFHUNG, SHTDN_REASON_MAJOR_OTHER);
}

void system_hibernate() {
    // TRUE indicates hibernate, second param forceCritical FALSE, disableWakeEvent FALSE
    SetSuspendState(TRUE, FALSE, FALSE);
}

static const WCHAR* RUN_KEY = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static const WCHAR* RUN_BACKUP_VALUE = L"WinMacMenu";

static BOOL set_run_value(LPCWSTR valueName, LPCWSTR commandLine) {
    HKEY hKey;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, NULL, 0, KEY_SET_VALUE, NULL, &hKey, NULL) != ERROR_SUCCESS)
        return FALSE;
    DWORD cb = (DWORD)((lstrlenW(commandLine) + 1) * sizeof(WCHAR));
    LONG r = RegSetValueExW(hKey, valueName, 0, REG_SZ, (const BYTE*)commandLine, cb);
    RegCloseKey(hKey);
    return (r == ERROR_SUCCESS);
}

static BOOL remove_run_value(LPCWSTR valueName) {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, KEY_SET_VALUE, &hKey) != ERROR_SUCCESS)
        return FALSE;
    LONG r = RegDeleteValueW(hKey, valueName);
    RegCloseKey(hKey);
    return (r == ERROR_SUCCESS || r == ERROR_FILE_NOT_FOUND);
}

static BOOL has_run_value(LPCWSTR valueName) {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, KEY_QUERY_VALUE, &hKey) != ERROR_SUCCESS)
        return FALSE;
    LONG r = RegQueryValueExW(hKey, valueName, NULL, NULL, NULL, NULL);
    RegCloseKey(hKey);
    return (r == ERROR_SUCCESS);
}

BOOL set_run_at_login(LPCWSTR valueName, LPCWSTR commandLine) {
    BOOL primaryOk = set_run_value(valueName, commandLine);
    // Cleanup legacy alias entry so only one Run value remains.
    if (lstrcmpiW(valueName, RUN_BACKUP_VALUE) != 0) {
        remove_run_value(RUN_BACKUP_VALUE);
    }
    return primaryOk;
}

BOOL remove_run_at_login(LPCWSTR valueName) {
    BOOL primaryOk = remove_run_value(valueName);
    BOOL backupOk = TRUE;
    // Also remove legacy alias entry if present.
    if (lstrcmpiW(valueName, RUN_BACKUP_VALUE) != 0) {
        backupOk = remove_run_value(RUN_BACKUP_VALUE);
    }
    return (primaryOk && backupOk);
}

BOOL check_run_at_login(LPCWSTR valueName) {
    return has_run_value(valueName);
}

BOOL is_process_elevated(void) {
    HANDLE hToken = NULL;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken)) return FALSE;
    TOKEN_ELEVATION elev = {0}; DWORD cb = sizeof(elev);
    BOOL elevated = FALSE;
    if (GetTokenInformation(hToken, TokenElevation, &elev, sizeof(elev), &cb)) {
        elevated = (elev.TokenIsElevated != 0);
    }
    CloseHandle(hToken);
    return elevated;
}

BOOL is_windows_11_or_greater(void) {
    typedef LONG (WINAPI *RtlGetVersionFn)(PRTL_OSVERSIONINFOW);
    RTL_OSVERSIONINFOW osvi;
    HMODULE hNtdll;
    RtlGetVersionFn rtlGetVersion;

    ZeroMemory(&osvi, sizeof(osvi));
    osvi.dwOSVersionInfoSize = sizeof(osvi);
    hNtdll = GetModuleHandleW(L"ntdll.dll");
    if (!hNtdll) return FALSE;
    rtlGetVersion = (RtlGetVersionFn)GetProcAddress(hNtdll, "RtlGetVersion");
    if (!rtlGetVersion) return FALSE;
    if (rtlGetVersion(&osvi) != 0) return FALSE;
    return (osvi.dwMajorVersion > 10) || (osvi.dwMajorVersion == 10 && osvi.dwBuildNumber >= 22000);
}

BOOL should_block_triggers_for_fullscreen(BOOL enabled, LPCWSTR exclusionCsv, HWND appWindow) {
    HWND fg;
    DWORD fgPid = 0;
    DWORD selfPid = GetCurrentProcessId();
    HANDLE hProc;
    WCHAR path[MAX_PATH];
    DWORD cch;
    WCHAR exeBase[260];
    WCHAR exeStem[260];
    WCHAR* dot;

    if (!enabled) return FALSE;

    fg = GetForegroundWindow();
    if (!fg) return FALSE;
    if (appWindow && (fg == appWindow || IsChild(appWindow, fg))) return FALSE;
    if (is_shell_surface_window(fg)) return FALSE;
    if (IsIconic(fg)) return FALSE;

    GetWindowThreadProcessId(fg, &fgPid);
    if (!fgPid || fgPid == selfPid) return FALSE;

    if (!is_window_fullscreen(fg)) return FALSE;

    hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, fgPid);
    if (!hProc) return TRUE; // Cannot identify process; safest behavior is to block.

    path[0] = 0;
    cch = ARRAYSIZE(path);
    if (!QueryFullProcessImageNameW(hProc, 0, path, &cch)) {
        CloseHandle(hProc);
        return TRUE;
    }
    CloseHandle(hProc);

    lstrcpynW(exeBase, PathFindFileNameW(path), ARRAYSIZE(exeBase));
    to_lower_inplace(exeBase);

    lstrcpynW(exeStem, exeBase, ARRAYSIZE(exeStem));
    dot = wcsrchr(exeStem, L'.');
    if (dot && !lstrcmpiW(dot, L".exe")) *dot = 0;

    if (is_process_excluded(exclusionCsv, exeBase, exeStem)) {
        return FALSE;
    }

    return TRUE;
}
