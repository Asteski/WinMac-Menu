#include "big_menu_bridge.h"
#include "big_menu_api.h"
#include <windows.h>
#include <shlwapi.h>

#pragma comment(lib, "Shlwapi.lib")

#ifndef ARRAYSIZE
#define ARRAYSIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif

typedef struct BigMenuBridgeState {
    HMODULE module;
    WMM_SHOW_BIG_MENU_FN showFn;
    BOOL loadAttempted;
} BigMenuBridgeState;

static BigMenuBridgeState g_bigMenuBridge = { 0 };

static BOOL build_big_menu_dll_path(WCHAR* outPath, size_t cchOut) {
    DWORD len;

    if (!outPath || cchOut < MAX_PATH) return FALSE;
    len = GetModuleFileNameW(NULL, outPath, (DWORD)cchOut);
    if (!len || len >= cchOut) return FALSE;
    if (!PathRemoveFileSpecW(outPath)) return FALSE;
    if (!PathAppendW(outPath, WMM_BIG_MENU_DLL_NAME)) return FALSE;
    return TRUE;
}

static void ensure_big_menu_bridge_loaded(void) {
    WCHAR dllPath[MAX_PATH];
    WMM_GET_API_VERSION_FN versionFn;

    if (g_bigMenuBridge.loadAttempted) return;
    g_bigMenuBridge.loadAttempted = TRUE;

    if (!build_big_menu_dll_path(dllPath, ARRAYSIZE(dllPath))) return;
    g_bigMenuBridge.module = LoadLibraryW(dllPath);
    if (!g_bigMenuBridge.module) return;

    versionFn = (WMM_GET_API_VERSION_FN)GetProcAddress(g_bigMenuBridge.module, "WinMacMenu_GetBigMenuApiVersion");
    g_bigMenuBridge.showFn = (WMM_SHOW_BIG_MENU_FN)GetProcAddress(g_bigMenuBridge.module, "WinMacMenu_ShowBigMenu");
    if (!versionFn || !g_bigMenuBridge.showFn || versionFn() != WMM_BIG_MENU_API_VERSION) {
        FreeLibrary(g_bigMenuBridge.module);
        g_bigMenuBridge.module = NULL;
        g_bigMenuBridge.showFn = NULL;
    }
}

BOOL BigMenuBridge_ShowIfAvailable(const WMM_BIG_MENU_SHOW_PARAMS* params, WMM_BIG_MENU_RESULT* result) {
    ensure_big_menu_bridge_loaded();
    if (!g_bigMenuBridge.showFn || !params) return FALSE;
    return g_bigMenuBridge.showFn(params, result);
}
