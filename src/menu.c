// Clean implementation of menu building and handlers (sorting removed, visibility filters kept)

#include <windows.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <shlobj.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <vsstyle.h>
#include <tlhelp32.h>
#include <stdlib.h> // _wtoi
#pragma comment(lib, "Dwmapi.lib")
#pragma comment(lib, "UxTheme.lib")
#include "menu.h"
#include "big_menu_bridge.h"
#include "config.h"
#include "recent.h"
#include "util.h"
#include "theme.h"

#ifndef ARRAYSIZE
#define ARRAYSIZE(a) (sizeof(a)/sizeof((a)[0]))
#endif

#define IDM_DYNAMIC_BASE  1000
#define IDM_RECENT_BASE   2000
#define IDM_TASKKILL_BASE 3000
#define IDM_SIZER         9000
#define IDT_MENU_WINDOW_REFRESH 0x5A11

BOOL g_shouldReopenMenu = FALSE;

#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif

typedef struct MapEntry {
    UINT id;
    WCHAR path[MAX_PATH];
} MapEntry;

typedef struct FolderMenuData {
    WCHAR path[MAX_PATH];
    int depth;
    int offset;
    BOOL forceLinks;
    BOOL allowMixedIcons;
} FolderMenuData;

// Forward declaration
static void attach_menu_data(HMENU hMenu, const WCHAR* path, int depth, int offset, BOOL forceLinks, BOOL allowMixedIcons);


static Config g_cfg; // loaded on demand
static MapEntry g_map[4096];
static UINT g_mapCount = 0;
static UINT g_nextFolderId = IDM_FOLDER_BASE;
typedef struct ItemIcon { UINT id; HICON h; } ItemIcon;
static ItemIcon g_itemIcons[256];
static UINT g_itemIconCount = 0;
typedef struct ItemBmp { UINT id; HBITMAP hbmp; } ItemBmp;
static ItemBmp g_itemBmps[256];
static UINT g_itemBmpCount = 0;
static UINT g_suppressCmd = 0;
static DWORD g_suppressCmdTick = 0;
static HMENU g_rootMenuHandle = NULL;
static UINT g_legacyRootMenuWidth = 0;
static UINT g_legacyRootMenuItemHeight = 0;
static HHOOK g_menuCornerHook = NULL;
static HWND g_rootMenuWindow = NULL;
// Forward declarations for legacy icon helpers
static HBITMAP icon_to_hbmp(HICON hico, int cx, int cy);
static void assign_legacy_item_bitmap(HMENU hMenu, UINT id, HICON hico);
static void assign_menu_item_bitmap_sized(HMENU hMenu, UINT id, HICON hico, int size);
static void assign_menu_pos_bitmap_sized(HMENU hMenu, int pos, HICON hico, int size);
static HFONT get_menu_font(void);
static BOOL measure_legacy_root_menu_item(HWND owner, MEASUREITEMSTRUCT* mis);
static BOOL draw_legacy_root_menu_item(HWND owner, const DRAWITEMSTRUCT* dis);
static void set_legacy_root_owner_draw(HMENU hMenu);
static UINT build_big_menu_snapshot(HMENU hMenu, WMM_BIG_MENU_ITEM* items, UINT maxItems);
static BOOL WINAPI big_menu_init_native_submenu_callback(HWND owner, HMENU hMenu, UINT itemIndex);
static UINT WINAPI big_menu_show_native_submenu_callback(HWND owner, HMENU hMenu, POINT screenPt, UINT itemIndex);
static void begin_menu_corner_hook(void);
static void end_menu_corner_hook(void);
static void draw_fake_rounded_highlight(HDC hdc, const RECT* rc, int radius, COLORREF fillColor, COLORREF backgroundColor);
static void draw_highlight_border(HDC hdc, const RECT* rc, int radius, COLORREF borderColor);
static COLORREF blend_colors(COLORREF base, COLORREF top, int alpha);
static void draw_root_menu_submenu_arrow(HDC hdc, const RECT* rc, COLORREF color);
static void add_item_icon(UINT id, HICON h) {
    if (!h) return;
    for (UINT i=0;i<g_itemIconCount;i++) if (g_itemIcons[i].id==id) { g_itemIcons[i].h=h; return; }
    if (g_itemIconCount < ARRAYSIZE(g_itemIcons)) { g_itemIcons[g_itemIconCount++] = (ItemIcon){ id, h }; }
}
static HICON get_item_icon(UINT id) {
    for (UINT i=0;i<g_itemIconCount;i++) if (g_itemIcons[i].id==id) return g_itemIcons[i].h;
    return NULL;
}

static int get_small_icon_size() {
    HDC hdc = GetDC(NULL);
    int dpi = GetDeviceCaps(hdc, LOGPIXELSX);
    ReleaseDC(NULL, hdc);
    return MulDiv(16, dpi, 96);
}

static int get_root_icon_size() {
    HDC hdc = GetDC(NULL);
    int dpi = GetDeviceCaps(hdc, LOGPIXELSX);
    ReleaseDC(NULL, hdc);
    return MulDiv(g_cfg.rootMenuLargeIcons ? 32 : 16, dpi, 96);
}

static BOOL use_legacy_root_large_icon_layout(void) {
    return (g_cfg.menuStyle == STYLE_LEGACY && g_cfg.showIcons == 1 && g_cfg.rootMenuLargeIcons);
}

static BOOL WINAPI big_menu_init_native_submenu_callback(HWND owner, HMENU hMenu, UINT itemIndex) {
    MenuOnInitMenuPopup(owner, hMenu, itemIndex, FALSE);
    return TRUE;
}

static UINT WINAPI big_menu_show_native_submenu_callback(HWND owner, HMENU hMenu, POINT screenPt, UINT itemIndex) {
    UINT cmd;

    MenuOnInitMenuPopup(owner, hMenu, itemIndex, FALSE);
    SetForegroundWindow(owner);
    cmd = TrackPopupMenuEx(
        hMenu,
        TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_LEFTALIGN | TPM_TOPALIGN | TPM_VERTICAL | TPM_RECURSE,
        screenPt.x,
        screenPt.y,
        owner,
        NULL);
    PostMessageW(owner, WM_NULL, 0, 0);
    return cmd;
}

static UINT build_big_menu_snapshot(HMENU hMenu, WMM_BIG_MENU_ITEM* items, UINT maxItems) {
    int count;
    UINT written = 0;

    if (!hMenu || !items || maxItems == 0) return 0;

    count = GetMenuItemCount(hMenu);
    if (count <= 0) return 0;

    for (int i = 0; i < count && written < maxItems; ++i) {
        MENUITEMINFOW mii = { sizeof(mii) };
        WMM_BIG_MENU_ITEM* item = &items[written];

        ZeroMemory(item, sizeof(*item));
        item->cbSize = sizeof(*item);

        mii.fMask = MIIM_FTYPE | MIIM_STATE | MIIM_ID | MIIM_SUBMENU;
        if (!GetMenuItemInfoW(hMenu, i, TRUE, &mii)) {
            continue;
        }

        if (mii.fType & MFT_SEPARATOR) {
            item->kind = WMM_BIG_MENU_ITEM_SEPARATOR;
        } else if (mii.hSubMenu) {
            item->kind = WMM_BIG_MENU_ITEM_SUBMENU;
        } else if (mii.wID == 0 || (mii.fState & (MFS_DISABLED | MFS_GRAYED))) {
            item->kind = WMM_BIG_MENU_ITEM_PLACEHOLDER;
        } else {
            item->kind = WMM_BIG_MENU_ITEM_COMMAND;
        }

        if (mii.fState & (MFS_DISABLED | MFS_GRAYED)) item->flags |= WMM_BIG_MENU_ITEM_FLAG_DISABLED;
        if (mii.fState & MFS_DEFAULT) item->flags |= WMM_BIG_MENU_ITEM_FLAG_DEFAULT;

        item->commandId = mii.wID;
        item->nativeSubmenu = mii.hSubMenu;
        item->icon = get_item_icon(mii.wID);
        if (!(mii.fType & MFT_SEPARATOR)) {
            GetMenuStringW(hMenu, i, item->text, ARRAYSIZE(item->text), MF_BYPOSITION);
        }

        written++;
    }

    return written;
}

static COLORREF blend_colors(COLORREF base, COLORREF top, int alpha) {
    int inv = 255 - alpha;
    int r = (GetRValue(base) * inv + GetRValue(top) * alpha) / 255;
    int g = (GetGValue(base) * inv + GetGValue(top) * alpha) / 255;
    int b = (GetBValue(base) * inv + GetBValue(top) * alpha) / 255;
    return RGB(r, g, b);
}

static void draw_fake_rounded_highlight(HDC hdc, const RECT* rc, int radius, COLORREF fillColor, COLORREF backgroundColor) {
    RECT fill = *rc;
    HBRUSH hFill;

    if (radius <= 0) {
        hFill = CreateSolidBrush(fillColor);
        if (hFill) {
            FillRect(hdc, &fill, hFill);
            DeleteObject(hFill);
        } else {
            FillRect(hdc, &fill, GetSysColorBrush(COLOR_HIGHLIGHT));
        }
        return;
    }

    hFill = CreateSolidBrush(fillColor);
    if (!hFill) {
        FillRect(hdc, &fill, GetSysColorBrush(COLOR_HIGHLIGHT));
        return;
    }
    FillRect(hdc, &fill, hFill);
    DeleteObject(hFill);

    for (int y = 0; y < radius; ++y) {
        for (int x = 0; x < radius; ++x) {
            enum { SUBSAMPLES = 6 };
            int inside = 0;
            int total = SUBSAMPLES * SUBSAMPLES;
            COLORREF color;

            for (int sy = 0; sy < SUBSAMPLES; ++sy) {
                for (int sx = 0; sx < SUBSAMPLES; ++sx) {
                    double sampleX = (double)x + ((double)sx + 0.5) / (double)SUBSAMPLES;
                    double sampleY = (double)y + ((double)sy + 0.5) / (double)SUBSAMPLES;
                    double dx = (double)radius - sampleX;
                    double dy = (double)radius - sampleY;
                    if ((dx * dx + dy * dy) <= (double)(radius * radius)) {
                        inside++;
                    }
                }
            }

            if (inside == total) continue;
            if (inside <= 0) {
                color = backgroundColor;
            } else {
                int alpha = (inside * 255) / total;
                color = blend_colors(backgroundColor, fillColor, alpha);
            }

            SetPixelV(hdc, rc->left + x, rc->top + y, color);
            SetPixelV(hdc, rc->right - 1 - x, rc->top + y, color);
            SetPixelV(hdc, rc->left + x, rc->bottom - 1 - y, color);
            SetPixelV(hdc, rc->right - 1 - x, rc->bottom - 1 - y, color);
        }
    }

}

static void draw_highlight_border(HDC hdc, const RECT* rc, int radius, COLORREF borderColor) {
    HPEN pen;
    HPEN oldPen;
    HBRUSH oldBrush;

    if (!hdc || !rc) return;
    pen = CreatePen(PS_SOLID, 1, borderColor);
    if (!pen) return;
    oldPen = (HPEN)SelectObject(hdc, pen);
    oldBrush = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
    if (radius > 0) {
        RoundRect(hdc, rc->left, rc->top, rc->right, rc->bottom, radius * 2, radius * 2);
    } else {
        Rectangle(hdc, rc->left, rc->top, rc->right, rc->bottom);
    }
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);
}

static void apply_menu_window_corners(HWND hWnd) {
    if (!hWnd) return;
    {
        RECT rc;
        int width;
        int height;
        HRGN hrgn;
        HDC hdc;
        int dpi;
        int radius;

        if (!GetWindowRect(hWnd, &rc)) return;
        width = rc.right - rc.left;
        height = rc.bottom - rc.top;
        if (width <= 0 || height <= 0) return;

        if (hWnd != g_rootMenuWindow) {
            if (is_windows_11_or_greater()) {
                DWM_WINDOW_CORNER_PREFERENCE pref = DWMWCP_DEFAULT;
                DwmSetWindowAttribute(hWnd, DWMWA_WINDOW_CORNER_PREFERENCE, &pref, sizeof(pref));
            }
            SetWindowRgn(hWnd, NULL, TRUE);
            return;
        }

        if (is_windows_11_or_greater()) {
            DWM_WINDOW_CORNER_PREFERENCE pref = DWMWCP_ROUNDSMALL;
            DwmSetWindowAttribute(hWnd, DWMWA_WINDOW_CORNER_PREFERENCE, &pref, sizeof(pref));
            SetWindowRgn(hWnd, NULL, TRUE);
            return;
        }

        hdc = GetDC(hWnd);
        dpi = hdc ? GetDeviceCaps(hdc, LOGPIXELSX) : 96;
        if (hdc) ReleaseDC(hWnd, hdc);
        radius = is_windows_11_or_greater() ? MulDiv(4, dpi, 96) : 0;

        if (radius > 0) {
            hrgn = CreateRoundRectRgn(0, 0, width + 1, height + 1, radius, radius);
        } else {
            hrgn = CreateRectRgn(0, 0, width + 1, height + 1);
        }
        if (!hrgn) return;
        SetWindowRgn(hWnd, hrgn, TRUE);
    }
}

static LRESULT CALLBACK menu_corner_cbt_proc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HCBT_CREATEWND || nCode == HCBT_ACTIVATE) {
        HWND hWnd = (HWND)wParam;
        WCHAR cls[64];
        if (hWnd && GetClassNameW(hWnd, cls, ARRAYSIZE(cls)) && !lstrcmpW(cls, L"#32768")) {
            if (!g_rootMenuWindow) g_rootMenuWindow = hWnd;
            apply_menu_window_corners(hWnd);
        }
    }
    return CallNextHookEx(g_menuCornerHook, nCode, wParam, lParam);
}

static void begin_menu_corner_hook(void) {
    if (g_menuCornerHook || !is_windows_11_or_greater()) return;
    g_menuCornerHook = SetWindowsHookExW(WH_CBT, menu_corner_cbt_proc, NULL, GetCurrentThreadId());
}

static void end_menu_corner_hook(void) {
    if (!g_menuCornerHook) return;
    UnhookWindowsHookEx(g_menuCornerHook);
    g_menuCornerHook = NULL;
}

static BOOL CALLBACK enum_menu_windows_proc(HWND hWnd, LPARAM lParam) {
    WCHAR cls[64];
    UNREFERENCED_PARAMETER(lParam);
    if (GetClassNameW(hWnd, cls, ARRAYSIZE(cls)) && !lstrcmpW(cls, L"#32768")) {
        if (!g_rootMenuWindow) g_rootMenuWindow = hWnd;
        apply_menu_window_corners(hWnd);
    }
    return TRUE;
}

void MenuRefreshVisibleMenuWindows(void) {
    EnumThreadWindows(GetCurrentThreadId(), enum_menu_windows_proc, 0);
}

static void draw_root_menu_submenu_arrow(HDC hdc, const RECT* rc, COLORREF color) {
    HPEN pen;
    HPEN oldPen;
    int dpi;
    int stroke;
    int arrowWidth;
    int arrowHeight;
    int x0;
    int y0;
    POINT pts[3];

    if (!hdc || !rc) return;

    dpi = GetDeviceCaps(hdc, LOGPIXELSX);
    stroke = max(1, MulDiv(1, dpi, 96));
    arrowWidth = MulDiv(5, dpi, 96);
    arrowHeight = MulDiv(9, dpi, 96);
    x0 = rc->right - MulDiv(10, dpi, 96) - arrowWidth;
    y0 = rc->top + ((rc->bottom - rc->top) - arrowHeight) / 2;

    pts[0].x = x0;
    pts[0].y = y0;
    pts[1].x = x0 + arrowWidth;
    pts[1].y = y0 + (arrowHeight / 2);
    pts[2].x = x0;
    pts[2].y = y0 + arrowHeight;

    pen = CreatePen(PS_SOLID, stroke, color);
    if (!pen) return;

    oldPen = (HPEN)SelectObject(hdc, pen);
    MoveToEx(hdc, pts[0].x, pts[0].y, NULL);
    LineTo(hdc, pts[1].x, pts[1].y);
    LineTo(hdc, pts[2].x, pts[2].y);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);
}

// Assign an icon (converted to bitmap) to the most recently added item (typically a popup root)
static void assign_icon_to_last_popup(HMENU hMenu, HICON hico) {
    if (!hico) return;
    int count = GetMenuItemCount(hMenu);
    if (count <= 0) return;
    assign_menu_pos_bitmap_sized(hMenu, count - 1, hico, get_root_icon_size());
}

// Helper: attempt to load an icon from either a direct .ico file path (existing logic)
// or from a module (DLL/EXE) with an index specified using ",<index>" syntax.
// Accepted forms:
//   C:\Windows\System32\shell32.dll,10
//   shell32.dll,10            (searches System32)
//   imageres.dll,-3           (negative index still passed through; Windows treats as resource ID)
// If no comma present, falls back to LoadImageW for .ico file.
static HICON load_icon_path_or_module_sized(const WCHAR* spec, int size) {
    HICON hSmall = NULL;
    HICON hLarge = NULL;
    const WCHAR* comma;
    WCHAR module[MAX_PATH];
    size_t len;
    const WCHAR* idxStr;
    int idx;
    WCHAR expanded[MAX_PATH];
    UINT extracted;
    if (!spec || !spec[0]) return NULL;
    comma = wcschr(spec, L',');
    if (!comma) {
        return (HICON)LoadImageW(NULL, spec, IMAGE_ICON, size, size, LR_LOADFROMFILE|LR_SHARED);
    }
    // Copy module portion up to (but not including) comma.
    len = (size_t)(comma - spec);
    if (len >= MAX_PATH) len = MAX_PATH - 1;
    {
        size_t i;
        for (i=0;i<len;i++) module[i] = spec[i];
        module[len] = 0;
    }
    idxStr = comma + 1;
    idx = _wtoi(idxStr); // handles negative values
    // If no backslash at all, assume System32 for bare module names like 'shell32.dll'.
    if (!wcschr(module, L'\\')) {
        WCHAR sysdir[MAX_PATH];
        if (SHGetSpecialFolderPathW(NULL, sysdir, CSIDL_SYSTEM, FALSE)) {
            WCHAR combined[MAX_PATH];
            lstrcpynW(combined, sysdir, ARRAYSIZE(combined));
            PathAppendW(combined, module);
            lstrcpynW(module, combined, ARRAYSIZE(module));
        }
    }
    // Expand any environment variables.
    if (ExpandEnvironmentStringsW(module, expanded, ARRAYSIZE(expanded)) && expanded[0]) {
        lstrcpynW(module, expanded, ARRAYSIZE(module));
    }
    if (size > get_small_icon_size()) {
        UINT extractedPrivate = PrivateExtractIconsW(module, idx, size, size, &hLarge, NULL, 1, 0);
        if (extractedPrivate > 0 && hLarge) {
            return hLarge;
        }
    }
    extracted = ExtractIconExW(module, idx, &hLarge, &hSmall, 1);
    if (extracted == 0) {
        return (HICON)LoadImageW(NULL, spec, IMAGE_ICON, size, size, LR_LOADFROMFILE|LR_SHARED);
    }
    if (size > get_small_icon_size()) {
        if (hSmall) DestroyIcon(hSmall);
        return hLarge;
    }
    if (hLarge) DestroyIcon(hLarge);
    return hSmall;
}

static HICON load_icon_path_or_module(const WCHAR* spec) {
    return load_icon_path_or_module_sized(spec, get_small_icon_size());
}

static HICON load_root_icon_path_or_module(const WCHAR* spec) {
    return load_icon_path_or_module_sized(spec, get_root_icon_size());
}

static HICON get_system_folder_icon_sized(BOOL large) {
    static HICON hFolderSmall = NULL;
    static HICON hFolderLarge = NULL;
    HICON* cache = large ? &hFolderLarge : &hFolderSmall;
    if (*cache) return *cache;
    // Use a fake folder name to ensure we get the standard folder icon, not drive icon
    SHFILEINFOW sfi = {0};
    UINT flags = SHGFI_ICON | SHGFI_USEFILEATTRIBUTES | (large ? SHGFI_LARGEICON : SHGFI_SMALLICON);
    if (SHGetFileInfoW(L"FakeFolderName", FILE_ATTRIBUTE_DIRECTORY, &sfi, sizeof(sfi), flags)) {
        *cache = sfi.hIcon;
    }
    return *cache;
}

static HICON get_system_folder_icon(void) {
    return get_system_folder_icon_sized(FALSE);
}

static void map_add(UINT id, const WCHAR* path) {
    if (g_mapCount < ARRAYSIZE(g_map)) {
        g_map[g_mapCount].id = id;
        lstrcpynW(g_map[g_mapCount].path, path, ARRAYSIZE(g_map[g_mapCount].path));
        g_mapCount++;
    }
}

static const WCHAR* map_find(UINT id) {
    for (UINT i = 0; i < g_mapCount; ++i) {
        if (g_map[i].id == id) return g_map[i].path;
    }
    return NULL;
}

static BOOL is_filesystem_target(const WCHAR* path);

static BOOL resolve_filesystem_path_by_command(UINT cmd, WCHAR* outPath, size_t cchOut) {
    if (!outPath || cchOut == 0) return FALSE;
    outPath[0] = 0;

    const WCHAR* mapped = map_find(cmd);
    if (mapped && is_filesystem_target(mapped)) {
        lstrcpynW(outPath, mapped, (int)cchOut);
        return TRUE;
    }

    if (cmd >= IDM_RECENT_BASE && cmd < IDM_RECENT_BASE + 1000 && cmd != IDM_RECENT_BASE + 900) {
        RecentItem* items = NULL;
        int n = recent_get_items(&items, g_cfg.recentMax > 0 ? g_cfg.recentMax : 12);
        int idx = (int)(cmd - IDM_RECENT_BASE);
        if (idx >= 0 && idx < n && items && is_filesystem_target(items[idx].path)) {
            lstrcpynW(outPath, items[idx].path, (int)cchOut);
            LocalFree(items);
            return TRUE;
        }
        if (items) LocalFree(items);
    }

    UINT id = IDM_DYNAMIC_BASE;
    for (int i = 0; i < g_cfg.count; ++i) {
        ConfigItem* it = &g_cfg.items[i];
        switch (it->type) {
        case CI_SEPARATOR:
        case CI_CATEGORY:
        case CI_FOLDER_SUBMENU:
        case CI_RECENT_SUBMENU:
        case CI_POWER_MENU:
        case CI_THISPC:
        case CI_HOME:
        case CI_TASKKILL:
            break;
        case CI_URI:
        case CI_CMD:
            id++;
            break;
        case CI_FILE:
            if (id == cmd && is_filesystem_target(it->path)) {
                lstrcpynW(outPath, it->path, (int)cchOut);
                return TRUE;
            }
            id++;
            break;
        case CI_FOLDER:
            if (!it->submenu) {
                if (!it->inlineExpand) {
                    if (id == cmd && is_filesystem_target(it->path)) {
                        lstrcpynW(outPath, it->path, (int)cchOut);
                        return TRUE;
                    }
                    id++;
                }
            }
            break;
        case CI_POWER_SLEEP:
        case CI_POWER_SHUTDOWN:
        case CI_POWER_RESTART:
        case CI_POWER_LOCK:
        case CI_POWER_LOGOFF:
        case CI_POWER_HIBERNATE:
            id++;
            break;
        }
    }

    return FALSE;
}

static BOOL is_filesystem_target(const WCHAR* path) {
    if (!path || !path[0]) return FALSE;
    if (!PathFileExistsW(path)) return FALSE;
    DWORD attrs = GetFileAttributesW(path);
    return (attrs != INVALID_FILE_ATTRIBUTES);
}

static BOOL show_shell_context_menu_for_path(HWND owner, const WCHAR* path, POINT pt) {
    BOOL handled = FALSE;
    LPITEMIDLIST pidl = NULL;
    IShellFolder* parent = NULL;
    LPCITEMIDLIST child = NULL;
    IContextMenu* pcm = NULL;
    HMENU hCtx = NULL;

    HRESULT hrCo = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    BOOL coInit = SUCCEEDED(hrCo);

    if (!path || !path[0]) goto cleanup;
    if (!is_filesystem_target(path)) goto cleanup;

    if (FAILED(SHParseDisplayName(path, NULL, &pidl, 0, NULL))) goto cleanup;
    if (FAILED(SHBindToParent(pidl, &IID_IShellFolder, (void**)&parent, &child))) goto cleanup;
    if (FAILED(parent->lpVtbl->GetUIObjectOf(parent, owner, 1, &child, &IID_IContextMenu, NULL, (void**)&pcm))) goto cleanup;

    hCtx = CreatePopupMenu();
    if (!hCtx) goto cleanup;

    UINT qFlags = CMF_NORMAL;
    if (GetKeyState(VK_SHIFT) & 0x8000) qFlags |= CMF_EXTENDEDVERBS;
    if (FAILED(pcm->lpVtbl->QueryContextMenu(pcm, hCtx, 0, 1, 0x7FFF, qFlags))) goto cleanup;

    SetForegroundWindow(owner);
    UINT cmd = TrackPopupMenuEx(hCtx, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RECURSE, pt.x, pt.y, owner, NULL);
    PostMessageW(owner, WM_NULL, 0, 0);

    if (cmd >= 1) {
        CMINVOKECOMMANDINFOEX cmi;
        ZeroMemory(&cmi, sizeof(cmi));
        cmi.cbSize = sizeof(cmi);
        cmi.fMask = CMIC_MASK_UNICODE;
        cmi.hwnd = owner;
        cmi.lpVerb = (LPCSTR)MAKEINTRESOURCEA(cmd - 1);
        cmi.lpVerbW = (LPCWSTR)MAKEINTRESOURCEW(cmd - 1);
        cmi.nShow = SW_SHOWNORMAL;
        if (SUCCEEDED(pcm->lpVtbl->InvokeCommand(pcm, (LPCMINVOKECOMMANDINFO)&cmi))) {
            handled = TRUE;
        }
        if (!g_cfg.keepMenuOpenAfterContextAction) {
            EndMenu();
        }
    }
    g_shouldReopenMenu = FALSE;

cleanup:
    if (hCtx) DestroyMenu(hCtx);
    if (pcm) pcm->lpVtbl->Release(pcm);
    if (parent) parent->lpVtbl->Release(parent);
    if (pidl) CoTaskMemFree(pidl);
    if (coInit) CoUninitialize();
    return handled;
}

static void attach_menu_data(HMENU hMenu, const WCHAR* path, int depth, int offset, BOOL forceLinks, BOOL allowMixedIcons) {
    FolderMenuData* data = (FolderMenuData*)LocalAlloc(LMEM_FIXED|LMEM_ZEROINIT, sizeof(FolderMenuData));
    if (!data) return;
    lstrcpynW(data->path, path, ARRAYSIZE(data->path));
    data->depth = depth;
    data->offset = offset;
    data->forceLinks = forceLinks;
    data->allowMixedIcons = allowMixedIcons;
    MENUINFO mi = { sizeof(mi) };
    mi.fMask = MIM_MENUDATA;
    mi.dwMenuData = (ULONG_PTR)data;
    SetMenuInfo(hMenu, &mi);
}

static HICON get_file_icon_sized(const WCHAR* path, BOOL large) {
    SHFILEINFOW sfi = {0};
    UINT flags = SHGFI_ICON | (large ? SHGFI_LARGEICON : SHGFI_SMALLICON);
    if (SHGetFileInfoW(path, 0, &sfi, sizeof(sfi), flags)) {
        return sfi.hIcon;
    }
    return NULL;
}

static HICON get_file_icon(const WCHAR* path) {
    return get_file_icon_sized(path, FALSE);
}

static BOOL recent_item_is_folder(const RecentItem* item) {
    if (!item || !item->path[0]) return FALSE;
    return item->isFolder;
}

static void append_recent_item_to_menu(HMENU sub, const RecentItem* items, int i) {
    WCHAR text[MAX_PATH + 8];
    if (!sub || !items || !items[i].path[0]) return;

    if (g_cfg.recentLabelMode == 1) {
        const WCHAR* p = wcsrchr(items[i].path, L'\\');
        const WCHAR* name = p ? p + 1 : items[i].path;
        lstrcpynW(text, name, ARRAYSIZE(text));
        BOOL stripExt = FALSE;
        if (!g_cfg.recentShowExtensions) stripExt = TRUE;
        else if (!g_cfg.showExtensions) stripExt = TRUE;
        if (stripExt && text[0] != L'.') {
            WCHAR* dot = wcsrchr(text, L'.');
            if (dot) *dot = 0;
        }
    } else {
        lstrcpynW(text, items[i].path, ARRAYSIZE(text));
    }
    AppendMenuW(sub, MF_STRING, IDM_RECENT_BASE + i, text);
    if (g_cfg.recentShowIcons) {
        HICON hIcon = get_file_icon(items[i].path);
        if (hIcon) {
            add_item_icon(IDM_RECENT_BASE + i, hIcon);
            if (g_cfg.menuStyle == STYLE_LEGACY && g_cfg.showIcons) {
                assign_legacy_item_bitmap(sub, IDM_RECENT_BASE + i, hIcon);
            }
        }
    }
}

static HMENU build_recent_submenu(void) {
    HMENU sub = CreatePopupMenu();
    RecentItem* items = NULL;
    int maxItems = (g_cfg.recentMax > 0 ? g_cfg.recentMax : 12);
    int n = recent_get_items(&items, maxItems);
    if (n <= 0) {
        AppendMenuW(sub, MF_STRING | MF_GRAYED, 0, L"(None)");
        if (items) LocalFree(items);
        if (g_cfg.recentShowCleanItems) {
            AppendMenuW(sub, MF_SEPARATOR, 0, NULL);
            AppendMenuW(sub, MF_STRING, IDM_RECENT_BASE + 900, L"Clear Recent Items list");
        }
        return sub;
    }
    if (g_cfg.separateItems) {
        BOOL addedFolders = FALSE;
        BOOL addedFiles = FALSE;
        for (int i = 0; i < n; ++i) {
            if (!items[i].path[0] || recent_item_is_folder(&items[i])) continue;
            if (!addedFiles) AppendMenuW(sub, MF_STRING | MF_GRAYED, 0, L"Files");
            append_recent_item_to_menu(sub, items, i);
            addedFiles = TRUE;
        }
        for (int i = 0; i < n; ++i) {
            if (!items[i].path[0] || !recent_item_is_folder(&items[i])) continue;
            if (!addedFolders) {
                if (addedFiles) AppendMenuW(sub, MF_SEPARATOR, 0, NULL);
                AppendMenuW(sub, MF_STRING | MF_GRAYED, 0, L"Folders");
            }
            append_recent_item_to_menu(sub, items, i);
            addedFolders = TRUE;
        }
    } else {
        for (int i = 0; i < n; ++i) {
            append_recent_item_to_menu(sub, items, i);
        }
    }
    if (items) LocalFree(items);
    if (g_cfg.recentShowCleanItems) {
        AppendMenuW(sub, MF_SEPARATOR, 0, NULL);
        AppendMenuW(sub, MF_STRING, IDM_RECENT_BASE + 900, L"Clear Recent Items list");
    }
    return sub;
}

static void get_name_from_path(const WCHAR* full, WCHAR* name, size_t cch) {
    const WCHAR* p = wcsrchr(full, L'\\');
    lstrcpynW(name, p ? p + 1 : full, (int)cch);
    if (!g_cfg.showExtensions && name[0] != L'.') {
        WCHAR* dot = wcsrchr(name, L'.');
        if (dot) *dot = 0;
    }
}

typedef struct FileItem {
    WCHAR name[MAX_PATH];
    WCHAR fullPath[MAX_PATH];
    BOOL isDir;
    FILETIME ftLastWrite;
    FILETIME ftCreation;
    unsigned long long fileSize;
} FileItem;

static int compare_files(const void* a, const void* b) {
    const FileItem* fa = (const FileItem*)a;
    const FileItem* fb = (const FileItem*)b;

    // Object type priority logic
    if (g_cfg.sortObjectTypePriority == 1) { // Folders first
        if (fa->isDir && !fb->isDir) return -1;
        if (!fa->isDir && fb->isDir) return 1;
    } else if (g_cfg.sortObjectTypePriority == 2) { // Files first
        if (!fa->isDir && fb->isDir) return -1;
        if (fa->isDir && !fb->isDir) return 1;
    }

    int res = 0;
    switch (g_cfg.sortField) {
        case SORT_DATE_MODIFIED:
            res = CompareFileTime(&fa->ftLastWrite, &fb->ftLastWrite);
            break;
        case SORT_DATE_CREATED:
            res = CompareFileTime(&fa->ftCreation, &fb->ftCreation);
            break;
        case SORT_SIZE:
            if (fa->fileSize < fb->fileSize) res = -1;
            else if (fa->fileSize > fb->fileSize) res = 1;
            break;
        case SORT_TYPE:
        {
            const WCHAR* extA = wcsrchr(fa->name, L'.');
            const WCHAR* extB = wcsrchr(fb->name, L'.');
            if (!extA) extA = L"";
            if (!extB) extB = L"";
            res = lstrcmpiW(extA, extB);
            break;
        }
        case SORT_NAME:
        default:
            res = lstrcmpiW(fa->name, fb->name);
            break;
    }

    if (g_cfg.sortDescending) res = -res;
    
    // Fallback to name if equal (always ascending for stability)
    if (res == 0) {
        res = lstrcmpiW(fa->name, fb->name);
    }
    return res;
}

static int fill_menu_with_folder(HMENU hMenu, int insertPos, const WCHAR* path, int depth, int offset, BOOL forceLinks, BOOL allowMixedIcons, BOOL useLargeRootIcons) {
    WIN32_FIND_DATAW fd; WCHAR pattern[MAX_PATH];
    PathCombineW(pattern, path, L"*");
    HANDLE h = FindFirstFileExW(pattern, FindExInfoBasic, &fd, FindExSearchNameMatch, NULL, FIND_FIRST_EX_LARGE_FETCH);
    if (h == INVALID_HANDLE_VALUE) {
        InsertMenuW(hMenu, insertPos, MF_BYPOSITION | MF_STRING | MF_GRAYED, 0, L"(Empty)");
        return 1;
    }

    // Collect items
    FileItem* items = NULL;
    int count = 0;
    int capacity = 0;

    do {
        if (!lstrcmpW(fd.cFileName, L".") || !lstrcmpW(fd.cFileName, L"..")) continue;
        BOOL isDot = (fd.cFileName[0] == L'.');
        if (!g_cfg.showHidden && (fd.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN)) {
            if (!(isDot && g_cfg.dotMode > 0)) continue;
        }
        if (isDot) {
            if (g_cfg.dotMode == 0) continue;
            BOOL isDir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            if (isDir && g_cfg.dotMode == 1) continue;
            if (!isDir && g_cfg.dotMode == 2) continue;
        }

        if (count >= capacity) {
            capacity = (capacity == 0) ? 16 : capacity * 2;
            FileItem* newItems = (FileItem*)LocalAlloc(LMEM_FIXED, capacity * sizeof(FileItem));
            if (items) {
                memcpy(newItems, items, count * sizeof(FileItem));
                LocalFree(items);
            }
            items = newItems;
        }
        
        lstrcpynW(items[count].name, fd.cFileName, ARRAYSIZE(items[count].name));
        PathCombineW(items[count].fullPath, path, fd.cFileName);
        items[count].isDir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        items[count].ftLastWrite = fd.ftLastWriteTime;
        items[count].ftCreation = fd.ftCreationTime;
        items[count].fileSize = ((unsigned long long)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
        count++;

    } while (FindNextFileW(h, &fd));
    FindClose(h);

    if (count == 0) {
        InsertMenuW(hMenu, insertPos, MF_BYPOSITION | MF_STRING | MF_GRAYED, 0, L"(Empty)");
        if (items) LocalFree(items);
        return 1;
    }

    // Sort
    qsort(items, count, sizeof(FileItem), compare_files);

    // Paging
    int max = g_cfg.maxItems;
    if (max <= 0) max = 999999;
    
    int start = offset;
    int end = offset + max;
    if (start > count) start = count;
    if (end > count) end = count;

    int added = 0;

    // Populate
    for (int i = start; i < end; ++i) {
        if (items[i].isDir) {
            WCHAR name[260]; get_name_from_path(items[i].fullPath, name, ARRAYSIZE(name));
            if (!forceLinks && depth < g_cfg.folderMaxDepth) {
                HMENU sub = CreatePopupMenu();
                AppendMenuW(sub, MF_STRING | MF_GRAYED, 0, L"(Loading...)");
                attach_menu_data(sub, items[i].fullPath, depth + 1, 0, FALSE, allowMixedIcons);
                
                MENUITEMINFOW mii = { sizeof(mii) };
                UINT folderId = g_nextFolderId++;
                mii.fMask = MIIM_STRING | MIIM_SUBMENU | MIIM_DATA | MIIM_ID;
                mii.dwTypeData = name;
                mii.hSubMenu = sub;
                mii.wID = folderId;
                mii.dwItemData = (ULONG_PTR)LocalAlloc(LMEM_FIXED, (lstrlenW(items[i].fullPath) + 1) * sizeof(WCHAR));
                if (mii.dwItemData) lstrcpyW((LPWSTR)mii.dwItemData, items[i].fullPath);
                InsertMenuItemW(hMenu, insertPos + added, TRUE, &mii);

                if (g_cfg.showIcons != 0 && g_cfg.showFolderIcons) {
                    HICON hFolder = useLargeRootIcons ? get_system_folder_icon_sized(TRUE) : get_system_folder_icon();
                    if (hFolder) {
                        if (g_cfg.menuStyle == STYLE_LEGACY && (g_cfg.showIcons == 1 || (g_cfg.showIcons == 2 && allowMixedIcons))) {
                            if (useLargeRootIcons && use_legacy_root_large_icon_layout()) add_item_icon(folderId, hFolder);
                            else if (useLargeRootIcons) assign_menu_item_bitmap_sized(hMenu, folderId, hFolder, get_root_icon_size());
                            else assign_legacy_item_bitmap(hMenu, folderId, hFolder);
#ifdef ENABLE_MODERN_STYLE
                        } else if (g_cfg.menuStyle == STYLE_MODERN) {
                            add_item_icon(folderId, hFolder);
#endif
                        }
                    }
                }
            } else {
                MENUITEMINFOW mii = { sizeof(mii) };
                mii.fMask = MIIM_STRING | MIIM_ID | MIIM_DATA;
                mii.dwTypeData = name;
                mii.wID = g_nextFolderId++;
                mii.dwItemData = (ULONG_PTR)LocalAlloc(LMEM_FIXED, (lstrlenW(items[i].fullPath) + 1) * sizeof(WCHAR));
                if (mii.dwItemData) lstrcpyW((LPWSTR)mii.dwItemData, items[i].fullPath);
                InsertMenuItemW(hMenu, insertPos + added, TRUE, &mii);
                map_add(mii.wID, items[i].fullPath);

                if (g_cfg.showIcons != 0 && g_cfg.showFolderIcons) {
                    HICON hFolder = useLargeRootIcons ? get_system_folder_icon_sized(TRUE) : get_system_folder_icon();
                    if (hFolder) {
                        if (g_cfg.menuStyle == STYLE_LEGACY && (g_cfg.showIcons == 1 || (g_cfg.showIcons == 2 && allowMixedIcons))) {
                            if (useLargeRootIcons && use_legacy_root_large_icon_layout()) add_item_icon(mii.wID, hFolder);
                            else if (useLargeRootIcons) assign_menu_item_bitmap_sized(hMenu, mii.wID, hFolder, get_root_icon_size());
                            else assign_legacy_item_bitmap(hMenu, mii.wID, hFolder);
#ifdef ENABLE_MODERN_STYLE
                        } else if (g_cfg.menuStyle == STYLE_MODERN) {
                            add_item_icon(mii.wID, hFolder);
#endif
                        }
                    }
                }
            }
        } else {
            WCHAR name[260]; get_name_from_path(items[i].fullPath, name, ARRAYSIZE(name));
            MENUITEMINFOW mii = { sizeof(mii) };
            mii.fMask = MIIM_STRING | MIIM_ID | MIIM_DATA;
            mii.dwTypeData = name;
            mii.wID = g_nextFolderId++;
            mii.dwItemData = (ULONG_PTR)LocalAlloc(LMEM_FIXED, (lstrlenW(items[i].fullPath) + 1) * sizeof(WCHAR));
            if (mii.dwItemData) lstrcpyW((LPWSTR)mii.dwItemData, items[i].fullPath);
            InsertMenuItemW(hMenu, insertPos + added, TRUE, &mii);
            map_add(mii.wID, items[i].fullPath);

            if (g_cfg.showFileIcons && g_cfg.showIcons != 0) {
                HICON hFile = get_file_icon_sized(items[i].fullPath, useLargeRootIcons);
                if (hFile) {
                    if (g_cfg.menuStyle == STYLE_LEGACY && (g_cfg.showIcons == 1 || (g_cfg.showIcons == 2 && allowMixedIcons))) {
                        if (useLargeRootIcons && use_legacy_root_large_icon_layout()) add_item_icon(mii.wID, hFile);
                        else if (useLargeRootIcons) assign_menu_item_bitmap_sized(hMenu, mii.wID, hFile, get_root_icon_size());
                        else assign_legacy_item_bitmap(hMenu, mii.wID, hFile);
#ifdef ENABLE_MODERN_STYLE
                    } else if (g_cfg.menuStyle == STYLE_MODERN) {
                        add_item_icon(mii.wID, hFile);
#endif
                    }
                }
            }
        }
        added++;
    }

    if (end < count) {
        // Show More Items
        HMENU sub = CreatePopupMenu();
        AppendMenuW(sub, MF_STRING | MF_GRAYED, 0, L"(Loading...)");
        attach_menu_data(sub, path, depth, end, forceLinks, allowMixedIcons);
        
        InsertMenuW(hMenu, insertPos + added, MF_BYPOSITION | MF_SEPARATOR, 0, NULL);
        added++;

        MENUITEMINFOW mii = { sizeof(mii) };
        mii.fMask = MIIM_STRING | MIIM_SUBMENU;
        mii.dwTypeData = L"Show more items...";
        mii.hSubMenu = sub;
        InsertMenuItemW(hMenu, insertPos + added, TRUE, &mii);
        added++;
    }

    if (items) LocalFree(items);
    return added;
}

void MenuPerformScroll(HMENU hMenu, HWND hMenuWnd, const WCHAR* path, BOOL up) {
    // Removed
}

// Populate a folder submenu lazily (filters only, no sorting)
static void populate_folder_menu(HMENU parent, const FolderMenuData* data) {
    if (!data) return;
    int initialCount = GetMenuItemCount(parent);
    if (initialCount > 0) {
        WCHAR txt[32];
        GetMenuStringW(parent, 0, txt, ARRAYSIZE(txt), MF_BYPOSITION);
        if (lstrcmpW(txt, L"(Loading...)") != 0 && lstrcmpW(txt, L"(Empty)") != 0) {
            return; // already populated
        }
        while (GetMenuItemCount(parent) > 0) DeleteMenu(parent, 0, MF_BYPOSITION);
    }

    fill_menu_with_folder(parent, GetMenuItemCount(parent), data->path, data->depth, data->offset, data->forceLinks, data->allowMixedIcons, FALSE);
}

typedef struct TaskKillData {
    HMENU hMenu;
    int count;
    int max;
    UINT* pId;
    BOOL ignoreSystem;
    BOOL showIcons;
    WCHAR** excludes;
    int excludeCount;
    BOOL listWindows;
    BOOL allDesktops;
    WCHAR addedNames[64][64];
    int addedNamesCount;
} TaskKillData;

static HICON load_icon_path_or_module(const WCHAR* spec);

static BOOL is_user_visible_window(HWND hwnd, BOOL allowCloakedShell) {
    if (!IsWindowVisible(hwnd)) return FALSE;

    LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    if ((exStyle & WS_EX_TOOLWINDOW) && !(exStyle & WS_EX_APPWINDOW)) return FALSE;

    HWND owner = GetWindow(hwnd, GW_OWNER);
    if (owner && IsWindowVisible(owner) && !(exStyle & WS_EX_APPWINDOW)) return FALSE;

    DWORD cloaked = 0;
    if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked) {
        if (allowCloakedShell && cloaked == DWM_CLOAKED_SHELL) {
            // Allow windows cloaked by shell (virtual desktops)
        } else {
            return FALSE;
        }
    }

    RECT rc;
    if (GetWindowRect(hwnd, &rc)) {
        if ((rc.right - rc.left) <= 1 || (rc.bottom - rc.top) <= 1) return FALSE;
    }

    return TRUE;
}

static BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam) {
    TaskKillData* data = (TaskKillData*)lParam;
    if (data->count >= data->max) return FALSE;

    if (!is_user_visible_window(hwnd, data->allDesktops)) return TRUE;
    
    WCHAR cls[64];
    BOOL isExplorer = FALSE;
    if (GetClassNameW(hwnd, cls, ARRAYSIZE(cls))) {
        if (lstrcmpiW(cls, L"Shell_TrayWnd") == 0 || lstrcmpiW(cls, L"Progman") == 0) return TRUE;
        if (lstrcmpiW(cls, L"CabinetWClass") == 0) isExplorer = TRUE;
    }

    WCHAR title[256];
    if (GetWindowTextW(hwnd, title, ARRAYSIZE(title)) > 0) {
        if (lstrcmpW(title, L"Program Manager") == 0) return TRUE;
        
        if (data->ignoreSystem) {
            if (lstrcmpiW(title, L"Start") == 0) return TRUE;
            if (lstrcmpiW(title, L"Windows Input Experience") == 0) return TRUE;
            if (lstrcmpiW(title, L"Search") == 0) return TRUE;
            if (lstrcmpiW(title, L"Cortana") == 0) return TRUE;
            // Filter UWP/System classes if needed, but title check covers user request
            if (lstrcmpiW(cls, L"Windows.UI.Core.CoreWindow") == 0) {
                if (lstrcmpiW(title, L"Start") == 0 || lstrcmpiW(title, L"Search") == 0 || lstrcmpiW(title, L"Windows Input Experience") == 0) {
                    return TRUE;
                }
            }
        }

        if (data->excludeCount > 0 && data->excludes) {
            for (int i = 0; i < data->excludeCount; i++) {
                if (lstrcmpiW(title, data->excludes[i]) == 0) return TRUE;
            }
        }

        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        
        WCHAR label[256];
        lstrcpynW(label, title, ARRAYSIZE(label));

        if (!data->listWindows) {
            HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
            if (hProcess) {
                WCHAR path[MAX_PATH];
                DWORD size = ARRAYSIZE(path);
                if (QueryFullProcessImageNameW(hProcess, 0, path, &size)) {
                    WCHAR* name = PathFindFileNameW(path);
                    
                    // Special handling for ApplicationFrameHost.exe (UWP apps container)
                    // These apps must be treated individually by window title, not grouped by exe
                    BOOL isFrameHost = (lstrcmpiW(name, L"ApplicationFrameHost.exe") == 0);
                    
                    if (!isFrameHost) {
                        // Check duplicates by executable name (group all windows of same app)
                        for (int i = 0; i < data->addedNamesCount; i++) {
                            if (lstrcmpiW(data->addedNames[i], name) == 0) {
                                CloseHandle(hProcess);
                                return TRUE; // Skip duplicate app
                            }
                        }
                    }
                    
                    // For ApplicationFrameHost (UWP apps), always use window title - skip FileDescription
                    if (isFrameHost) {
                        lstrcpynW(label, title, ARRAYSIZE(label));
                    } else {
                        // Try to get File Description
                        DWORD handle;
                        DWORD verSize = GetFileVersionInfoSizeW(path, &handle);
                        BOOL gotDescription = FALSE;
                        if (verSize > 0) {
                            void* verData = malloc(verSize);
                            if (verData) {
                                if (GetFileVersionInfoW(path, handle, verSize, verData)) {
                                    struct LANGANDCODEPAGE {
                                        WORD wLanguage;
                                        WORD wCodePage;
                                    } *lpTranslate;
                                    UINT cbTranslate;

                                    // Read the list of languages and code pages.
                                    if (VerQueryValueW(verData, L"\\VarFileInfo\\Translation", (LPVOID*)&lpTranslate, &cbTranslate)) {
                                        // Read the file description for each language and code page.
                                        for( unsigned int i=0; i < (cbTranslate/sizeof(struct LANGANDCODEPAGE)); i++ ) {
                                            WCHAR subBlock[50];
                                            wsprintfW(subBlock, L"\\StringFileInfo\\%04x%04x\\FileDescription", lpTranslate[i].wLanguage, lpTranslate[i].wCodePage);
                                            
                                            WCHAR* description = NULL;
                                            UINT descLen = 0;
                                            if (VerQueryValueW(verData, subBlock, (LPVOID*)&description, &descLen) && descLen > 0) {
                                                lstrcpynW(label, description, ARRAYSIZE(label));
                                                gotDescription = TRUE;
                                                break;
                                            }
                                        }
                                    }
                                }
                                free(verData);
                            }
                        }

                        if (!gotDescription) {
                            // Hardcoded exceptions for specific apps
                            if (lstrcmpiW(name, L"mspaint.exe") == 0) {
                                lstrcpynW(label, L"Paint", ARRAYSIZE(label));
                            } else if (lstrcmpiW(name, L"SnippingTool.exe") == 0) {
                                lstrcpynW(label, L"Snipping Tool", ARRAYSIZE(label));
                            } else if (lstrcmpiW(name, L"PowerToys.Settings.exe") == 0) {
                                lstrcpynW(label, L"PowerToys", ARRAYSIZE(label));
                            } else {
                                // Fallback: use executable name without .exe extension
                                lstrcpynW(label, name, ARRAYSIZE(label));
                                // Remove .exe extension for cleaner display
                                WCHAR* ext = wcsrchr(label, L'.');
                                if (ext && lstrcmpiW(ext, L".exe") == 0) {
                                    *ext = 0;
                                }
                            }
                        }
                    }
                    
                    if (lstrcmpiW(name, L"explorer.exe") == 0) isExplorer = TRUE;

                    // Store executable name for deduplication (not display label)
                    // Skip deduplication storage for ApplicationFrameHost (each UWP app is unique)
                    if (!isFrameHost && data->addedNamesCount < 64) {
                        lstrcpynW(data->addedNames[data->addedNamesCount++], name, 64);
                    }
                }
                CloseHandle(hProcess);
            }
        }

        WCHAR cmd[MAX_PATH + 16];
        if (data->listWindows) {
            // Windows mode: kill specific PID
            wsprintfW(cmd, L"TASKKILL:%u", pid);
        } else {
            // Apps mode: kill all processes with this executable name
            // Exception: ApplicationFrameHost (UWP container) - kill by PID only
            HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
            if (hProc) {
                WCHAR exePath[MAX_PATH];
                DWORD sz = ARRAYSIZE(exePath);
                if (QueryFullProcessImageNameW(hProc, 0, exePath, &sz)) {
                    WCHAR* exeName = PathFindFileNameW(exePath);
                    if (lstrcmpiW(exeName, L"ApplicationFrameHost.exe") == 0) {
                        // UWP apps: kill only this specific instance
                        wsprintfW(cmd, L"TASKKILL:%u", pid);
                    } else {
                        // Regular apps: kill all instances with same exe name
                        wsprintfW(cmd, L"TASKKILL:EXE:%s", exeName);
                    }
                } else {
                    wsprintfW(cmd, L"TASKKILL:%u", pid);
                }
                CloseHandle(hProc);
            } else {
                wsprintfW(cmd, L"TASKKILL:%u", pid);
            }
        }
        
        AppendMenuW(data->hMenu, MF_STRING, *data->pId, label);
        
        if (data->showIcons) {
            HICON hIcon = NULL;
            if (isExplorer && !data->listWindows) {
                hIcon = load_icon_path_or_module(L"imageres.dll,-5325");
            }

            if (!hIcon) {
                // For UWP apps (ApplicationFrameWindow), try multiple child window approaches
                WCHAR className[64];
                if (GetClassNameW(hwnd, className, ARRAYSIZE(className))) {
                    if (lstrcmpiW(className, L"ApplicationFrameWindow") == 0) {
                        // Try CoreWindow first
                        HWND hChild = FindWindowExW(hwnd, NULL, L"Windows.UI.Core.CoreWindow", NULL);
                        if (hChild) {
                            hIcon = (HICON)SendMessageW(hChild, WM_GETICON, ICON_SMALL, 0);
                            if (!hIcon) hIcon = (HICON)GetClassLongPtrW(hChild, GCLP_HICONSM);
                            if (!hIcon) hIcon = (HICON)SendMessageW(hChild, WM_GETICON, ICON_BIG, 0);
                            if (!hIcon) hIcon = (HICON)GetClassLongPtrW(hChild, GCLP_HICON);
                        }
                        
                        // Try other child windows if CoreWindow didn't work
                        if (!hIcon) {
                            hChild = NULL;
                            while ((hChild = FindWindowExW(hwnd, hChild, NULL, NULL)) != NULL) {
                                hIcon = (HICON)SendMessageW(hChild, WM_GETICON, ICON_SMALL, 0);
                                if (hIcon) break;
                                hIcon = (HICON)GetClassLongPtrW(hChild, GCLP_HICONSM);
                                if (hIcon) break;
                            }
                        }
                        
                        // Fallback: extract from imageres.dll for generic app icon
                        if (!hIcon) {
                            hIcon = load_icon_path_or_module(L"imageres.dll,-102");
                        }
                    }
                }
            }

            if (!hIcon) hIcon = (HICON)SendMessageW(hwnd, WM_GETICON, ICON_SMALL, 0);
            if (!hIcon) hIcon = (HICON)GetClassLongPtrW(hwnd, GCLP_HICONSM);
            if (!hIcon) hIcon = (HICON)SendMessageW(hwnd, WM_GETICON, ICON_BIG, 0);
            if (!hIcon) hIcon = (HICON)GetClassLongPtrW(hwnd, GCLP_HICON);

            if (hIcon) {
                HICON hCopy = CopyIcon(hIcon);
                if (hCopy) {
                    add_item_icon(*data->pId, hCopy);
                    if (g_cfg.menuStyle == STYLE_LEGACY && g_cfg.showIcons) {
                        assign_legacy_item_bitmap(data->hMenu, *data->pId, hCopy);
                    }
                }
            }
        }
        
        map_add(*data->pId, cmd);
        
        (*data->pId)++;
        data->count++;
    }
    return TRUE;
}

static HMENU build_taskkill_submenu(int maxItems, BOOL ignoreSystem, BOOL showIcons, WCHAR** excludes, int excludeCount, BOOL listWindows, BOOL allDesktops) {
    HMENU sub = CreatePopupMenu();
    UINT id = IDM_TASKKILL_BASE;
    TaskKillData data = { sub, 0, maxItems, &id, ignoreSystem, showIcons, excludes, excludeCount, listWindows, allDesktops, {{0}}, 0 };
    EnumWindows(EnumWindowsProc, (LPARAM)&data);
    if (data.count == 0) {
        AppendMenuW(sub, MF_STRING | MF_GRAYED, 0, L"(None)");
    }
    return sub;
}

// GUID for shell:UsersFilesFolder {59031a47-3f72-44a7-89c5-5595fe6b30ee}
static const GUID CLSID_UsersFilesFolder = { 0x59031a47, 0x3f72, 0x44a7, { 0x89, 0xc5, 0x55, 0x95, 0xfe, 0x6b, 0x30, 0xee } };

static int fill_menu_with_home(HMENU hMenu, int insertPos, BOOL isSubmenu) {
    CoInitialize(NULL);

    IShellFolder* pDesktop = NULL;
    if (FAILED(SHGetDesktopFolder(&pDesktop))) {
        CoUninitialize();
        return 0;
    }

    LPITEMIDLIST pidlUsersFiles = NULL;
    // Parse ::{GUID}
    if (FAILED(pDesktop->lpVtbl->ParseDisplayName(pDesktop, NULL, NULL, L"::{59031a47-3f72-44a7-89c5-5595fe6b30ee}", NULL, &pidlUsersFiles, NULL))) {
        pDesktop->lpVtbl->Release(pDesktop);
        CoUninitialize();
        return 0;
    }

    IShellFolder* pUsersFiles = NULL;
    if (FAILED(pDesktop->lpVtbl->BindToObject(pDesktop, pidlUsersFiles, NULL, &IID_IShellFolder, (void**)&pUsersFiles))) {
        CoTaskMemFree(pidlUsersFiles);
        pDesktop->lpVtbl->Release(pDesktop);
        CoUninitialize();
        return 0;
    }

    DWORD flags = SHCONTF_FOLDERS | SHCONTF_NONFOLDERS;
    if (g_cfg.showHidden) flags |= SHCONTF_INCLUDEHIDDEN;

    IEnumIDList* pEnum = NULL;
    if (FAILED(pUsersFiles->lpVtbl->EnumObjects(pUsersFiles, NULL, flags, &pEnum))) {
        pUsersFiles->lpVtbl->Release(pUsersFiles);
        CoTaskMemFree(pidlUsersFiles);
        pDesktop->lpVtbl->Release(pDesktop);
        CoUninitialize();
        return 0;
    }

    LPITEMIDLIST pidlItem = NULL;
    ULONG fetched = 0;
    int added = 0;

    while (pEnum->lpVtbl->Next(pEnum, 1, &pidlItem, &fetched) == S_OK && fetched == 1) {
        STRRET str;
        WCHAR name[MAX_PATH];
        
        // Display Name
        if (SUCCEEDED(pUsersFiles->lpVtbl->GetDisplayNameOf(pUsersFiles, pidlItem, SHGDN_NORMAL, &str))) {
            StrRetToBufW(&str, pidlItem, name, ARRAYSIZE(name));
        } else {
            name[0] = 0;
        }

        // Parsing Path
        WCHAR path[MAX_PATH];
        path[0] = 0;
        if (SUCCEEDED(pUsersFiles->lpVtbl->GetDisplayNameOf(pUsersFiles, pidlItem, SHGDN_FORPARSING, &str))) {
            StrRetToBufW(&str, pidlItem, path, ARRAYSIZE(path));
        }

        ULONG attribs = SFGAO_FOLDER;
        pUsersFiles->lpVtbl->GetAttributesOf(pUsersFiles, 1, (LPCITEMIDLIST*)&pidlItem, &attribs);
        BOOL isFolder = (attribs & SFGAO_FOLDER);

        if (name[0]) {
            // Determine if we should show as submenu
            BOOL asSubmenu = (isFolder && g_cfg.homeItemsAsSubmenus && path[0] && PathFileExistsW(path));

            if (asSubmenu) {
                HMENU sub = CreatePopupMenu();
                AppendMenuW(sub, MF_STRING | MF_GRAYED, 0, L"(Loading...)");
                attach_menu_data(sub, path, 1, 0, FALSE, TRUE);
                
                MENUITEMINFOW mii = { sizeof(mii) };
                mii.fMask = MIIM_STRING | MIIM_SUBMENU | MIIM_DATA | MIIM_ID;
                mii.dwTypeData = name;
                mii.hSubMenu = sub;
                mii.wID = g_nextFolderId++;
                mii.dwItemData = (ULONG_PTR)LocalAlloc(LMEM_FIXED, (lstrlenW(path) + 1) * sizeof(WCHAR));
                if (mii.dwItemData) lstrcpyW((LPWSTR)mii.dwItemData, path);
                InsertMenuItemW(hMenu, insertPos + added, TRUE, &mii);
            } else {
                MENUITEMINFOW mii = { sizeof(mii) };
                mii.fMask = MIIM_STRING | MIIM_ID | MIIM_DATA;
                mii.dwTypeData = name;
                mii.wID = g_nextFolderId++;
                if (path[0]) {
                    mii.dwItemData = (ULONG_PTR)LocalAlloc(LMEM_FIXED, (lstrlenW(path) + 1) * sizeof(WCHAR));
                    if (mii.dwItemData) lstrcpyW((LPWSTR)mii.dwItemData, path);
                    map_add(mii.wID, path);
                }
                InsertMenuItemW(hMenu, insertPos + added, TRUE, &mii);
            }

            // Icon
            // Determine whether icons are allowed for this item:
            // - If we're populating a submenu (`isSubmenu`==TRUE), allow icons when HomeShowIcons is enabled and ShowIcons != 0.
            // - If we're populating the root menu, only allow icons when HomeShowIcons is enabled and ShowIcons == 1 (legacy).
            BOOL allowIcons = FALSE;
            if (g_cfg.homeShowIcons) {
                if (isSubmenu) {
                    if (g_cfg.showIcons != 0) allowIcons = TRUE;
                } else {
                    if (g_cfg.showIcons == 1) allowIcons = TRUE;
                }
            }
            if (allowIcons) {
                HICON hIcon = NULL;
                LPITEMIDLIST pidlAbs = ILCombine(pidlUsersFiles, pidlItem);
                SHFILEINFOW sfi = {0};
                UINT flags = SHGFI_PIDL | SHGFI_ICON | (isSubmenu ? SHGFI_SMALLICON : SHGFI_LARGEICON);
                if (SHGetFileInfoW((LPCWSTR)pidlAbs, 0, &sfi, sizeof(sfi), flags)) {
                    hIcon = sfi.hIcon;
                }
                CoTaskMemFree(pidlAbs);

                if (hIcon) {
                    // Register icon for both cases (normal and submenu)
                    add_item_icon(g_nextFolderId - 1, hIcon);

                    if (g_cfg.menuStyle == STYLE_LEGACY) {
                        if (!isSubmenu && use_legacy_root_large_icon_layout()) {
                            // Owner-drawn root menu reads icon from g_itemIcons.
                        } else if (asSubmenu) {
                            int pos = insertPos + added;
                            assign_menu_pos_bitmap_sized(hMenu, pos, hIcon, isSubmenu ? get_small_icon_size() : get_root_icon_size());
                        } else {
                            if (isSubmenu) assign_legacy_item_bitmap(hMenu, g_nextFolderId - 1, hIcon);
                            else assign_menu_item_bitmap_sized(hMenu, g_nextFolderId - 1, hIcon, get_root_icon_size());
                        }
                    }
                }
            }
            added++;
        }
        CoTaskMemFree(pidlItem);
    }

    pEnum->lpVtbl->Release(pEnum);
    pUsersFiles->lpVtbl->Release(pUsersFiles);
    CoTaskMemFree(pidlUsersFiles);
    pDesktop->lpVtbl->Release(pDesktop);
    CoUninitialize();
    return added;
}

static int fill_menu_with_thispc(HMENU hMenu, int insertPos, BOOL isSubmenu) {
    WCHAR drives[512];
    if (GetLogicalDriveStringsW(ARRAYSIZE(drives), drives) == 0) {
        InsertMenuW(hMenu, insertPos, MF_BYPOSITION | MF_STRING | MF_GRAYED, 0, L"(No drives found)");
        return 1;
    }

    int added = 0;
    WCHAR* p = drives;
    while (*p) {
        WCHAR label[MAX_PATH + 32];
        WCHAR volName[MAX_PATH] = {0};
        
        GetVolumeInformationW(p, volName, ARRAYSIZE(volName), NULL, NULL, NULL, NULL, 0);
        
        if (volName[0]) {
            size_t len = lstrlenW(p);
            if (len > 0 && p[len-1] == L'\\') p[len-1] = 0;
            wsprintfW(label, L"%s (%s)", volName, p);
            if (len > 0) p[len-1] = L'\\';
        } else {
            UINT type = GetDriveTypeW(p);
            const WCHAR* typeStr = L"Local Disk";
            if (type == DRIVE_REMOVABLE) typeStr = L"Removable Disk";
            else if (type == DRIVE_CDROM) typeStr = L"CD Drive";
            else if (type == DRIVE_REMOTE) typeStr = L"Network Drive";
            
            size_t len = lstrlenW(p);
            if (len > 0 && p[len-1] == L'\\') p[len-1] = 0;
            wsprintfW(label, L"%s (%s)", typeStr, p);
            if (len > 0) p[len-1] = L'\\';
        }

        MENUITEMINFOW mii = { sizeof(mii) };
        
        if (g_cfg.thisPCItemsAsSubmenus) {
            HMENU sub = CreatePopupMenu();
            AppendMenuW(sub, MF_STRING | MF_GRAYED, 0, L"(Loading...)");
            attach_menu_data(sub, p, 1, 0, FALSE, TRUE);
            
            mii.fMask = MIIM_STRING | MIIM_SUBMENU | MIIM_DATA | MIIM_ID;
            mii.dwTypeData = label;
            mii.hSubMenu = sub;
            mii.wID = g_nextFolderId++;
            mii.dwItemData = (ULONG_PTR)LocalAlloc(LMEM_FIXED, (lstrlenW(p) + 1) * sizeof(WCHAR));
            if (mii.dwItemData) lstrcpyW((LPWSTR)mii.dwItemData, p);
            InsertMenuItemW(hMenu, insertPos + added, TRUE, &mii);
        } else {
            mii.fMask = MIIM_STRING | MIIM_ID | MIIM_DATA;
            mii.dwTypeData = label;
            mii.wID = g_nextFolderId++;
            mii.dwItemData = (ULONG_PTR)LocalAlloc(LMEM_FIXED, (lstrlenW(p) + 1) * sizeof(WCHAR));
            if (mii.dwItemData) lstrcpyW((LPWSTR)mii.dwItemData, p);
            InsertMenuItemW(hMenu, insertPos + added, TRUE, &mii);
            map_add(mii.wID, p);
        }
        
        // Determine whether icons are allowed for this drive item:
        // - If populating a submenu (`isSubmenu`), allow icons when ThisPCShowIcons is enabled and ShowIcons != 0.
        // - If populating the root menu, only allow icons when ThisPCShowIcons is enabled and ShowIcons == 1 (legacy).
        BOOL allowIcons = FALSE;
        if (g_cfg.thisPCShowIcons) {
            if (isSubmenu) {
                if (g_cfg.showIcons != 0) allowIcons = TRUE;
            } else {
                if (g_cfg.showIcons == 1) allowIcons = TRUE;
            }
        }
        if (allowIcons) {
            HICON hIcon = NULL;
            SHFILEINFOW sfi = {0};
            UINT flags = SHGFI_ICON | (isSubmenu ? SHGFI_SMALLICON : SHGFI_LARGEICON);
            if (SHGetFileInfoW(p, 0, &sfi, sizeof(sfi), flags)) {
                hIcon = sfi.hIcon;
            }
            if (hIcon) {
                // Register icon for both cases (normal and submenu)
                // Note: For submenu, we just assigned an ID (g_nextFolderId - 1)
                add_item_icon(g_nextFolderId - 1, hIcon);

                if (g_cfg.menuStyle == STYLE_LEGACY) {
                    if (!isSubmenu && use_legacy_root_large_icon_layout()) {
                        // Owner-drawn root menu reads icon from g_itemIcons.
                    } else if (g_cfg.thisPCItemsAsSubmenus) {
                        int pos = insertPos + added;
                        assign_menu_pos_bitmap_sized(hMenu, pos, hIcon, isSubmenu ? get_small_icon_size() : get_root_icon_size());
                    } else {
                        if (isSubmenu) assign_legacy_item_bitmap(hMenu, g_nextFolderId - 1, hIcon);
                        else assign_menu_item_bitmap_sized(hMenu, g_nextFolderId - 1, hIcon, get_root_icon_size());
                    }
                }
            }
        } 


        added++;
        p += lstrlenW(p) + 1;
    }
    return added;
}

static HMENU build_menu(void) {
    config_load(&g_cfg);
    HMENU hMenu = CreatePopupMenu();
    g_mapCount = 0; // reset mapping for this menu build
    g_itemIconCount = 0; // reset icons
    g_nextFolderId = IDM_FOLDER_BASE;
    UINT id = IDM_DYNAMIC_BASE;
    for (int i = 0; i < g_cfg.count; ++i) {
        ConfigItem* it = &g_cfg.items[i];
        switch (it->type) {
        case CI_SEPARATOR:
            AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
            break;
        case CI_CATEGORY:
            AppendMenuW(hMenu, MF_STRING | MF_GRAYED, 0, it->label[0] ? it->label : it->path);
            break;
        case CI_URI:
        case CI_FILE:
        case CI_CMD:
        {
            AppendMenuW(hMenu, MF_STRING, id, it->label[0] ? it->label : it->path);
            // Pick icon path with theme awareness: per-item (Light/Dark) -> generic -> default (Light/Dark) -> default generic
            const BOOL dark = theme_is_dark();
            const WCHAR* ipath = NULL;
            if (dark && it->iconPathDark[0]) ipath = it->iconPathDark;
            else if (!dark && it->iconPathLight[0]) ipath = it->iconPathLight;
            else if (it->iconPath[0]) ipath = it->iconPath;
            else if (dark && g_cfg.defaultIconPathDark[0]) ipath = g_cfg.defaultIconPathDark;
            else if (!dark && g_cfg.defaultIconPathLight[0]) ipath = g_cfg.defaultIconPathLight;
            else if (g_cfg.defaultIconPath[0]) ipath = g_cfg.defaultIconPath;
                if (ipath) {
                    HICON hico = g_cfg.rootMenuLargeIcons ? load_root_icon_path_or_module(ipath) : load_icon_path_or_module(ipath);
                    // Root-level icons: only register item icons for legacy-visible mode (ShowIcons==1).
                    if (g_cfg.showIcons == 1) add_item_icon(id, hico);
                    if (g_cfg.menuStyle == STYLE_LEGACY && g_cfg.showIcons == 1 && !use_legacy_root_large_icon_layout()) assign_legacy_item_bitmap(hMenu, id, hico);
                }
            id++;
            break;
        }
            break;
        case CI_FOLDER:
        {
            if (it->submenu) {
                HMENU sub = CreatePopupMenu();
                AppendMenuW(sub, MF_STRING | MF_GRAYED, 0, L"(Loading...)");
                attach_menu_data(sub, it->path, 1, 0, FALSE, TRUE);
                AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)sub, it->label[0] ? it->label : it->path);
                UINT popupId = g_nextFolderId++;
                MENUITEMINFOW mii = { sizeof(mii) };
                mii.fMask = MIIM_DATA | MIIM_SUBMENU | MIIM_ID;
                mii.wID = popupId;
                mii.dwItemData = (ULONG_PTR)LocalAlloc(LMEM_FIXED, (lstrlenW(it->path) + 1) * sizeof(WCHAR));
                if (mii.dwItemData) lstrcpyW((LPWSTR)mii.dwItemData, it->path);
                mii.hSubMenu = sub;
                int pos = GetMenuItemCount(hMenu) - 1;
                SetMenuItemInfoW(hMenu, pos, TRUE, &mii);

                if (g_cfg.showIcons == 1) {
                    const BOOL dark = theme_is_dark();
                    const WCHAR* ipath = NULL;
                    HICON hicoF = NULL;
                    if (dark && it->iconPathDark[0]) ipath = it->iconPathDark;
                    else if (!dark && it->iconPathLight[0]) ipath = it->iconPathLight;
                    else if (it->iconPath[0]) ipath = it->iconPath;
                    if (!ipath) {
                        if (g_cfg.showFolderIcons) hicoF = g_cfg.rootMenuLargeIcons ? get_system_folder_icon_sized(TRUE) : get_system_folder_icon();
                        else if (dark && g_cfg.defaultIconPathDark[0]) ipath = g_cfg.defaultIconPathDark;
                        else if (!dark && g_cfg.defaultIconPathLight[0]) ipath = g_cfg.defaultIconPathLight;
                        else if (g_cfg.defaultIconPath[0]) ipath = g_cfg.defaultIconPath;
                    }
                    if (!hicoF && ipath) hicoF = g_cfg.rootMenuLargeIcons ? load_root_icon_path_or_module(ipath) : load_icon_path_or_module(ipath);
                    if (hicoF) {
                        add_item_icon(popupId, hicoF);
                        if (!use_legacy_root_large_icon_layout()) assign_icon_to_last_popup(hMenu, hicoF);
                    }
                }
            } else if (it->inlineExpand) {
                // Inline expand: inject folder entries directly at root at this position
                // Optional header (may be suppressed by future flag)
                if (it->label[0] && !it->inlineNoHeader) {
                    if (it->inlineOpen) {
                        // Clickable header that opens the folder
                        AppendMenuW(hMenu, MF_STRING, g_nextFolderId, it->label);
                        map_add(g_nextFolderId++, it->path);
                    } else {
                        AppendMenuW(hMenu, MF_STRING | MF_GRAYED, 0, it->label);
                    }
                }
                
                fill_menu_with_folder(hMenu, GetMenuItemCount(hMenu), it->path, 1, 0, FALSE, FALSE, TRUE);
                
                // No automatic trailing separator; user controls separators explicitly in config.
            } else {
                AppendMenuW(hMenu, MF_STRING, id, it->label[0] ? it->label : it->path);
                const BOOL dark = theme_is_dark();
                const WCHAR* ipath = NULL;
                HICON hico = NULL;
                // Prefer per-item icon first
                if (dark && it->iconPathDark[0]) ipath = it->iconPathDark;
                else if (!dark && it->iconPathLight[0]) ipath = it->iconPathLight;
                else if (it->iconPath[0]) ipath = it->iconPath;
                if (!ipath) {
                    if (g_cfg.showFolderIcons) {
                        // When showing folder icons and no per-item icon, use system folder icon
                        hico = g_cfg.rootMenuLargeIcons ? get_system_folder_icon_sized(TRUE) : get_system_folder_icon();
                    } else {
                        // Fall back to defaults
                        if (dark && g_cfg.defaultIconPathDark[0]) ipath = g_cfg.defaultIconPathDark;
                        else if (!dark && g_cfg.defaultIconPathLight[0]) ipath = g_cfg.defaultIconPathLight;
                        else if (g_cfg.defaultIconPath[0]) ipath = g_cfg.defaultIconPath;
                    }
                }
                if (!hico && ipath) {
                    hico = g_cfg.rootMenuLargeIcons ? load_root_icon_path_or_module(ipath) : load_icon_path_or_module(ipath);
                }
                if (hico) {
                    // Root-level icons: only register item icons for legacy-visible mode (ShowIcons==1).
                    if (g_cfg.showIcons == 1) add_item_icon(id, hico);
                    if (g_cfg.menuStyle == STYLE_LEGACY && g_cfg.showIcons == 1 && !use_legacy_root_large_icon_layout()) assign_legacy_item_bitmap(hMenu, id, hico);
                }
                id++;
            }
            break;
        }
        case CI_THISPC:
        {
            if (it->submenu) {
                HMENU sub = CreatePopupMenu();
                fill_menu_with_thispc(sub, 0, TRUE);
                AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)sub, it->label[0] ? it->label : L"This PC");
                UINT popupId = g_nextFolderId++;
                MENUITEMINFOW popupMii = { sizeof(popupMii) };
                popupMii.fMask = MIIM_ID;
                popupMii.wID = popupId;
                SetMenuItemInfoW(hMenu, GetMenuItemCount(hMenu) - 1, TRUE, &popupMii);
                
                const BOOL dark = theme_is_dark();
                const WCHAR* ipath = NULL;
                if (dark && it->iconPathDark[0]) ipath = it->iconPathDark;
                else if (!dark && it->iconPathLight[0]) ipath = it->iconPathLight;
                else if (it->iconPath[0]) ipath = it->iconPath;
                else if (dark && g_cfg.defaultIconPathDark[0]) ipath = g_cfg.defaultIconPathDark;
                else if (!dark && g_cfg.defaultIconPathLight[0]) ipath = g_cfg.defaultIconPathLight;
                else if (g_cfg.defaultIconPath[0]) ipath = g_cfg.defaultIconPath;
                if (ipath) {
                    // Only load/assign popup-root icons when icons are enabled (legacy only).
                    // When ShowIcons is not legacy (==1), do not show icons for submenu roots.
                    if (g_cfg.showIcons == 1) {
                        HICON hico = g_cfg.rootMenuLargeIcons ? load_root_icon_path_or_module(ipath) : load_icon_path_or_module(ipath);
                        if (hico) {
                            add_item_icon(popupId, hico);
                            if (!use_legacy_root_large_icon_layout()) assign_icon_to_last_popup(hMenu, hico);
                        }
                    }
                }
            } else {
                if (it->label[0] && !it->inlineNoHeader) {
                    if (it->inlineOpen) {
                        AppendMenuW(hMenu, MF_STRING, g_nextFolderId, it->label);
                        map_add(g_nextFolderId++, L"::{20D04FE0-3AEA-1069-A2D8-08002B30309D}");
                    } else {
                        AppendMenuW(hMenu, MF_STRING | MF_GRAYED, 0, it->label);
                    }
                }
                fill_menu_with_thispc(hMenu, GetMenuItemCount(hMenu), FALSE);
            }
            break;
        }
        case CI_HOME:
        {
            if (it->submenu) {
                HMENU sub = CreatePopupMenu();
                fill_menu_with_home(sub, 0, TRUE);
                AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)sub, it->label[0] ? it->label : L"Home");
                UINT popupId = g_nextFolderId++;
                MENUITEMINFOW popupMii = { sizeof(popupMii) };
                popupMii.fMask = MIIM_ID;
                popupMii.wID = popupId;
                SetMenuItemInfoW(hMenu, GetMenuItemCount(hMenu) - 1, TRUE, &popupMii);
                
                const BOOL dark = theme_is_dark();
                const WCHAR* ipath = NULL;
                if (dark && it->iconPathDark[0]) ipath = it->iconPathDark;
                else if (!dark && it->iconPathLight[0]) ipath = it->iconPathLight;
                else if (it->iconPath[0]) ipath = it->iconPath;
                else if (dark && g_cfg.defaultIconPathDark[0]) ipath = g_cfg.defaultIconPathDark;
                else if (!dark && g_cfg.defaultIconPathLight[0]) ipath = g_cfg.defaultIconPathLight;
                else if (g_cfg.defaultIconPath[0]) ipath = g_cfg.defaultIconPath;
                if (ipath) {
                    // Only load/assign popup-root icons when icons are enabled (legacy only).
                    // When ShowIcons is not legacy (==1), do not show icons for submenu roots.
                    if (g_cfg.showIcons == 1) {
                        HICON hico = g_cfg.rootMenuLargeIcons ? load_root_icon_path_or_module(ipath) : load_icon_path_or_module(ipath);
                        if (hico) {
                            add_item_icon(popupId, hico);
                            if (!use_legacy_root_large_icon_layout()) assign_icon_to_last_popup(hMenu, hico);
                        }
                    }
                }
            } else {
                if (it->label[0] && !it->inlineNoHeader) {
                    if (it->inlineOpen) {
                        AppendMenuW(hMenu, MF_STRING, g_nextFolderId, it->label);
                        map_add(g_nextFolderId++, L"::{59031a47-3f72-44a7-89c5-5595fe6b30ee}");
                    } else {
                        AppendMenuW(hMenu, MF_STRING | MF_GRAYED, 0, it->label);
                    }
                }
                fill_menu_with_home(hMenu, GetMenuItemCount(hMenu), FALSE);
            }
            break;
        }
        case CI_FOLDER_SUBMENU:
        {
            HMENU sub = CreatePopupMenu();
            AppendMenuW(sub, MF_STRING | MF_GRAYED, 0, L"(Loading...)");
            attach_menu_data(sub, it->path, 1, 0, FALSE, TRUE);
            AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)sub, it->label[0] ? it->label : it->path);
            UINT popupId = g_nextFolderId++;
            if (g_cfg.showIcons != 0) {
                const BOOL dark = theme_is_dark();
                HICON hicoF = NULL;
                const WCHAR* ipath = NULL;
                // Prefer per-item icon first
                if (dark && it->iconPathDark[0]) ipath = it->iconPathDark;
                else if (!dark && it->iconPathLight[0]) ipath = it->iconPathLight;
                else if (it->iconPath[0]) ipath = it->iconPath;
                if (!ipath) {
                    if (g_cfg.showFolderIcons) {
                        hicoF = g_cfg.rootMenuLargeIcons ? get_system_folder_icon_sized(TRUE) : get_system_folder_icon();
                    } else {
                        if (dark && g_cfg.defaultIconPathDark[0]) ipath = g_cfg.defaultIconPathDark;
                        else if (!dark && g_cfg.defaultIconPathLight[0]) ipath = g_cfg.defaultIconPathLight;
                        else if (g_cfg.defaultIconPath[0]) ipath = g_cfg.defaultIconPath;
                    }
                }
                if (!hicoF && ipath) hicoF = g_cfg.rootMenuLargeIcons ? load_root_icon_path_or_module(ipath) : load_icon_path_or_module(ipath);
                if (hicoF) {
                    if (g_cfg.showIcons == 1) add_item_icon(popupId, hicoF);
                    if (g_cfg.menuStyle == STYLE_LEGACY && g_cfg.showIcons == 1) {
                        if (!use_legacy_root_large_icon_layout()) assign_icon_to_last_popup(hMenu, hicoF);
#ifdef ENABLE_MODERN_STYLE
                    } else if (g_cfg.menuStyle == STYLE_MODERN) {
                        add_item_icon(popupId, hicoF);
#endif
                    }
                }
            }
            MENUITEMINFOW mii = { sizeof(mii) };
            mii.fMask = MIIM_DATA | MIIM_SUBMENU | MIIM_ID;
            mii.wID = popupId;
            mii.dwItemData = (ULONG_PTR)LocalAlloc(LMEM_FIXED, (lstrlenW(it->path) + 1) * sizeof(WCHAR));
            if (mii.dwItemData) lstrcpyW((LPWSTR)mii.dwItemData, it->path);
            mii.hSubMenu = sub;
            int pos = GetMenuItemCount(hMenu) - 1;
            SetMenuItemInfoW(hMenu, pos, TRUE, &mii);
            break;
        }
        case CI_POWER_SLEEP:
        case CI_POWER_SHUTDOWN:
        case CI_POWER_RESTART:
        case CI_POWER_LOCK:
        case CI_POWER_LOGOFF:
            AppendMenuW(hMenu, MF_STRING, id++, it->label);
            break;
        case CI_POWER_HIBERNATE:
            AppendMenuW(hMenu, MF_STRING, id++, it->label);
            break;
    case CI_RECENT_SUBMENU:
        {
            HMENU sub = build_recent_submenu();
            AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)sub, it->label[0] ? it->label : L"Recent Items");
            UINT popupId = g_nextFolderId++;
            MENUITEMINFOW popupMii = { sizeof(popupMii) };
            popupMii.fMask = MIIM_ID;
            popupMii.wID = popupId;
            SetMenuItemInfoW(hMenu, GetMenuItemCount(hMenu) - 1, TRUE, &popupMii);
            if (g_cfg.menuStyle == STYLE_LEGACY && g_cfg.showIcons == 1) {
                const BOOL dark = theme_is_dark();
                HICON hicoR = NULL;
                const WCHAR* ipath = NULL;
                if (dark && it->iconPathDark[0]) ipath = it->iconPathDark;
                else if (!dark && it->iconPathLight[0]) ipath = it->iconPathLight;
                else if (it->iconPath[0]) ipath = it->iconPath;
                else if (dark && g_cfg.defaultIconPathDark[0]) ipath = g_cfg.defaultIconPathDark;
                else if (!dark && g_cfg.defaultIconPathLight[0]) ipath = g_cfg.defaultIconPathLight;
                else if (g_cfg.defaultIconPath[0]) ipath = g_cfg.defaultIconPath;
                if (ipath) hicoR = g_cfg.rootMenuLargeIcons ? load_root_icon_path_or_module(ipath) : load_icon_path_or_module(ipath);
                if (hicoR) {
                    add_item_icon(popupId, hicoR);
                    if (!use_legacy_root_large_icon_layout()) assign_icon_to_last_popup(hMenu, hicoR);
                }
            }
            break;
        }
        case CI_POWER_MENU:
        {
            HMENU sub = CreatePopupMenu();
            // Order: Sleep, Hibernate, Shutdown, Restart, Lock, Log off (group with separator before lock group)
            BOOL firstGroupAdded = FALSE;
            if (!g_cfg.excludeSleep) { AppendMenuW(sub, MF_STRING, id, L"Sleep"); map_add(id++, L"POWER_SLEEP"); firstGroupAdded=TRUE; }
            if (!g_cfg.excludeHibernate) { AppendMenuW(sub, MF_STRING, id, L"Hibernate"); map_add(id++, L"POWER_HIBERNATE"); firstGroupAdded=TRUE; }
            if (!g_cfg.excludeShutdown) { AppendMenuW(sub, MF_STRING, id, L"Shut down"); map_add(id++, L"POWER_SHUTDOWN"); firstGroupAdded=TRUE; }
            if (!g_cfg.excludeRestart) { AppendMenuW(sub, MF_STRING, id, L"Restart"); map_add(id++, L"POWER_RESTART"); firstGroupAdded=TRUE; }
            BOOL secondGroup = FALSE;
            if (!g_cfg.excludeLock || !g_cfg.excludeLogoff) {
                if (firstGroupAdded) AppendMenuW(sub, MF_SEPARATOR, 0, NULL);
                secondGroup = TRUE;
            }
            if (!g_cfg.excludeLock) { AppendMenuW(sub, MF_STRING, id, L"Lock"); map_add(id++, L"POWER_LOCK"); }
            if (!g_cfg.excludeLogoff) { AppendMenuW(sub, MF_STRING, id, L"Log off"); map_add(id++, L"POWER_LOGOFF"); }
            // If everything excluded, show placeholder disabled item
            if (!firstGroupAdded && !secondGroup) {
                AppendMenuW(sub, MF_STRING | MF_GRAYED, 0, L"(None)");
            }
            AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)sub, it->label[0] ? it->label : L"Power");
            UINT popupId = g_nextFolderId++;
            MENUITEMINFOW popupMii = { sizeof(popupMii) };
            popupMii.fMask = MIIM_ID;
            popupMii.wID = popupId;
            SetMenuItemInfoW(hMenu, GetMenuItemCount(hMenu) - 1, TRUE, &popupMii);
            if (g_cfg.menuStyle == STYLE_LEGACY && g_cfg.showIcons == 1) {
                const BOOL dark = theme_is_dark();
                HICON hicoP = NULL;
                const WCHAR* ipath = NULL;
                if (dark && it->iconPathDark[0]) ipath = it->iconPathDark;
                else if (!dark && it->iconPathLight[0]) ipath = it->iconPathLight;
                else if (it->iconPath[0]) ipath = it->iconPath;
                else if (dark && g_cfg.defaultIconPathDark[0]) ipath = g_cfg.defaultIconPathDark;
                else if (!dark && g_cfg.defaultIconPathLight[0]) ipath = g_cfg.defaultIconPathLight;
                else if (g_cfg.defaultIconPath[0]) ipath = g_cfg.defaultIconPath;
                if (ipath) hicoP = g_cfg.rootMenuLargeIcons ? load_root_icon_path_or_module(ipath) : load_icon_path_or_module(ipath);
                if (hicoP) {
                    add_item_icon(popupId, hicoP);
                    if (!use_legacy_root_large_icon_layout()) assign_icon_to_last_popup(hMenu, hicoP);
                }
            }
            break;
        }
        case CI_TASKKILL:
        {
            int max = g_cfg.taskKillMax;
            BOOL ignoreSystem = g_cfg.taskKillIgnoreSystem;
            BOOL showIcons = g_cfg.taskKillShowIcons;
            BOOL listWindows = g_cfg.taskKillListWindows;
            WCHAR* excludes[32];
            int excludeCount = 0;
            
            // Parse global excludes
            WCHAR globalBuf[512];
            lstrcpynW(globalBuf, g_cfg.taskKillExcludes, ARRAYSIZE(globalBuf));
            
            WCHAR* p = globalBuf;
            while (*p && excludeCount < 32) {
                WCHAR* next = wcschr(p, L',');
                if (next) *next = 0;
                if (*p) excludes[excludeCount++] = p;
                if (!next) break;
                p = next + 1;
            }

            // Override with item params if present
            if (it->params[0]) {
                excludeCount = 0; // Reset excludes if params provided
                
                WCHAR buf[256];
                lstrcpynW(buf, it->params, ARRAYSIZE(buf));
                p = buf;

                if (*p) {
                    WCHAR* next = wcschr(p, L',');
                    if (next) *next = 0;
                    max = _wtoi(p);
                    if (next) {
                        p = next + 1;
                        next = wcschr(p, L',');
                        if (next) *next = 0;
                        if (lstrcmpiW(p, L"true") == 0 || lstrcmpiW(p, L"1") == 0) ignoreSystem = TRUE;
                        else if (lstrcmpiW(p, L"false") == 0 || lstrcmpiW(p, L"0") == 0) ignoreSystem = FALSE;
                        
                        if (next) {
                            p = next + 1;
                            
                            // Check for optional ShowIcons boolean
                            WCHAR* comma = wcschr(p, L',');
                            if (comma) *comma = 0;
                            BOOL isBool = (lstrcmpiW(p, L"true") == 0 || lstrcmpiW(p, L"false") == 0 || 
                                           lstrcmpiW(p, L"1") == 0 || lstrcmpiW(p, L"0") == 0);
                            
                            if (isBool) {
                                if (lstrcmpiW(p, L"false") == 0 || lstrcmpiW(p, L"0") == 0) showIcons = FALSE;
                                else showIcons = TRUE;
                                
                                if (comma) {
                                    p = comma + 1;
                                    
                                    // Check for optional ListWindows boolean
                                    comma = wcschr(p, L',');
                                    if (comma) *comma = 0;
                                    BOOL isBool2 = (lstrcmpiW(p, L"true") == 0 || lstrcmpiW(p, L"false") == 0 || 
                                                   lstrcmpiW(p, L"1") == 0 || lstrcmpiW(p, L"0") == 0);
                                    
                                    if (isBool2) {
                                        if (lstrcmpiW(p, L"false") == 0 || lstrcmpiW(p, L"0") == 0) listWindows = FALSE;
                                        else listWindows = TRUE;
                                        
                                        if (comma) {
                                            p = comma + 1;
                                        } else {
                                            p = NULL;
                                        }
                                    } else {
                                        // Not a bool, so it's an exclude
                                        excludes[excludeCount++] = p;
                                        if (comma) p = comma + 1;
                                        else p = NULL;
                                    }
                                } else {
                                    p = NULL;
                                }
                            } else {
                                if (comma) {
                                    excludes[excludeCount++] = p;
                                    p = comma + 1;
                                } else {
                                    excludes[excludeCount++] = p;
                                    p = NULL;
                                }
                            }

                            while (p && *p && excludeCount < 32) {
                                next = wcschr(p, L',');
                                if (next) *next = 0;
                                excludes[excludeCount++] = p;
                                if (!next) break;
                                p = next + 1;
                            }
                        }
                    }
                }
            }
            if (max <= 0) max = 10;
            HMENU sub = build_taskkill_submenu(max, ignoreSystem, showIcons, excludes, excludeCount, listWindows, g_cfg.taskKillAllDesktops);
            AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)sub, it->label[0] ? it->label : L"Task Kill");
            UINT popupId = g_nextFolderId++;
            MENUITEMINFOW popupMii = { sizeof(popupMii) };
            popupMii.fMask = MIIM_ID;
            popupMii.wID = popupId;
            SetMenuItemInfoW(hMenu, GetMenuItemCount(hMenu) - 1, TRUE, &popupMii);
            if (g_cfg.menuStyle == STYLE_LEGACY && g_cfg.showIcons == 1) {
                const BOOL dark = theme_is_dark();
                HICON hico = NULL;
                const WCHAR* ipath = NULL;
                if (dark && it->iconPathDark[0]) ipath = it->iconPathDark;
                else if (!dark && it->iconPathLight[0]) ipath = it->iconPathLight;
                else if (it->iconPath[0]) ipath = it->iconPath;
                if (ipath) hico = g_cfg.rootMenuLargeIcons ? load_root_icon_path_or_module(ipath) : load_icon_path_or_module(ipath);
                if (hico) {
                    add_item_icon(popupId, hico);
                    if (!use_legacy_root_large_icon_layout()) assign_icon_to_last_popup(hMenu, hico);
                }
            }
            break;
        }
        }
    }
    theme_style_menu(hMenu);
    if (use_legacy_root_large_icon_layout()) set_legacy_root_owner_draw(hMenu);
    g_rootMenuHandle = hMenu;
    #ifdef ENABLE_MODERN_STYLE
    if (g_cfg.menuStyle == STYLE_MODERN) {
        extern void Menu_SetOwnerDrawRecursive(HMENU m);
        Menu_SetOwnerDrawRecursive(hMenu);
    }
    #endif
    // No width shim anymore; modern width is controlled in measure/draw, legacy stays native.
    return hMenu;
}

static POINT compute_menu_pos(HWND owner) {
    POINT cursor; GetCursorPos(&cursor);
    HMONITOR mon = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = { sizeof(mi) };
    GetMonitorInfoW(mon, &mi);

    if (g_cfg.count == 0) config_load(&g_cfg);

    RECT wa = mi.rcWork;
    LONG x = 0, y = 0;
    if (g_cfg.pointerRelative) {
        // Anchor relative to pointer using configured offsets (optionally ignored per setting)
        x = cursor.x;
        y = cursor.y;
        if (!g_cfg.ignoreHOffsetWhenRelative) x += g_cfg.hOffset;
        if (!g_cfg.ignoreVOffsetWhenRelative) y += g_cfg.vOffset;
    } else {
    if (g_cfg.hPlacement == 0) {
        x = wa.left + g_cfg.hOffset;
        if (g_cfg.hOffset < 0) x = wa.left - g_cfg.hOffset; // negative flips semantics for edge padding
    } else if (g_cfg.hPlacement == 1) {
        // Center horizontally; optionally ignore offset per setting
        x = wa.left + (wa.right - wa.left) / 2;
        if (!g_cfg.ignoreHOffsetWhenCentered) x += g_cfg.hOffset;
    } else {
        x = wa.right - g_cfg.hOffset;
        if (g_cfg.hOffset < 0) x = wa.right + g_cfg.hOffset; // negative flips semantics for edge padding
    }

    if (g_cfg.vPlacement == 0) {
        y = wa.top + g_cfg.vOffset;
        if (g_cfg.vOffset < 0) y = wa.top - g_cfg.vOffset;
    } else if (g_cfg.vPlacement == 1) {
        // Center vertically; optionally ignore offset per setting
        y = wa.top + (wa.bottom - wa.top) / 2;
        if (!g_cfg.ignoreVOffsetWhenCentered) y += g_cfg.vOffset;
    } else {
        y = wa.bottom - g_cfg.vOffset;
        if (g_cfg.vOffset < 0) y = wa.bottom + g_cfg.vOffset;
    }
    }

    if (x < wa.left) x = wa.left;
    if (x > wa.right) x = wa.right;
    if (y < wa.top) y = wa.top;
    if (y > wa.bottom) y = wa.bottom;
    POINT pt = { x, y };
    return pt;
}

void MenuOnMenuSelect(HWND owner, WPARAM wParam, LPARAM lParam) {
    static UINT lastItem = (UINT)-1;
    static DWORD lastTime = 0;
    UINT item = LOWORD(wParam);
    UINT flags = HIWORD(wParam);
    HMENU hMenu = (HMENU)lParam;
    if (!(flags & MF_POPUP)) { return; }
    DWORD now = GetTickCount();
    if (!g_cfg.folderSingleClickOpen && item == lastItem && (now - lastTime) <= GetDoubleClickTime()) {
        MENUITEMINFOW mii = { sizeof(mii) };
        mii.fMask = MIIM_DATA;
        if (GetMenuItemInfoW(hMenu, item, TRUE, &mii) && mii.dwItemData) {
            open_shell_item((LPCWSTR)mii.dwItemData);
        }
    }
    lastItem = item;
    lastTime = now;
}

void MenuOnInitMenuPopup(HWND owner, HMENU hMenu, UINT item, BOOL isSystemMenu) {
    UNREFERENCED_PARAMETER(owner);
    UNREFERENCED_PARAMETER(item);
    UNREFERENCED_PARAMETER(isSystemMenu);
    MENUINFO mi; ZeroMemory(&mi, sizeof(mi));
    mi.cbSize = sizeof(mi);
    mi.fMask = MIM_MENUDATA;
    if (GetMenuInfo(hMenu, &mi) && mi.dwMenuData != 0) {
        FolderMenuData* data = (FolderMenuData*)mi.dwMenuData;
        if (data) {
            populate_folder_menu(hMenu, data);
        }
    }
}

BOOL MenuOnMenuRButtonUp(HWND owner, UINT itemPos, HMENU hMenu) {
    if (!hMenu) return FALSE;

    MENUITEMINFOW mii;
    ZeroMemory(&mii, sizeof(mii));
    mii.cbSize = sizeof(mii);
    mii.fMask = MIIM_ID | MIIM_DATA;

    if (!GetMenuItemInfoW(hMenu, itemPos, TRUE, &mii)) return FALSE;

    WCHAR path[MAX_PATH];
    path[0] = 0;

    if (mii.dwItemData) {
        lstrcpynW(path, (const WCHAR*)mii.dwItemData, ARRAYSIZE(path));
    }
    if (!path[0]) {
        if (!resolve_filesystem_path_by_command(mii.wID, path, ARRAYSIZE(path))) return FALSE;
    }
    if (!is_filesystem_target(path)) return FALSE;

    POINT pt;
    GetCursorPos(&pt);

    // Keep WinMac menu visible and display shell context menu recursively.
    g_suppressCmd = mii.wID;
    g_suppressCmdTick = GetTickCount();
    return show_shell_context_menu_for_path(owner, path, pt);
}

void MenuExecuteCommand(HWND owner, UINT cmd) {
    if (!cmd) return;

    wchar_t dbg_buf[256];
    wsprintfW(dbg_buf, L"[MenuExecuteCommand] Received cmd: %u\n", cmd);
    OutputDebugStringW(dbg_buf);

    if (g_suppressCmd) {
        BOOL same = (cmd == g_suppressCmd);
        DWORD age = GetTickCount() - g_suppressCmdTick;
        g_suppressCmd = 0;
        if (same && age <= 2000) return;
    }

    BOOL found = FALSE;
    for (UINT i = 0; i < g_mapCount; ++i) {
        if (g_map[i].id == (UINT)cmd) {
            found = TRUE;
            wsprintfW(dbg_buf, L"[MenuExecuteCommand] Found cmd in g_map: %u, path: %s\n", cmd, g_map[i].path);
            OutputDebugStringW(dbg_buf);
            // Interpret special power markers
            if (!lstrcmpiW(g_map[i].path, L"POWER_SLEEP")) { system_sleep(); return; }
            if (!lstrcmpiW(g_map[i].path, L"POWER_SHUTDOWN")) { system_shutdown(FALSE); return; }
            if (!lstrcmpiW(g_map[i].path, L"POWER_RESTART")) { system_shutdown(TRUE); return; }
            if (!lstrcmpiW(g_map[i].path, L"POWER_LOCK")) { LockWorkStation(); return; }
            if (!lstrcmpiW(g_map[i].path, L"POWER_LOGOFF")) { ExitWindowsEx(EWX_LOGOFF, 0); return; }
            if (!lstrcmpiW(g_map[i].path, L"POWER_HIBERNATE")) { system_hibernate(); return; }
            if (wcsncmp(g_map[i].path, L"TASKKILL:", 9) == 0) {
                const WCHAR* param = g_map[i].path + 9;
                if (wcsncmp(param, L"EXE:", 4) == 0) {
                    // Kill all processes with this executable name
                    const WCHAR* exeName = param + 4;
                    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
                    if (hSnapshot != INVALID_HANDLE_VALUE) {
                        PROCESSENTRY32W pe = { sizeof(pe) };
                        if (Process32FirstW(hSnapshot, &pe)) {
                            do {
                                if (lstrcmpiW(pe.szExeFile, exeName) == 0) {
                                    HANDLE hProc = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
                                    if (hProc) {
                                        TerminateProcess(hProc, 1);
                                        CloseHandle(hProc);
                                    }
                                }
                            } while (Process32NextW(hSnapshot, &pe));
                        }
                        CloseHandle(hSnapshot);
                    }
                } else {
                    // Kill specific PID
                    DWORD pid = _wtoi(param);
                    if (pid) {
                        HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
                        if (hProcess) {
                            TerminateProcess(hProcess, 1);
                            CloseHandle(hProcess);
                        }
                    }
                }
                return;
            }
            open_shell_item(g_map[i].path); return; }
    }
    if (!found) {
        wsprintfW(dbg_buf, L"[MenuExecuteCommand] cmd %u NOT FOUND in g_map\n", cmd);
        OutputDebugStringW(dbg_buf);
    }
    if (cmd >= IDM_RECENT_BASE && cmd < IDM_RECENT_BASE + 1000) {
        if (cmd == IDM_RECENT_BASE + 900) {
            recent_clear_all();
            return; // no reopen; user can reopen menu manually
        }
        RecentItem *items = NULL; int n = recent_get_items(&items, g_cfg.recentMax > 0 ? g_cfg.recentMax : 12);
        int idx = cmd - IDM_RECENT_BASE;
        if (idx >= 0 && idx < n) recent_open_item(&items[idx]);
        if (items) LocalFree(items);
        return;
    }
    UINT id = IDM_DYNAMIC_BASE;
    for (int i = 0; i < g_cfg.count; ++i) {
        ConfigItem* it = &g_cfg.items[i];
        switch (it->type) {
        case CI_SEPARATOR:
        case CI_CATEGORY:
            break;
        case CI_URI:
            if (id == cmd) { open_uri(it->path); return; } id++; break;
        case CI_FILE:
            if (id == cmd) { open_shell_known(L"open", it->path, it->params[0]?it->params:NULL); return; } id++; break;
        case CI_CMD:
            if (id == cmd) { open_shell_known(L"open", L"cmd.exe", it->params[0] ? it->params : it->path); return; } id++; break;
        case CI_FOLDER:
            if (!it->submenu) {
                if (it->inlineExpand) {
                    // Inline-expanded folder does NOT create a direct command item, so it must NOT
                    // consume or compare the current dynamic id. Skip without increment.
                } else {
                    if (id == cmd) { open_shell_item(it->path); return; }
                    id++;
                }
            }
            break;
        case CI_FOLDER_SUBMENU:
            break;
        case CI_POWER_SLEEP:
            if (id == cmd) { system_sleep(); return; } id++; break;
        case CI_POWER_SHUTDOWN:
            if (id == cmd) { system_shutdown(FALSE); return; } id++; break;
        case CI_POWER_RESTART:
            if (id == cmd) { system_shutdown(TRUE); return; } id++; break;
        case CI_POWER_LOCK:
            if (id == cmd) { LockWorkStation(); return; } id++; break;
        case CI_POWER_LOGOFF:
            if (id == cmd) { ExitWindowsEx(EWX_LOGOFF, 0); return; } id++; break;
        case CI_POWER_HIBERNATE:
            if (id == cmd) { system_hibernate(); return; } id++; break;
        case CI_RECENT_SUBMENU:
        case CI_POWER_MENU:
        case CI_THISPC:
        case CI_HOME:
        case CI_TASKKILL:
            break;
        }
    }
}

BOOL MenuOpenRecentParentFolder(UINT cmd) {
    // Check if this is a recent item command
    if (cmd < IDM_RECENT_BASE || cmd >= IDM_RECENT_BASE + 1000) {
        return FALSE; // Not a recent item
    }
    
    // Skip the "Clear Recent Items" special command
    if (cmd == IDM_RECENT_BASE + 900) {
        return FALSE;
    }
    
    RecentItem *items = NULL;
    int n = recent_get_items(&items, g_cfg.recentMax > 0 ? g_cfg.recentMax : 12);
    int idx = cmd - IDM_RECENT_BASE;
    
    BOOL result = FALSE;
    if (idx >= 0 && idx < n) {
        recent_open_parent_folder(&items[idx]);
        result = TRUE;
    }
    
    if (items) LocalFree(items);
    return result;
}

void ShowWinXMenu(HWND owner, POINT screenPt) {
    g_suppressCmd = 0;
    if (screenPt.x == 0 && screenPt.y == 0) {
        screenPt = compute_menu_pos(owner);
    }
    // Reload config on each open so INI toggles apply immediately in background mode.
    config_load(&g_cfg);
    do {
        HMENU hMenu = build_menu();
        UINT dllCommand = 0;
        BOOL handledByDll = FALSE;
        SetForegroundWindow(owner);
        if (g_cfg.rootMenuLargeIcons) {
            WMM_BIG_MENU_ITEM snapshot[256];
            WMM_BIG_MENU_SHOW_PARAMS params;
            WMM_BIG_MENU_RESULT result;

            ZeroMemory(&params, sizeof(params));
            ZeroMemory(&result, sizeof(result));
            params.cbSize = sizeof(params);
            params.apiVersion = WMM_BIG_MENU_API_VERSION;
            params.owner = owner;
            params.screenPt = screenPt;
            params.pointerRelative = g_cfg.pointerRelative;
            params.hPlacement = g_cfg.hPlacement;
            params.vPlacement = g_cfg.vPlacement;
            params.darkMode = theme_is_dark();
            params.largeRootIcons = g_cfg.rootMenuLargeIcons;
            params.nativeRootMenu = hMenu;
            params.initNativeSubmenu = big_menu_init_native_submenu_callback;
            params.showNativeSubmenu = big_menu_show_native_submenu_callback;
            params.items = snapshot;
            params.itemCount = build_big_menu_snapshot(hMenu, snapshot, ARRAYSIZE(snapshot));
            params.animationDirection = (UINT)g_cfg.animationDirection;
            params.keepLargeMenuHighlightTextColor = g_cfg.keepLargeMenuHighlightTextColor;
            params.highlightFrame = (UINT)g_cfg.largeMenuHighlightFrame;
            result.cbSize = sizeof(result);

            handledByDll = BigMenuBridge_ShowIfAvailable(&params, &result);
            dllCommand = result.selectedCommandId;
        }
        if (handledByDll) {
            PostMessageW(owner, WM_NULL, 0, 0);
            if (dllCommand != 0) {
                MenuExecuteCommand(owner, dllCommand);
            }
            if (g_rootMenuHandle == hMenu) g_rootMenuHandle = NULL;
            DestroyMenu(hMenu);
            return;
        }
        UINT flags = TPM_RIGHTBUTTON | TPM_VERPOSANIMATION | TPM_HORIZONTAL | TPM_RETURNCMD;
        if (g_cfg.pointerRelative) {
            // Anchor at pointer; default to left/top align so menu grows down/right from cursor
            flags |= TPM_LEFTALIGN | TPM_TOPALIGN;
        } else {
            // Align according to configured placement
            if (g_cfg.hPlacement == 0) flags |= TPM_LEFTALIGN;
            else if (g_cfg.hPlacement == 1) flags |= TPM_CENTERALIGN;
            else flags |= TPM_RIGHTALIGN;

            if (g_cfg.vPlacement == 0) flags |= TPM_TOPALIGN;
            else if (g_cfg.vPlacement == 1) flags |= TPM_VCENTERALIGN;
            else flags |= TPM_BOTTOMALIGN;
        }
        g_shouldReopenMenu = FALSE;
        g_rootMenuWindow = NULL;
        SetTimer(owner, IDT_MENU_WINDOW_REFRESH, 15, NULL);
        MenuRefreshVisibleMenuWindows();
        begin_menu_corner_hook();
        int cmd = TrackPopupMenu(hMenu, flags, screenPt.x, screenPt.y, 0, owner, NULL);
        end_menu_corner_hook();
        KillTimer(owner, IDT_MENU_WINDOW_REFRESH);
        g_rootMenuWindow = NULL;
        PostMessageW(owner, WM_NULL, 0, 0);
        MenuExecuteCommand(owner, (UINT)cmd);
        if (g_rootMenuHandle == hMenu) g_rootMenuHandle = NULL;
        DestroyMenu(hMenu);
    }
    while (g_shouldReopenMenu);
    // In background mode the window stays alive; WM_CLOSE is posted by caller when needed.
}

static HFONT get_menu_font(void) {
    NONCLIENTMETRICSW ncm = { sizeof(ncm) };
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0)) {
        return CreateFontIndirectW(&ncm.lfMenuFont);
    }
    return (HFONT)GetStockObject(DEFAULT_GUI_FONT);
}

static void set_legacy_root_owner_draw(HMENU hMenu) {
    int count = GetMenuItemCount(hMenu);
    int iconSize = get_root_icon_size();
    HDC hdc = GetDC(NULL);
    int dpi = GetDeviceCaps(hdc, LOGPIXELSX);
    int leftPad = MulDiv(6, dpi, 96);
    int iconGap = MulDiv(8, dpi, 96);
    int rightPad = MulDiv(12, dpi, 96);
    int arrowPad = MulDiv(16, dpi, 96);
    int minWidth = MulDiv(188, dpi, 96);
    int minHeight = MulDiv(38, dpi, 96);
    int maxWidth = minWidth;
    HFONT hf = get_menu_font();
    HFONT old = (HFONT)SelectObject(hdc, hf);
    TEXTMETRICW tm;
    GetTextMetricsW(hdc, &tm);
    g_legacyRootMenuItemHeight = max(minHeight, max(iconSize + MulDiv(6, dpi, 96), tm.tmHeight + MulDiv(10, dpi, 96)));

    for (int i = 0; i < count; ++i) {
        MENUITEMINFOW mii = { sizeof(mii) };
        WCHAR text[512] = L"";
        RECT rc = {0, 0, 1, 1};
        mii.fMask = MIIM_FTYPE | MIIM_SUBMENU;
        if (!GetMenuItemInfoW(hMenu, i, TRUE, &mii)) continue;
        if (mii.fType & MFT_SEPARATOR) continue;
        mii.fType |= MFT_OWNERDRAW;
        SetMenuItemInfoW(hMenu, i, TRUE, &mii);
        GetMenuStringW(hMenu, i, text, ARRAYSIZE(text), MF_BYPOSITION);
        DrawTextW(hdc, text, -1, &rc, DT_SINGLELINE | DT_CALCRECT);
        {
            int width = leftPad + iconSize + iconGap + (rc.right - rc.left) + rightPad + (mii.hSubMenu ? arrowPad : 0);
            if (width > maxWidth) maxWidth = width;
        }
    }

    g_legacyRootMenuWidth = maxWidth;
    SelectObject(hdc, old);
    if (hf && hf != GetStockObject(DEFAULT_GUI_FONT)) DeleteObject(hf);
    ReleaseDC(NULL, hdc);
}

static BOOL measure_legacy_root_menu_item(HWND owner, MEASUREITEMSTRUCT* mis) {
    UNREFERENCED_PARAMETER(owner);
    if (mis->CtlType != ODT_MENU) return FALSE;
    if (!use_legacy_root_large_icon_layout()) return FALSE;
    mis->itemWidth = g_legacyRootMenuWidth ? g_legacyRootMenuWidth : (UINT)MulDiv(188, 96, 96);
    mis->itemHeight = g_legacyRootMenuItemHeight ? g_legacyRootMenuItemHeight : (UINT)max(get_root_icon_size() + 6, 38);
    return TRUE;
}

static BOOL draw_legacy_root_menu_item(HWND owner, const DRAWITEMSTRUCT* dis) {
    if (dis->CtlType != ODT_MENU) return FALSE;
    if (!use_legacy_root_large_icon_layout()) return FALSE;
    if ((HMENU)dis->hwndItem != g_rootMenuHandle) return FALSE;

    HDC hdc = dis->hDC;
    RECT rc = dis->rcItem;
    BOOL selected = (dis->itemState & ODS_SELECTED) != 0;
    BOOL disabled = (dis->itemState & (ODS_DISABLED | ODS_GRAYED)) != 0;
    COLORREF highlightColor = GetSysColor(COLOR_HIGHLIGHT);
    COLORREF highlightTextColor = GetSysColor(COLOR_HIGHLIGHTTEXT);
    // Use system menu text color so dark/light themes are respected when keeping highlight text color
    COLORREF textColor = GetSysColor(COLOR_MENUTEXT);
    int iconSize = get_root_icon_size();
    HICON icon = NULL;
    WCHAR text[512] = L"";
    UINT id = dis->itemID;
    BOOL hasSub = FALSE;
    int idx = -1;
    int count = GetMenuItemCount(g_rootMenuHandle);
    int leftPad = 6;
    int iconGap = 8;
    int rightPad = 10;
    int arrowPad = 14;
    int dpi = GetDeviceCaps(hdc, LOGPIXELSX);
    int selectionRadius = is_windows_11_or_greater() ? MulDiv(8, dpi, 96) : 0;

    if (theme_get_menu_highlight(&highlightColor) || theme_get_accent(&highlightColor)) {
        int lum = ((30 * GetRValue(highlightColor)) + (59 * GetGValue(highlightColor)) + (11 * GetBValue(highlightColor))) / 100;
        highlightTextColor = (lum > 140) ? RGB(32, 32, 32) : RGB(255, 255, 255);
    }

    for (int i = 0; i < count; ++i) {
        if (GetMenuItemID(g_rootMenuHandle, i) == id) { idx = i; break; }
    }
    if (idx < 0) {
        for (int i = 0; i < count; ++i) {
            RECT itemRc;
            if (GetMenuItemRect(NULL, g_rootMenuHandle, i, &itemRc) && itemRc.top == rc.top && itemRc.bottom == rc.bottom) { idx = i; break; }
        }
    }
    if (idx >= 0) {
        MENUITEMINFOW mii = { sizeof(mii) };
        mii.fMask = MIIM_SUBMENU;
        if (GetMenuItemInfoW(g_rootMenuHandle, idx, TRUE, &mii)) hasSub = (mii.hSubMenu != NULL);
        GetMenuStringW(g_rootMenuHandle, idx, text, ARRAYSIZE(text), MF_BYPOSITION);
    }
    icon = get_item_icon(id);

    FillRect(hdc, &rc, GetSysColorBrush(COLOR_MENU));
    if (selected) {
        RECT sel = rc;
        BOOL drawBackground = (g_cfg.largeMenuHighlightFrame != HIGHLIGHT_BORDER);
        BOOL drawBorder = (g_cfg.largeMenuHighlightFrame != HIGHLIGHT_BACKGROUND);
        InflateRect(&sel, -1, 0);
        if (sel.top > rc.top) sel.top -= 1;
        if (sel.bottom < rc.bottom) sel.bottom += 1;
        if (drawBackground) {
            if (selectionRadius > 0) {
                draw_fake_rounded_highlight(hdc, &sel, selectionRadius, highlightColor, GetSysColor(COLOR_MENU));
            } else {
                HBRUSH hbrHighlight = CreateSolidBrush(highlightColor);
                if (hbrHighlight) {
                    FillRect(hdc, &sel, hbrHighlight);
                    DeleteObject(hbrHighlight);
                } else {
                    FillRect(hdc, &sel, GetSysColorBrush(COLOR_HIGHLIGHT));
                }
            }
        }
        if (drawBorder) {
            COLORREF stroke = drawBackground ? blend_colors(highlightColor, highlightTextColor, 96) : highlightColor;
            draw_highlight_border(hdc, &sel, selectionRadius, stroke);
        }
    }

    HFONT hf = get_menu_font();
    HFONT old = (HFONT)SelectObject(hdc, hf);
    SetBkMode(hdc, TRANSPARENT);
    if (disabled) {
        textColor = GetSysColor(COLOR_GRAYTEXT);
    } else if (selected && g_cfg.largeMenuHighlightFrame != HIGHLIGHT_BORDER && !g_cfg.keepLargeMenuHighlightTextColor) {
        textColor = highlightTextColor;
    }
    SetTextColor(hdc, textColor);

    if (icon) {
        int x = rc.left + leftPad;
        int y = rc.top + ((rc.bottom - rc.top) - iconSize) / 2;
        DrawIconEx(hdc, x, y, icon, iconSize, iconSize, 0, NULL, DI_NORMAL);
    }

    RECT trc = rc;
    trc.left += leftPad + iconSize + iconGap;
    trc.right -= rightPad + (hasSub ? arrowPad : 0);
    DrawTextW(hdc, text, -1, &trc, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);

    if (hasSub) {
        RECT arrowRc = rc;
        arrowRc.left = arrowRc.right - arrowPad;
        draw_root_menu_submenu_arrow(hdc, &arrowRc, textColor);
    }

    SelectObject(hdc, old);
    if (hf && hf != GetStockObject(DEFAULT_GUI_FONT)) DeleteObject(hf);
    return TRUE;
}

// ===== Modern owner-draw implementation (compiled only when ENABLE_MODERN_STYLE) =====
#ifdef ENABLE_MODERN_STYLE

static COLORREF blend(COLORREF a, COLORREF b, int alpha /*0..255*/) {
    int inv = 255 - alpha;
    int r = (GetRValue(a)*inv + GetRValue(b)*alpha) / 255;
    int g = (GetGValue(a)*inv + GetGValue(b)*alpha) / 255;
    int bl = (GetBValue(a)*inv + GetBValue(b)*alpha) / 255;
    return RGB(r,g,bl);
}

static void draw_chevron(HDC hdc, RECT rc, COLORREF color) {
    // Draw a simple '>' chevron near the right edge
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, color);
    WCHAR ch = L'>';
    RECT r = rc; r.left = r.right - 16; // padding for chevron
    DrawTextW(hdc, &ch, 1, &r, DT_SINGLELINE | DT_VCENTER | DT_RIGHT);
}

static int get_item_index_from_dis(const DRAWITEMSTRUCT* dis) {
    HMENU m = (HMENU)dis->hwndItem;
    int count = GetMenuItemCount(m);
    // Prefer stored index when provided (non-zero, within range)
    UINT idx = (UINT)dis->itemData;
    if (idx < (UINT)count) return (int)idx;
    // Fallback: match rectangle
    for (int i = 0; i < count; ++i) {
        RECT r; if (GetMenuItemRect(NULL, m, i, &r)) {
            if (r.top == dis->rcItem.top && r.bottom == dis->rcItem.bottom) return i;
        }
    }
    return -1;
}

static void set_owner_for_menu_item(HMENU m, int i) {
    MENUITEMINFOW mii = { sizeof(mii) };
    mii.fMask = MIIM_FTYPE | MIIM_SUBMENU | MIIM_DATA | MIIM_ID | MIIM_STATE;
    if (!GetMenuItemInfoW(m, i, TRUE, &mii)) return;
    if (mii.fType & MFT_SEPARATOR) return;
    mii.fType |= MFT_OWNERDRAW;
    // Only set positional itemData if not already used (e.g., we use dwItemData to store paths on folder popups)
    if (mii.dwItemData == 0 && mii.hSubMenu == NULL) {
        mii.dwItemData = (ULONG_PTR)i; // local index for lookup
        mii.fMask |= MIIM_DATA;
    }
    SetMenuItemInfoW(m, i, TRUE, &mii);
}

void Menu_SetOwnerDrawRecursive(HMENU m) {
    int count = GetMenuItemCount(m);
    for (int i = 0; i < count; ++i) {
        set_owner_for_menu_item(m, i);
        MENUITEMINFOW mii = { sizeof(mii) };
        mii.fMask = MIIM_SUBMENU;
        if (GetMenuItemInfoW(m, i, TRUE, &mii) && mii.hSubMenu) {
            Menu_SetOwnerDrawRecursive(mii.hSubMenu);
        }
    }
}

BOOL MenuOnMeasureItem(HWND owner, MEASUREITEMSTRUCT* mis) {
    if (measure_legacy_root_menu_item(owner, mis)) return TRUE;
    if (mis->CtlType != ODT_MENU) return FALSE;
    if (mis->itemID == IDM_SIZER) return FALSE; // unused
    if (g_cfg.menuStyle != STYLE_MODERN) return FALSE; // system draws legacy
    // Measure text size (menu handle not needed here)
    HDC hdc = GetDC(owner);
    HFONT hf = get_menu_font();
    HFONT old = (HFONT)SelectObject(hdc, hf);
    WCHAR text[512] = L"";
    // Retrieve by scanning rectangle index
    // We don't have DRAWITEMSTRUCT here; measure uses MEASUREITEMSTRUCT, which doesn't give rect.
    // But system calls measure before draw, with itemData we set to index for non-popup items.
    UINT idx = (UINT)mis->itemData;
    int width = 0, height = 0;
    if (idx != 0xFFFFFFFF) {
        // Try to get text via GetMenuString by position using the menu handle stored in CtlID? Not available.
        // As a fallback, pick a reasonable width based on item ID text length later; use a safe default.
    }
    RECT rc = {0,0,1,1};
    // Use a generic sample text to compute height; final width gets adjusted in Draw with DT_CALCRECT
    DrawTextW(hdc, L"Ay", -1, &rc, DT_SINGLELINE | DT_CALCRECT);
    height = (rc.bottom - rc.top);
    int padY = 10; // top/bottom padding
    int minH = 28;
    int calcH = height + padY*2;
    if (calcH < minH) calcH = minH;
    mis->itemHeight = (UINT)calcH;
    // Width: will be recomputed in Draw via DT_CALCRECT; provide nominal
    // Target width for modern: MenuWidth override (226..255) if set, else DPI-based default (~264 @ 96dpi)
    int w = 0;
    if (g_cfg.menuWidth >= 226 && g_cfg.menuWidth <= 255) {
        // Scale the logical width similarly across DPI to keep perceived width consistent
        HDC sdc = GetDC(owner);
        int dpi = GetDeviceCaps(sdc, LOGPIXELSX);
        ReleaseDC(owner, sdc);
        w = MulDiv(g_cfg.menuWidth, dpi, 96);
    } else {
        HDC sdc = GetDC(owner);
        int dpi = GetDeviceCaps(sdc, LOGPIXELSX);
        ReleaseDC(owner, sdc);
        w = MulDiv(264, dpi, 96);
    }
    mis->itemWidth = w;
    SelectObject(hdc, old);
    if (hf && hf != GetStockObject(DEFAULT_GUI_FONT)) DeleteObject(hf);
    ReleaseDC(owner, hdc);
    return TRUE;
}

BOOL MenuOnDrawItem(HWND owner, const DRAWITEMSTRUCT* dis) {
    if (draw_legacy_root_menu_item(owner, dis)) return TRUE;
    if (dis->CtlType != ODT_MENU) return FALSE;
    if (dis->itemID == IDM_SIZER) return FALSE; // unused
    if (g_cfg.menuStyle != STYLE_MODERN) return FALSE; // legacy system drawn
    HDC hdc = dis->hDC;
    RECT rc = dis->rcItem;
    BOOL selected = (dis->itemState & ODS_SELECTED) != 0; // includes keyboard or mouse hot state
    BOOL disabled = (dis->itemState & (ODS_DISABLED | ODS_GRAYED)) != 0;
    BOOL dark = theme_is_dark();
    BOOL modern = TRUE;

    COLORREF bg = (dark ? RGB(32,32,32) : RGB(255,255,255));
    COLORREF txt = (dark ? RGB(240,240,240) : RGB(32,32,32));
    COLORREF disTxt = (dark ? RGB(120,120,120) : RGB(160,160,160));
    COLORREF sel = (dark ? RGB(60,60,60) : RGB(230,230,230));
    COLORREF accent;
    if (theme_get_accent(&accent)) {
        // Use accent as selection background; adjust text color for contrast
        sel = accent;
        int lum = ( (30*GetRValue(accent)) + (59*GetGValue(accent)) + (11*GetBValue(accent)) ) / 100; // 0-255
        if (lum > 140) { txt = RGB(32,32,32); } else { txt = RGB(245,245,245); }
    }

    // Fill background
    HBRUSH hbrBg = CreateSolidBrush(bg);
    FillRect(hdc, &rc, hbrBg);
    DeleteObject(hbrBg);

    // Full-width accent selection (modern only)
    if (selected) {
        HBRUSH hbrSel = CreateSolidBrush(sel);
        FillRect(hdc, &rc, hbrSel);
        DeleteObject(hbrSel);
    }

    // Discover item index and submenu presence
    HMENU m = (HMENU)dis->hwndItem;
    int idx = get_item_index_from_dis(dis);
    BOOL hasSub = FALSE;
    if (idx >= 0) {
        MENUITEMINFOW mii = { sizeof(mii) };
        mii.fMask = MIIM_SUBMENU;
        if (GetMenuItemInfoW(m, idx, TRUE, &mii)) hasSub = (mii.hSubMenu != NULL);
    }

    // Fetch text by position
    WCHAR text[512] = L"";
    if (idx >= 0) GetMenuStringW(m, idx, text, ARRAYSIZE(text), MF_BYPOSITION);

    // Draw text
    HFONT hf = get_menu_font();
    HFONT oldF = (HFONT)SelectObject(hdc, hf);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(0, 0, 0));
    // Optional icon mapped by command ID
    HICON icon = NULL;
    if (idx >= 0) {
        UINT id = (UINT)-1;
        MENUITEMINFOW miiID = { sizeof(miiID) };
        miiID.fMask = MIIM_ID;
        if (GetMenuItemInfoW(m, idx, TRUE, &miiID)) {
            id = miiID.wID;
        }
        if (id == (UINT)-1) id = GetMenuItemID(m, idx); // Fallback

        if (id != (UINT)-1) icon = get_item_icon(id);
    }

    HDC sdc = GetDC(owner);
    int dpi = GetDeviceCaps(sdc, LOGPIXELSX);
    ReleaseDC(owner, sdc);
    int iconSize = MulDiv(16, dpi, 96);

    int leftPad = 16;
    if (icon) {
        int cx=iconSize, cy=iconSize;
        int x = rc.left + 8; int y = rc.top + ( (rc.bottom-rc.top) - cy )/2;
        DrawIconEx(hdc, x, y, icon, cx, cy, 0, NULL, DI_NORMAL);
        leftPad = 8 + cx + 8;
    }
    RECT trc = rc; trc.left += leftPad; trc.right -= (hasSub ? 20 : 8); // right room for chevron
    DrawTextW(hdc, text, -1, &trc, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);
    if (hasSub && modern) draw_chevron(hdc, rc, RGB(0, 0, 0));
    SelectObject(hdc, oldF);
    if (hf && hf != GetStockObject(DEFAULT_GUI_FONT)) DeleteObject(hf);
    return TRUE;
}
#endif // ENABLE_MODERN_STYLE

#ifndef ENABLE_MODERN_STYLE
// Stubs when modern style is compiled out
BOOL MenuOnMeasureItem(HWND owner, MEASUREITEMSTRUCT* mis) { return measure_legacy_root_menu_item(owner, mis); }
BOOL MenuOnDrawItem(HWND owner, const DRAWITEMSTRUCT* dis) { return draw_legacy_root_menu_item(owner, dis); }
#endif

// ===== Legacy icons via item bitmaps (no owner-draw) =====
static HBITMAP icon_to_hbmp(HICON hico, int cx, int cy) {
    if (!hico) return NULL;
    BITMAPINFO bmi = {0};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = cx;
    bmi.bmiHeader.biHeight = -cy; // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    void* bits = NULL;
    HDC hdc = GetDC(NULL);
    HBITMAP hbmp = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (hbmp) {
        HDC mem = CreateCompatibleDC(hdc);
        HBITMAP old = (HBITMAP)SelectObject(mem, hbmp);
        RECT rc = {0,0,cx,cy};
        HBRUSH hb = CreateSolidBrush(RGB(0,0,0)); // clear to 0
        FillRect(mem, &rc, hb); DeleteObject(hb);
        DrawIconEx(mem, 0, 0, hico, cx, cy, 0, NULL, DI_NORMAL);
        SelectObject(mem, old);
        DeleteDC(mem);
    }
    ReleaseDC(NULL, hdc);
    return hbmp;
}

static void assign_menu_item_bitmap_sized(HMENU hMenu, UINT id, HICON hico, int size) {
    if (!hico) return;
    HBITMAP hb = icon_to_hbmp(hico, size, size);
    if (!hb) return;
    MENUITEMINFOW mii = { sizeof(mii) };
    mii.fMask = MIIM_BITMAP;
    mii.hbmpItem = hb;
    SetMenuItemInfoW(hMenu, id, FALSE, &mii);
    if (g_itemBmpCount < ARRAYSIZE(g_itemBmps)) g_itemBmps[g_itemBmpCount++] = (ItemBmp){ id, hb };
}

static void assign_menu_pos_bitmap_sized(HMENU hMenu, int pos, HICON hico, int size) {
    if (!hico) return;
    HBITMAP hb = icon_to_hbmp(hico, size, size);
    if (!hb) return;
    MENUITEMINFOW mii = { sizeof(mii) };
    mii.fMask = MIIM_BITMAP;
    mii.hbmpItem = hb;
    SetMenuItemInfoW(hMenu, pos, TRUE, &mii);
}

static void assign_legacy_item_bitmap(HMENU hMenu, UINT id, HICON hico) {
    assign_menu_item_bitmap_sized(hMenu, id, hico, get_small_icon_size());
}
