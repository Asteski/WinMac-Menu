#pragma once
#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WMM_CUSTOM_THEME_API_VERSION 3u
#define WMM_CUSTOM_THEME_DLL_NAME L"WinMacMenuCustomTheme.dll"

typedef enum WMM_CUSTOM_THEME_ITEM_KIND {
    WMM_CUSTOM_THEME_ITEM_SEPARATOR = 0,
    WMM_CUSTOM_THEME_ITEM_COMMAND = 1,
    WMM_CUSTOM_THEME_ITEM_SUBMENU = 2,
    WMM_CUSTOM_THEME_ITEM_PLACEHOLDER = 3
} WMM_CUSTOM_THEME_ITEM_KIND;

typedef enum WMM_CUSTOM_THEME_ANIMATION_DIRECTION {
    WMM_CUSTOM_THEME_ANIM_AUTO = 0,
    WMM_CUSTOM_THEME_ANIM_TOP = 1,
    WMM_CUSTOM_THEME_ANIM_BOTTOM = 2,
    WMM_CUSTOM_THEME_ANIM_LEFT = 3,
    WMM_CUSTOM_THEME_ANIM_RIGHT = 4
} WMM_CUSTOM_THEME_ANIMATION_DIRECTION;

typedef enum WMM_CUSTOM_THEME_HIGHLIGHT_FRAME {
    WMM_CUSTOM_THEME_HIGHLIGHT_BACKGROUND = 0,
    WMM_CUSTOM_THEME_HIGHLIGHT_BORDER = 1,
    WMM_CUSTOM_THEME_HIGHLIGHT_BACKGROUND_BORDER = 2
} WMM_CUSTOM_THEME_HIGHLIGHT_FRAME;

enum {
    WMM_CUSTOM_THEME_ITEM_FLAG_DISABLED = 0x0001u,
    WMM_CUSTOM_THEME_ITEM_FLAG_DEFAULT = 0x0002u
};

typedef struct WMM_CUSTOM_THEME_ITEM {
    UINT cbSize;
    UINT kind;
    UINT flags;
    UINT commandId;
    HMENU nativeSubmenu;
    HICON icon;
    WCHAR text[260];
} WMM_CUSTOM_THEME_ITEM;

typedef BOOL (WINAPI *WMM_INIT_NATIVE_SUBMENU_FN)(HWND owner, HMENU hMenu, UINT itemIndex);
typedef UINT (WINAPI *WMM_SHOW_NATIVE_SUBMENU_FN)(HWND owner, HMENU hMenu, POINT screenPt, UINT itemIndex);

typedef struct WMM_CUSTOM_THEME_SHOW_PARAMS {
    UINT cbSize;
    UINT apiVersion;
    HWND owner;
    POINT screenPt;
    BOOL pointerRelative;
    int hPlacement; // 0=left,1=center,2=right (used when pointerRelative=false)
    int vPlacement; // 0=top,1=center,2=bottom (used when pointerRelative=false)
    BOOL darkMode;
    BOOL largeRootIcons;
    HMENU nativeRootMenu;
    WMM_INIT_NATIVE_SUBMENU_FN initNativeSubmenu;
    WMM_SHOW_NATIVE_SUBMENU_FN showNativeSubmenu;
    const WMM_CUSTOM_THEME_ITEM* items;
    UINT itemCount;
    UINT animationDirection; // WMM_CUSTOM_THEME_ANIMATION_DIRECTION
    BOOL keepLargeMenuHighlightTextColor;
    UINT highlightFrame; // WMM_CUSTOM_THEME_HIGHLIGHT_FRAME
} WMM_CUSTOM_THEME_SHOW_PARAMS;

typedef struct WMM_CUSTOM_THEME_RESULT {
    UINT cbSize;
    UINT selectedCommandId;
} WMM_CUSTOM_THEME_RESULT;

typedef UINT (WINAPI *WMM_GET_API_VERSION_FN)(void);
typedef BOOL (WINAPI *WMM_SHOW_CUSTOM_THEME_FN)(const WMM_CUSTOM_THEME_SHOW_PARAMS* params, WMM_CUSTOM_THEME_RESULT* result);

#ifdef __cplusplus
}
#endif
