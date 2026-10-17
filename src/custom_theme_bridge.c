#include "custom_theme_bridge.h"
#include "custom_theme_api.h"
#include <windows.h>
#include <shlwapi.h>

#pragma comment(lib, "Shlwapi.lib")

#ifndef ARRAYSIZE
#define ARRAYSIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif

typedef struct CustomThemeBridgeState {
    HMODULE module;
    WMM_SHOW_CUSTOM_THEME_FN showFn;
    BOOL loadAttempted;
} CustomThemeBridgeState;

static CustomThemeBridgeState g_CustomThemeBridge = { 0 };

static BOOL build_custom_theme_dll_path(WCHAR* outPath, size_t cchOut) {
    DWORD len;

    if (!outPath || cchOut < MAX_PATH) return FALSE;
    len = GetModuleFileNameW(NULL, outPath, (DWORD)cchOut);
    if (!len || len >= cchOut) return FALSE;
    if (!PathRemoveFileSpecW(outPath)) return FALSE;
    if (!PathAppendW(outPath, WMM_CUSTOM_THEME_DLL_NAME)) return FALSE;
    return TRUE;
}

static void ensure_custom_theme_bridge_loaded(void) {
    WCHAR dllPath[MAX_PATH];
    WMM_GET_API_VERSION_FN versionFn;

    if (g_CustomThemeBridge.loadAttempted) return;
    g_CustomThemeBridge.loadAttempted = TRUE;

    if (!build_custom_theme_dll_path(dllPath, ARRAYSIZE(dllPath))) return;
    g_CustomThemeBridge.module = LoadLibraryW(dllPath);
    if (!g_CustomThemeBridge.module) return;

    versionFn = (WMM_GET_API_VERSION_FN)GetProcAddress(g_CustomThemeBridge.module, "WinMacMenu_GetCustomThemeApiVersion");
    g_CustomThemeBridge.showFn = (WMM_SHOW_CUSTOM_THEME_FN)GetProcAddress(g_CustomThemeBridge.module, "WinMacMenu_ShowCustomTheme");
    if (!versionFn || !g_CustomThemeBridge.showFn || versionFn() != WMM_CUSTOM_THEME_API_VERSION) {
        FreeLibrary(g_CustomThemeBridge.module);
        g_CustomThemeBridge.module = NULL;
        g_CustomThemeBridge.showFn = NULL;
    }
}

BOOL CustomThemeBridge_ShowIfAvailable(const WMM_CUSTOM_THEME_SHOW_PARAMS* params, WMM_CUSTOM_THEME_RESULT* result) {
    ensure_custom_theme_bridge_loaded();
    if (!g_CustomThemeBridge.showFn || !params) return FALSE;
    return g_CustomThemeBridge.showFn(params, result);
}
