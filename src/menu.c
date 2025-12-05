// Clean implementation of menu building and handlers (sorting removed, visibility filters kept)

#include <windows.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <shlobj.h>
#include <stdlib.h> // _wtoi
#include "menu.h"
#include "config.h"
#include "recent.h"
#include "util.h"
#include "theme.h"

#ifndef ARRAYSIZE
#define ARRAYSIZE(a) (sizeof(a)/sizeof((a)[0]))
#endif

#define IDM_DYNAMIC_BASE  1000
#define IDM_RECENT_BASE   2000
#define IDM_SIZER         9000

BOOL g_shouldReopenMenu = FALSE;

typedef struct MapEntry {
    UINT id;
    WCHAR path[MAX_PATH];
} MapEntry;

typedef struct FolderMenuData {
    WCHAR path[MAX_PATH];
    int depth;
    int offset;
} FolderMenuData;

// Forward declaration
static void attach_menu_data(HMENU hMenu, const WCHAR* path, int depth, int offset);


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
// Forward declarations for legacy icon helpers
static HBITMAP icon_to_hbmp(HICON hico, int cx, int cy);
static void assign_legacy_item_bitmap(HMENU hMenu, UINT id, HICON hico);
static void add_item_icon(UINT id, HICON h) {
    if (!h) return;
    for (UINT i=0;i<g_itemIconCount;i++) if (g_itemIcons[i].id==id) { g_itemIcons[i].h=h; return; }
    if (g_itemIconCount < ARRAYSIZE(g_itemIcons)) { g_itemIcons[g_itemIconCount++] = (ItemIcon){ id, h }; }
}
static HICON get_item_icon(UINT id) {
    for (UINT i=0;i<g_itemIconCount;i++) if (g_itemIcons[i].id==id) return g_itemIcons[i].h;
    return NULL;
}

// Assign an icon (converted to bitmap) to the most recently added item (typically a popup root)
static void assign_icon_to_last_popup(HMENU hMenu, HICON hico) {
    if (!hico) return;
    int count = GetMenuItemCount(hMenu);
    if (count <= 0) return;
    int pos = count - 1;
    HBITMAP hb = icon_to_hbmp(hico, 16, 16);
    if (!hb) return;
    MENUITEMINFOW mii = { sizeof(mii) };
    mii.fMask = MIIM_BITMAP;
    mii.hbmpItem = hb;
    SetMenuItemInfoW(hMenu, pos, TRUE, &mii);
}

// Helper: attempt to load an icon from either a direct .ico file path (existing logic)
// or from a module (DLL/EXE) with an index specified using ",<index>" syntax.
// Accepted forms:
//   C:\Windows\System32\shell32.dll,10
//   shell32.dll,10            (searches System32)
//   imageres.dll,-3           (negative index still passed through; Windows treats as resource ID)
// If no comma present, falls back to LoadImageW for .ico file.
static HICON load_icon_path_or_module(const WCHAR* spec) {
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
        return (HICON)LoadImageW(NULL, spec, IMAGE_ICON, 16, 16, LR_LOADFROMFILE|LR_SHARED);
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
    extracted = ExtractIconExW(module, idx, &hLarge, &hSmall, 1);
    if (extracted == 0) {
        return (HICON)LoadImageW(NULL, spec, IMAGE_ICON, 16, 16, LR_LOADFROMFILE|LR_SHARED);
    }
    if (hLarge) DestroyIcon(hLarge);
    return hSmall;
}

static HICON get_system_folder_icon(void) {
    static HICON hFolder = NULL;
    if (hFolder) return hFolder;
    // Use a fake folder name to ensure we get the standard folder icon, not drive icon
    SHFILEINFOW sfi = {0};
    if (SHGetFileInfoW(L"FakeFolderName", FILE_ATTRIBUTE_DIRECTORY, &sfi, sizeof(sfi), SHGFI_ICON | SHGFI_USEFILEATTRIBUTES | SHGFI_SMALLICON)) {
        hFolder = sfi.hIcon; // cache handle (system-managed small icon)
    }
    return hFolder;
}

static void map_add(UINT id, const WCHAR* path) {
    if (g_mapCount < ARRAYSIZE(g_map)) {
        g_map[g_mapCount].id = id;
        lstrcpynW(g_map[g_mapCount].path, path, ARRAYSIZE(g_map[g_mapCount].path));
        g_mapCount++;
    }
}

static void attach_menu_data(HMENU hMenu, const WCHAR* path, int depth, int offset) {
    FolderMenuData* data = (FolderMenuData*)LocalAlloc(LMEM_FIXED|LMEM_ZEROINIT, sizeof(FolderMenuData));
    if (!data) return;
    lstrcpynW(data->path, path, ARRAYSIZE(data->path));
    data->depth = depth;
    data->offset = offset;
    MENUINFO mi = { sizeof(mi) };
    mi.fMask = MIM_MENUDATA;
    mi.dwMenuData = (ULONG_PTR)data;
    SetMenuInfo(hMenu, &mi);
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
    for (int i = 0; i < n; ++i) {
        if (!items[i].path[0]) continue; // skip empty (defensive)
        WCHAR text[MAX_PATH + 8];
        if (g_cfg.recentLabelMode == 1) {
            // filename only (optionally strip extension)
            const WCHAR* p = wcsrchr(items[i].path, L'\\');
            const WCHAR* name = p ? p + 1 : items[i].path;
            lstrcpynW(text, name, ARRAYSIZE(text));
            BOOL stripExt = FALSE;
            // Inverted semantics: showExtensions/recentShowExtensions mean KEEP extensions
            // We strip when the corresponding show flag is false.
            if (!g_cfg.recentShowExtensions) stripExt = TRUE; // explicit recent override to hide
            else if (!g_cfg.showExtensions) stripExt = TRUE;   // fallback to global hide when recent doesn't force show
            if (stripExt && text[0] != L'.') {
                WCHAR* dot = wcsrchr(text, L'.');
                if (dot) *dot = 0;
            }
        } else {
            // full path (unchanged)
            lstrcpynW(text, items[i].path, ARRAYSIZE(text));
        }
        AppendMenuW(sub, MF_STRING, IDM_RECENT_BASE + i, text);
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

    // Folders first logic
    if (g_cfg.sortFoldersFirst) {
        if (fa->isDir && !fb->isDir) return -1;
        if (!fa->isDir && fb->isDir) return 1;
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

static int fill_menu_with_folder(HMENU hMenu, int insertPos, const WCHAR* path, int depth, int offset) {
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
            if (depth < g_cfg.folderMaxDepth) {
                HMENU sub = CreatePopupMenu();
                AppendMenuW(sub, MF_STRING | MF_GRAYED, 0, L"(Loading...)");
                attach_menu_data(sub, items[i].fullPath, depth + 1, 0);
                
                MENUITEMINFOW mii = { sizeof(mii) };
                mii.fMask = MIIM_STRING | MIIM_SUBMENU | MIIM_DATA;
                mii.dwTypeData = name;
                mii.hSubMenu = sub;
                mii.dwItemData = (ULONG_PTR)LocalAlloc(LMEM_FIXED, (lstrlenW(items[i].fullPath) + 1) * sizeof(WCHAR));
                if (mii.dwItemData) lstrcpyW((LPWSTR)mii.dwItemData, items[i].fullPath);
                InsertMenuItemW(hMenu, insertPos + added, TRUE, &mii);
            } else {
                MENUITEMINFOW mii = { sizeof(mii) };
                mii.fMask = MIIM_STRING | MIIM_ID | MIIM_DATA;
                mii.dwTypeData = name;
                mii.wID = g_nextFolderId++;
                mii.dwItemData = (ULONG_PTR)LocalAlloc(LMEM_FIXED, (lstrlenW(items[i].fullPath) + 1) * sizeof(WCHAR));
                if (mii.dwItemData) lstrcpyW((LPWSTR)mii.dwItemData, items[i].fullPath);
                InsertMenuItemW(hMenu, insertPos + added, TRUE, &mii);
                map_add(mii.wID, items[i].fullPath);
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
        }
        added++;
    }

    if (end < count) {
        // Show More Items
        HMENU sub = CreatePopupMenu();
        AppendMenuW(sub, MF_STRING | MF_GRAYED, 0, L"(Loading...)");
        attach_menu_data(sub, path, depth, end);
        
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

    fill_menu_with_folder(parent, GetMenuItemCount(parent), data->path, data->depth, data->offset);
}

static HMENU build_menu(void) {
    config_load(&g_cfg);
    HMENU hMenu = CreatePopupMenu();
    g_mapCount = 0; // reset mapping for this menu build
    g_nextFolderId = IDM_FOLDER_BASE;
    UINT id = IDM_DYNAMIC_BASE;
    for (int i = 0; i < g_cfg.count; ++i) {
        ConfigItem* it = &g_cfg.items[i];
        switch (it->type) {
        case CI_SEPARATOR:
            AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
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
                HICON hico = load_icon_path_or_module(ipath);
                add_item_icon(id, hico);
                if (g_cfg.menuStyle == STYLE_LEGACY && g_cfg.showIcons) assign_legacy_item_bitmap(hMenu, id, hico);
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
                attach_menu_data(sub, it->path, 1, 0);
                AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)sub, it->label[0] ? it->label : it->path);
                MENUITEMINFOW mii = { sizeof(mii) };
                mii.fMask = MIIM_DATA | MIIM_SUBMENU;
                mii.dwItemData = (ULONG_PTR)LocalAlloc(LMEM_FIXED, (lstrlenW(it->path) + 1) * sizeof(WCHAR));
                if (mii.dwItemData) lstrcpyW((LPWSTR)mii.dwItemData, it->path);
                mii.hSubMenu = sub;
                int pos = GetMenuItemCount(hMenu) - 1;
                SetMenuItemInfoW(hMenu, pos, TRUE, &mii);
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
                
                fill_menu_with_folder(hMenu, GetMenuItemCount(hMenu), it->path, 1, 0);
                
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
                        hico = get_system_folder_icon();
                    } else {
                        // Fall back to defaults
                        if (dark && g_cfg.defaultIconPathDark[0]) ipath = g_cfg.defaultIconPathDark;
                        else if (!dark && g_cfg.defaultIconPathLight[0]) ipath = g_cfg.defaultIconPathLight;
                        else if (g_cfg.defaultIconPath[0]) ipath = g_cfg.defaultIconPath;
                    }
                }
                if (!hico && ipath) {
                    hico = load_icon_path_or_module(ipath);
                }
                if (hico) {
                    add_item_icon(id, hico);
                    if (g_cfg.menuStyle == STYLE_LEGACY && g_cfg.showIcons) assign_legacy_item_bitmap(hMenu, id, hico);
                }
                id++;
            }
            break;
        }
        case CI_FOLDER_SUBMENU:
        {
            HMENU sub = CreatePopupMenu();
            AppendMenuW(sub, MF_STRING | MF_GRAYED, 0, L"(Loading...)");
            attach_menu_data(sub, it->path, 1, 0);
            AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)sub, it->label[0] ? it->label : it->path);
            if (g_cfg.menuStyle == STYLE_LEGACY && g_cfg.showIcons) {
                const BOOL dark = theme_is_dark();
                HICON hicoF = NULL;
                const WCHAR* ipath = NULL;
                // Prefer per-item icon first
                if (dark && it->iconPathDark[0]) ipath = it->iconPathDark;
                else if (!dark && it->iconPathLight[0]) ipath = it->iconPathLight;
                else if (it->iconPath[0]) ipath = it->iconPath;
                if (!ipath) {
                    if (g_cfg.showFolderIcons) {
                        hicoF = get_system_folder_icon();
                    } else {
                        if (dark && g_cfg.defaultIconPathDark[0]) ipath = g_cfg.defaultIconPathDark;
                        else if (!dark && g_cfg.defaultIconPathLight[0]) ipath = g_cfg.defaultIconPathLight;
                        else if (g_cfg.defaultIconPath[0]) ipath = g_cfg.defaultIconPath;
                    }
                }
                if (!hicoF && ipath) hicoF = load_icon_path_or_module(ipath);
                assign_icon_to_last_popup(hMenu, hicoF);
            }
            MENUITEMINFOW mii = { sizeof(mii) };
            mii.fMask = MIIM_DATA | MIIM_SUBMENU;
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
            if (g_cfg.menuStyle == STYLE_LEGACY && g_cfg.showIcons) {
                const BOOL dark = theme_is_dark();
                HICON hicoR = NULL;
                const WCHAR* ipath = NULL;
                if (dark && it->iconPathDark[0]) ipath = it->iconPathDark;
                else if (!dark && it->iconPathLight[0]) ipath = it->iconPathLight;
                else if (it->iconPath[0]) ipath = it->iconPath;
                else if (dark && g_cfg.defaultIconPathDark[0]) ipath = g_cfg.defaultIconPathDark;
                else if (!dark && g_cfg.defaultIconPathLight[0]) ipath = g_cfg.defaultIconPathLight;
                else if (g_cfg.defaultIconPath[0]) ipath = g_cfg.defaultIconPath;
                if (ipath) hicoR = load_icon_path_or_module(ipath);
                assign_icon_to_last_popup(hMenu, hicoR);
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
            if (g_cfg.menuStyle == STYLE_LEGACY && g_cfg.showIcons) {
                const BOOL dark = theme_is_dark();
                HICON hicoP = NULL;
                const WCHAR* ipath = NULL;
                if (dark && it->iconPathDark[0]) ipath = it->iconPathDark;
                else if (!dark && it->iconPathLight[0]) ipath = it->iconPathLight;
                else if (it->iconPath[0]) ipath = it->iconPath;
                else if (dark && g_cfg.defaultIconPathDark[0]) ipath = g_cfg.defaultIconPathDark;
                else if (!dark && g_cfg.defaultIconPathLight[0]) ipath = g_cfg.defaultIconPathLight;
                else if (g_cfg.defaultIconPath[0]) ipath = g_cfg.defaultIconPath;
                if (ipath) hicoP = load_icon_path_or_module(ipath);
                assign_icon_to_last_popup(hMenu, hicoP);
            }
            break;
        }
        }
    }
    theme_style_menu(hMenu);
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

void MenuExecuteCommand(HWND owner, UINT cmd) {
    if (!cmd) return;
    for (UINT i = 0; i < g_mapCount; ++i) {
        if (g_map[i].id == (UINT)cmd) {
            // Interpret special power markers
            if (!lstrcmpiW(g_map[i].path, L"POWER_SLEEP")) { system_sleep(); return; }
            if (!lstrcmpiW(g_map[i].path, L"POWER_SHUTDOWN")) { system_shutdown(FALSE); return; }
            if (!lstrcmpiW(g_map[i].path, L"POWER_RESTART")) { system_shutdown(TRUE); return; }
            if (!lstrcmpiW(g_map[i].path, L"POWER_LOCK")) { LockWorkStation(); return; }
            if (!lstrcmpiW(g_map[i].path, L"POWER_LOGOFF")) { ExitWindowsEx(EWX_LOGOFF, 0); return; }
        if (!lstrcmpiW(g_map[i].path, L"POWER_HIBERNATE")) { system_hibernate(); return; }
            open_shell_item(g_map[i].path); return; }
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
        case CI_SEPARATOR: break;
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
            break;
        }
    }
}

void ShowWinXMenu(HWND owner, POINT screenPt) {
    HMENU hMenu = build_menu();
    if (screenPt.x == 0 && screenPt.y == 0) {
        screenPt = compute_menu_pos(owner);
    }
    SetForegroundWindow(owner);
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
    int cmd = TrackPopupMenu(hMenu, flags, screenPt.x, screenPt.y, 0, owner, NULL);
    PostMessageW(owner, WM_NULL, 0, 0);
    MenuExecuteCommand(owner, (UINT)cmd);
    DestroyMenu(hMenu);
    // In background mode the window stays alive; WM_CLOSE is posted by caller when needed.
}

// ===== Modern owner-draw implementation (compiled only when ENABLE_MODERN_STYLE) =====
#ifdef ENABLE_MODERN_STYLE

static HFONT get_menu_font() {
    NONCLIENTMETRICSW ncm = { sizeof(ncm) };
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0)) {
        return CreateFontIndirectW(&ncm.lfMenuFont);
    }
    return (HFONT)GetStockObject(DEFAULT_GUI_FONT);
}

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
    SetTextColor(hdc, disabled ? disTxt : txt);
    // Optional icon mapped by command ID
    HICON icon = NULL;
    if (idx >= 0) {
        UINT id = GetMenuItemID(m, idx);
        if (id != (UINT)-1) icon = get_item_icon(id);
    }
    int leftPad = 16;
    if (icon) {
        int cx=16, cy=16;
        int x = rc.left + 8; int y = rc.top + ( (rc.bottom-rc.top) - cy )/2;
        DrawIconEx(hdc, x, y, icon, cx, cy, 0, NULL, DI_NORMAL);
        leftPad = 8 + cx + 8;
    }
    RECT trc = rc; trc.left += leftPad; trc.right -= (hasSub ? 20 : 8); // right room for chevron
    DrawTextW(hdc, text, -1, &trc, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);
    if (hasSub && modern) draw_chevron(hdc, rc, disabled ? disTxt : txt);
    SelectObject(hdc, oldF);
    if (hf && hf != GetStockObject(DEFAULT_GUI_FONT)) DeleteObject(hf);
    return TRUE;
}
#endif // ENABLE_MODERN_STYLE

#ifndef ENABLE_MODERN_STYLE
// Stubs when modern style is compiled out
BOOL MenuOnMeasureItem(HWND owner, MEASUREITEMSTRUCT* mis) { UNREFERENCED_PARAMETER(owner); UNREFERENCED_PARAMETER(mis); return FALSE; }
BOOL MenuOnDrawItem(HWND owner, const DRAWITEMSTRUCT* dis) { UNREFERENCED_PARAMETER(owner); UNREFERENCED_PARAMETER(dis); return FALSE; }
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

static void assign_legacy_item_bitmap(HMENU hMenu, UINT id, HICON hico) {
    if (!hico) return;
    HBITMAP hb = icon_to_hbmp(hico, 16, 16);
    if (!hb) return;
    MENUITEMINFOW mii = { sizeof(mii) };
    mii.fMask = MIIM_BITMAP;
    mii.hbmpItem = hb;
    SetMenuItemInfoW(hMenu, id, FALSE, &mii);
    if (g_itemBmpCount < ARRAYSIZE(g_itemBmps)) g_itemBmps[g_itemBmpCount++] = (ItemBmp){ id, hb };
}
