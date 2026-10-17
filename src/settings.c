
// Reconstructed clean settings.c to address prior hidden corruption causing C2181.
#include "settings.h"
#include "settings_extra.h"
#include "winui3_launcher.h"
#include "resource.h"
#include "theme.h"
#include "util.h"
#include "update_check.h"
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#ifndef LVIM_BEFORE
#define LVIM_BEFORE 0x00000001
#endif
#ifndef LVIM_AFTER
#define LVIM_AFTER 0x00000002
#endif
#ifndef LVM_SETINSERTMARKCOLOR
#define LVM_SETINSERTMARKCOLOR (LVM_FIRST + 166)
#endif
#include <shlwapi.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <string.h>
#include <stdlib.h>
#include <strsafe.h>
#include "ini_transaction.h"
static IniTransaction g_saveTransaction;
static BOOL settings_write(const WCHAR* section, const WCHAR* key, const WCHAR* value, const WCHAR* path) {
    return ini_transaction_write(&g_saveTransaction, section, key, value, path);
}

// Forward declaration for dialog procedure
static INT_PTR CALLBACK MainDlgProc(HWND dlg, UINT msg, WPARAM wParam, LPARAM lParam);
static HWND g_activeSettingsDlg = NULL;

// Shows the settings dialog (modal). Returns TRUE if any values were changed and saved.
BOOL ShowSettingsDialog(HWND owner, Config* cfg) {
    if (g_activeSettingsDlg && IsWindow(g_activeSettingsDlg)) {
        ShowWindow(g_activeSettingsDlg, SW_SHOWNORMAL);
        SetForegroundWindow(g_activeSettingsDlg);
        return FALSE;
    }

    if (cfg) {
        WCHAR iniPath[MAX_PATH] = {0};
        if (cfg->iniPath[0]) {
            lstrcpynW(iniPath, cfg->iniPath, ARRAYSIZE(iniPath));
            config_set_default_path(iniPath);
        }
        config_load(cfg);
    }

    g_settingsOwnerHwnd = owner;
    INT_PTR result = DialogBoxParamW(
        GetModuleHandleW(NULL),
        MAKEINTRESOURCEW(IDD_SETTINGS),
        owner,
        MainDlgProc,
        (LPARAM)cfg
    );
    g_activeSettingsDlg = NULL;
    g_settingsOwnerHwnd = NULL;
    return (result == IDOK) ? TRUE : FALSE;
}

typedef struct SettingsState {
    Config* cfg;
    // Track original power option states
    BOOL origExcludeSleep, origExcludeHibernate, origExcludeShutdown, origExcludeRestart, origExcludeLock, origExcludeLogoff;
    HWND hTabs;
    HWND pages[7]; // General, Placement, Menu, Icons, hidden Sorting, Controls, Advanced
    // Working copy of menu/icon items so Apply/Save commits atomically
    ConfigItem workingItems[64];
    int workingCount;
    BOOL workingDirty; // set when workingItems modified
    int baseW, baseH; // initial dialog size for min constraint
    HFONT hItalic; // italic font for filename label
    BOOL reloadNeeded; // set if tray reload is needed after Save & Close
    FILETIME loadedConfigWriteTime;
    ULONGLONG loadedConfigSize;
    // Drag state for menu list reordering
    BOOL dragging;
    int dragSource; // index in workingItems being dragged
    HWND hDragBadge; // follows the pointer and identifies the dragged menu item
    int insertTarget; // insertion slot in workingItems/current menu order
} SettingsState;

enum {
    PAGE_GENERAL = 0,
    PAGE_PLACEMENT,
    PAGE_MENU,
    PAGE_ICONS,
    PAGE_SORTING,
    PAGE_CONTROLS,
    PAGE_ADVANCED,
    PAGE_COUNT
};

// Prototype so early helpers can reference selection utility without ordering issues
static int icons_get_selected_index(HWND lv);
// Forward declarations for helpers referenced before definition
static void refresh_lists(SettingsState* st);
static void working_clone(SettingsState* st);
static void working_commit(SettingsState* st);
static BOOL Icons_Save(HWND pg, Config* c);
static void Sorting_Load(HWND pg, Config* c);
static BOOL Sorting_Save(HWND pg, Config* c);
static void Sorting_SyncCombo(SettingsState* st, HWND sourceCombo, int id);
static void Controls_Load(HWND pg, Config* c);
static BOOL Controls_Save(HWND pg, Config* c);
static void relayout_page(HWND page, int w, int h);
static void show_page(SettingsState* st,int idx);
static void update_tab_item_widths(SettingsState* st);
static void update_apply_button_state(SettingsState* st, HWND dlg);
static void menu_update_buttons(SettingsState* st, HWND page);

static BOOL settings_get_config_stamp(const WCHAR* path, FILETIME* writeTime, ULONGLONG* size) {
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!path || !path[0] || !GetFileAttributesExW(path, GetFileExInfoStandard, &data)) return FALSE;
    if (writeTime) *writeTime = data.ftLastWriteTime;
    if (size) *size = (((ULONGLONG)data.nFileSizeHigh) << 32) | data.nFileSizeLow;
    return TRUE;
}

static BOOL settings_config_stamp_matches(SettingsState* st) {
    FILETIME currentWrite;
    ULONGLONG currentSize;
    if (!st || !st->cfg || !settings_get_config_stamp(st->cfg->iniPath, &currentWrite, &currentSize)) return TRUE;
    return CompareFileTime(&currentWrite, &st->loadedConfigWriteTime) == 0 &&
           currentSize == st->loadedConfigSize;
}

static BOOL settings_is_button_type(HWND hwnd, UINT type){
    if(!hwnd) return FALSE;
    WCHAR className[16] = {0};
    if(!GetClassNameW(hwnd, className, ARRAYSIZE(className))) return FALSE;
    if(lstrcmpiW(className, L"Button") != 0) return FALSE;
    LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    return ((UINT)(style & BS_TYPEMASK)) == type;
}

static void settings_redraw_checkbox(HWND hwnd){
    if(!settings_is_button_type(hwnd, BS_AUTOCHECKBOX)) return;
    RedrawWindow(hwnd, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_UPDATENOW);
}
// Generic child page dialog procedure: forward button commands to main dialog
static INT_PTR CALLBACK PageDlgProc(HWND dlg, UINT msg, WPARAM wParam, LPARAM lParam){
    // All custom groupbox drawing and coloring reverted
    switch(msg){
    case WM_NOTIFY: {
        // Forward notifications (e.g., list view selection changes) to main dialog
        HWND parent = GetParent(dlg);
        if(parent){
            SendMessageW(parent, WM_NOTIFY, wParam, lParam);
            return TRUE; // handled (parent will process)
        }
        break; }
    case WM_COMMAND: {
        HWND parent = GetParent(dlg);
        if(parent){
            // Forward to main settings dialog to handle.
            SendMessageW(parent, WM_COMMAND, wParam, lParam);
            return TRUE;
        }
        break; }
    }
    return FALSE;
}

static void set_check(HWND d,int id,BOOL v){ CheckDlgButton(d,id,v?BST_CHECKED:BST_UNCHECKED); }
static BOOL get_check(HWND d,int id){ return IsDlgButtonChecked(d,id)==BST_CHECKED; }
static void set_int(HWND d,int id,int v){ WCHAR b[32]; StringCchPrintfW(b, ARRAYSIZE(b),L"%d",v); SetDlgItemTextW(d,id,b);}
static int  get_int(HWND d,int id,int def){
    WCHAR b[32];
    if(!GetDlgItemTextW(d,id,b,ARRAYSIZE(b))) return def;
    StrTrimW(b, L" \t\r\n");
    if(!b[0]) return def;
    return _wtoi(b);
}

static int show_icons_combo_index_from_value(int showIcons){
    if(showIcons == 1) return 0;
    if(showIcons == 2) return 1;
    if(showIcons == 3) return 2;
    return 3;
}

static int show_icons_value_from_combo_index(int sel){
    if(sel == 0) return 1;
    if(sel == 1) return 2;
    if(sel == 2) return 3;
    return 0;
}

static const WCHAR* show_icons_ini_value(int showIcons){
    if(showIcons == 1) return L"all";
    if(showIcons == 2) return L"files";
    if(showIcons == 3) return L"folders";
    return L"false";
}

static void update_show_menu_icon_dependencies(HWND pg){
    HWND hShowIcons = GetDlgItem(pg, IDC_SHOWICONS);
    if (!hShowIcons) return;
    int sel = (int)SendMessageW(hShowIcons, CB_GETCURSEL, 0, 0);
    int showIcons = show_icons_value_from_combo_index(sel);
    BOOL enabled = (showIcons != 0);
    HWND hRoot = GetDlgItem(pg, IDC_HIDEICONSINROOT);
    EnableWindow(hRoot, enabled);
}

static void General_Capture(HWND pg, Config* c){
    if(!pg || !c) return;
    BOOL b;
    int v;
    HWND hRenderer = GetDlgItem(pg, IDC_RENDERER_COMBO);
    int originalRenderer = hRenderer ? (int)GetWindowLongPtrW(hRenderer, GWLP_USERDATA) : 0;
    v = (int)SendDlgItemMessageW(pg, IDC_RENDERER_COMBO, CB_GETCURSEL, 0, 0);
    if(v < 0 || v > 1) v = c->useWinUI3Menu ? 1 : 0;
    if(!(originalRenderer == 2 && v == 0)) {
        c->useWinUI3Menu = (v == 1);
        c->rootMenuLargeIcons = FALSE;
    }

    c->showOnLaunch = get_check(pg, IDC_SHOWONLAUNCH);
    c->showTrayIcon = get_check(pg, IDC_SHOWTRAYICON);
    c->startOnLogin = get_check(pg, IDC_STARTONLOGIN);
    v = (int)SendDlgItemMessageW(pg, IDC_SHOWICONS, CB_GETCURSEL, 0, 0);
    if(v >= 0) c->showIcons = show_icons_value_from_combo_index(v);
    v = get_int(pg, IDC_FOLDERDEPTH_EDIT, c->folderMaxDepth);
    if(v < 0) v = 0;
    if(v > 4) v = 4;
    c->folderMaxDepth = v;
    c->hideIconsInRootMenu = get_check(pg, IDC_HIDEICONSINROOT);
    c->showExtensions = get_check(pg, IDC_SHOWEXTENSIONS);
    c->recentShowExtensions = c->showExtensions;
    c->showHidden = get_check(pg, IDC_SHOWHIDDEN);
    c->showDotfiles = get_check(pg, IDC_SHOWDOTFILES);
    c->monochromeTrayIcon = get_check(pg, IDC_MONOTRAYICON);
    c->showThumbnails = get_check(pg, IDC_WINUI_ALWAYS_SHOW_ICONS);
    b = get_check(pg, IDC_WINUI_SIZE_COMBO);
    lstrcpynW(c->winuiSize, b ? L"large" : L"default", ARRAYSIZE(c->winuiSize));
}

static void Placement_Capture(HWND pg, Config* c){
    if(!pg || !c) return;
    c->pointerRelative = get_check(pg, IDC_POINTERRELATIVE);
    int v = (int)SendDlgItemMessageW(pg, IDC_HPLACEMENT_COMBO, CB_GETCURSEL, 0, 0);
    if(v >= 0) c->hPlacement = v;
    v = (int)SendDlgItemMessageW(pg, IDC_VPLACEMENT_COMBO, CB_GETCURSEL, 0, 0);
    if(v >= 0) c->vPlacement = v;
    c->hOffset = get_int(pg, IDC_HOFFSET_EDIT, c->hOffset);
    c->vOffset = get_int(pg, IDC_VOFFSET_EDIT, c->vOffset);

    HWND hCentered = GetDlgItem(pg, IDC_IGNORE_CENTERED_COMBO);
    int centeredSel = (int)SendMessageW(hCentered, CB_GETCURSEL, 0, 0);
    c->ignoreHOffsetWhenCentered = FALSE;
    c->ignoreVOffsetWhenCentered = FALSE;
    if(centeredSel == 0) {
        c->ignoreHOffsetWhenCentered = TRUE;
        c->ignoreVOffsetWhenCentered = TRUE;
    } else if(centeredSel == 2) {
        c->ignoreHOffsetWhenCentered = TRUE;
    } else if(centeredSel == 3) {
        c->ignoreVOffsetWhenCentered = TRUE;
    }

    HWND hRelative = GetDlgItem(pg, IDC_IGNORE_RELATIVE_COMBO);
    int relativeSel = (int)SendMessageW(hRelative, CB_GETCURSEL, 0, 0);
    c->ignoreHOffsetWhenRelative = FALSE;
    c->ignoreVOffsetWhenRelative = FALSE;
    if(relativeSel == 0) {
        c->ignoreHOffsetWhenRelative = TRUE;
        c->ignoreVOffsetWhenRelative = TRUE;
    } else if(relativeSel == 2) {
        c->ignoreHOffsetWhenRelative = TRUE;
    } else if(relativeSel == 3) {
        c->ignoreVOffsetWhenRelative = TRUE;
    }
}

static void Sorting_Capture(HWND pg, Config* c){
    if(!pg || !c) return;
    HWND hSortField = GetDlgItem(pg, IDC_SORT_FIELD);
    int v = (int)SendMessageW(hSortField, CB_GETCURSEL, 0, 0);
    if(v >= 0 && v <= 4) c->sortField = v;

    HWND hSortDir = GetDlgItem(pg, IDC_SORT_DIRECTION);
    int dir = (int)SendMessageW(hSortDir, CB_GETCURSEL, 0, 0);
    c->sortDescending = (dir == 1);

    HWND hSortObjType = GetDlgItem(pg, IDC_SORT_FOLDERSFIRST);
    int objType = (int)SendMessageW(hSortObjType, CB_GETCURSEL, 0, 0);
    if(objType < 0 || objType > 2) objType = 0;
    c->sortObjectTypePriority = objType;
}

static void Controls_Capture(HWND pg, Config* c){
    if(!pg || !c) return;
    c->leftClickTrigger = get_check(pg, IDC_CTRL_LEFT_CLICK);
    c->rightClickTrigger = get_check(pg, IDC_CTRL_RIGHT_CLICK);
    c->middleClickTrigger = get_check(pg, IDC_CTRL_MIDDLE_CLICK);
    c->shiftLeftClickTrigger = get_check(pg, IDC_CTRL_SHIFT_LEFT_CLICK);
    c->shiftRightClickTrigger = get_check(pg, IDC_CTRL_SHIFT_RIGHT_CLICK);
    c->shiftMiddleClickTrigger = get_check(pg, IDC_CTRL_SHIFT_MIDDLE_CLICK);
    c->windowsKeyTrigger = get_check(pg, IDC_CTRL_WINDOWS_KEY);
    c->shiftWindowsKeyTrigger = get_check(pg, IDC_CTRL_SHIFT_WINDOWS_KEY);
    c->windowsKeyXTrigger = get_check(pg, IDC_CTRL_WINDOWS_KEY_X);
    c->legacyStartButtonDetection = get_check(pg, IDC_CTRL_LEGACY_START_DETECTION);
    {
        HWND combo = GetDlgItem(pg, IDC_CTRL_SETTINGS_ITEM_COMBO);
        int mode = combo ? (int)SendMessageW(combo, CB_GETCURSEL, 0, 0) : SETTINGS_ITEM_DISABLED;
        if(mode < SETTINGS_ITEM_DISABLED || mode > SETTINGS_ITEM_ALWAYS) mode = SETTINGS_ITEM_DISABLED;
        c->showSettingsItem = (SettingsItemMode)mode;
    }
    c->ignoreTriggersWhenFullscreen = get_check(pg, IDC_IGNORE_FULLSCREEN_CHECK);
    GetDlgItemTextW(pg, IDC_FULLSCREEN_EXCLUSION_EDIT, c->fullscreenExclusionList, ARRAYSIZE(c->fullscreenExclusionList));
}

static void Advanced_Capture(HWND pg, Config* c){
    if(!pg || !c) return;
    c->recentShowCleanItems = get_check(pg, IDC_RECENT_SHOW_CLEAN);
    c->separateItems = get_check(pg, IDC_RECENT_SEPARATE_ITEMS);
    c->recentHideProtocolShortcuts = get_check(pg, IDC_RECENT_HIDE_PROTOCOL_SHORTCUTS);
    c->thisPCAsSubmenu = get_check(pg, IDC_THISPC_AS_SUBMENU);
    c->thisPCItemsAsSubmenus = get_check(pg, IDC_THISPC_ITEMS_AS_SUBMENUS);
    c->homeAsSubmenu = get_check(pg, IDC_HOME_AS_SUBMENU);
    c->homeItemsAsSubmenus = get_check(pg, IDC_HOME_ITEMS_AS_SUBMENUS);
    c->taskKillAllDesktops = get_check(pg, IDC_TASKKILL_ALL_DESKTOPS);
    c->taskKillIgnoreSystem = get_check(pg, IDC_TASKKILL_IGNORE_SYSTEM);
    c->taskKillListWindows = get_check(pg, IDC_TASKKILL_LIST_WINDOWS);
    c->taskKillShowProcessIds = get_check(pg, IDC_TASKKILL_SHOW_PROCESS_IDS);
    c->taskKillMax = get_int(pg, IDC_TASKKILL_MAX_COMBO, c->taskKillMax);
    GetDlgItemTextW(pg, IDC_TASKKILL_EXCLUDES, c->taskKillExcludes, ARRAYSIZE(c->taskKillExcludes));
    c->excludeHibernate = !get_check(pg, IDC_POWER_HIBERNATE);
    c->excludeLock = !get_check(pg, IDC_POWER_LOCK);
    c->excludeLogoff = !get_check(pg, IDC_POWER_LOGOFF);
    c->excludeRestart = !get_check(pg, IDC_POWER_RESTART);
    c->excludeShutdown = !get_check(pg, IDC_POWER_SHUTDOWN);
    c->excludeSleep = !get_check(pg, IDC_POWER_SLEEP);
    c->powerAskConfirmation = get_check(pg, IDC_POWER_CONFIRM);
    {
        HWND hCombo = GetDlgItem(pg, 1702);
        int mode = hCombo ? (int)SendMessageW(hCombo, CB_GETCURSEL, 0, 0) : 0;
        if(mode < 0) mode = 0;
        c->recentLabelMode = mode;
    }
    c->recentMax = get_int(pg, IDC_RECENTMAX_EDIT, c->recentMax);
    GetDlgItemTextW(pg, IDC_DEFAULT_ICON, c->defaultIconPath, ARRAYSIZE(c->defaultIconPath));
    GetDlgItemTextW(pg, IDC_DEFAULT_ICON_LIGHT, c->defaultIconPathLight, ARRAYSIZE(c->defaultIconPathLight));
    GetDlgItemTextW(pg, IDC_DEFAULT_ICON_DARK, c->defaultIconPathDark, ARRAYSIZE(c->defaultIconPathDark));
}

static BOOL settings_has_pending_changes(SettingsState* st){
    if(!st || !st->cfg) return FALSE;

    Config preview = *st->cfg;
    if(st->workingDirty) {
        if(st->workingCount < 0) st->workingCount = 0;
        if(st->workingCount > 64) st->workingCount = 64;
        preview.count = st->workingCount;
        for(int i = 0; i < st->workingCount; ++i) preview.items[i] = st->workingItems[i];
    }

    General_Capture(st->pages[PAGE_GENERAL], &preview);
    Sorting_Capture(st->pages[PAGE_GENERAL], &preview);
    Placement_Capture(st->pages[PAGE_PLACEMENT], &preview);
    Controls_Capture(st->pages[PAGE_CONTROLS], &preview);
    Advanced_Capture(st->pages[PAGE_ADVANCED], &preview);

    return memcmp(&preview, st->cfg, sizeof(Config)) != 0;
}

static void update_apply_button_state(SettingsState* st, HWND dlg){
    HWND hApply = GetDlgItem(dlg, IDC_APPLY);
    if(!hApply) return;
    EnableWindow(hApply, settings_has_pending_changes(st));
}

static BOOL settings_command_may_change_state(WORD notifyCode){
    switch(notifyCode){
        case BN_CLICKED:
        case CBN_SELCHANGE:
        case EN_CHANGE:
            return TRUE;
    }
    return FALSE;
}

static void apply_dialog_icon(HWND dlg){
    int cx=GetSystemMetrics(SM_CXICON), cy=GetSystemMetrics(SM_CYICON);
    int cxs=GetSystemMetrics(SM_CXSMICON), cys=GetSystemMetrics(SM_CYSMICON);
    HICON icoBig=(HICON)LoadImageW(GetModuleHandleW(NULL),MAKEINTRESOURCEW(IDI_APPICON),IMAGE_ICON,cx,cy,LR_DEFAULTCOLOR);
    HICON icoSmall=(HICON)LoadImageW(GetModuleHandleW(NULL),MAKEINTRESOURCEW(IDI_APPICON),IMAGE_ICON,cxs,cys,LR_DEFAULTCOLOR);
    if(!icoBig)   icoBig=LoadIconW(NULL, IDI_APPLICATION);
    if(!icoSmall) icoSmall=LoadIconW(NULL, IDI_APPLICATION);
    if(icoBig)   SendMessageW(dlg,WM_SETICON,ICON_BIG,(LPARAM)icoBig);
    if(icoSmall) SendMessageW(dlg,WM_SETICON,ICON_SMALL,(LPARAM)icoSmall);
}

static void apply_settings_title(HWND dlg, const Config* cfg) {
    static const WCHAR baseTitle[] = L"WinMac Menu Settings";
    if(!cfg || !cfg->iniPath[0]) {
        SetWindowTextW(dlg, baseTitle);
        return;
    }

    const WCHAR* slash = wcsrchr(cfg->iniPath, L'\\');
    const WCHAR* fileName = slash ? slash + 1 : cfg->iniPath;
    if(!lstrcmpiW(fileName, L"config.ini")) {
        SetWindowTextW(dlg, baseTitle);
        return;
    }

    WCHAR stem[MAX_PATH];
    lstrcpynW(stem, fileName, ARRAYSIZE(stem));
    WCHAR* dot = wcsrchr(stem, L'.');
    if(dot) *dot = 0;

    WCHAR title[MAX_PATH + 64];
    StringCchPrintfW(title, ARRAYSIZE(title), L"%s (%s)", baseTitle, stem[0] ? stem : fileName);
    SetWindowTextW(dlg, title);
}

static COLORREF settings_accent_text_color(COLORREF accent) {
    int luminance = (30 * GetRValue(accent) + 59 * GetGValue(accent) + 11 * GetBValue(accent)) / 100;
    return luminance > 140 ? RGB(32, 32, 32) : RGB(245, 245, 245);
}

static LRESULT settings_draw_list_selection(const NMLVCUSTOMDRAW* draw) {
    COLORREF accent;

    if(!draw) return CDRF_DODEFAULT;
    if(draw->nmcd.dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
    if(draw->nmcd.dwDrawStage != CDDS_ITEMPREPAINT) return CDRF_DODEFAULT;
    if(!(draw->nmcd.uItemState & CDIS_SELECTED)) return CDRF_DODEFAULT;

    if(!theme_get_accent(&accent)) accent = GetSysColor(COLOR_HIGHLIGHT);
    ((NMLVCUSTOMDRAW*)draw)->clrTextBk = accent;
    ((NMLVCUSTOMDRAW*)draw)->clrText = settings_accent_text_color(accent);
    return CDRF_NEWFONT;
}

static BOOL menu_list_cursor_hit(SettingsState* st, HWND hwnd) {
    if(!st || !st->pages[PAGE_MENU] || hwnd != GetDlgItem(st->pages[PAGE_MENU], IDC_MENU_LIST)) return FALSE;
    POINT pt;
    GetCursorPos(&pt);
    ScreenToClient(hwnd, &pt);
    LVHITTESTINFO hit;
    ZeroMemory(&hit, sizeof(hit));
    hit.pt = pt;
    return ListView_HitTest(hwnd, &hit) >= 0;
}

static int menu_list_drop_position(HWND lv, POINT pt) {
    int count = ListView_GetItemCount(lv);
    LVHITTESTINFO hit;
    if(!lv || count <= 0) return 0;

    ZeroMemory(&hit, sizeof(hit));
    hit.pt = pt;
    int row = ListView_HitTest(lv, &hit);
    if(row >= 0) {
        RECT bounds;
        if(ListView_GetItemRect(lv, row, &bounds, LVIR_BOUNDS)) {
            return (pt.y >= bounds.top + ((bounds.bottom - bounds.top) / 2)) ? row + 1 : row;
        }
        return row;
    }

    RECT first, last;
    if(ListView_GetItemRect(lv, 0, &first, LVIR_BOUNDS) && pt.y < first.top) return 0;
    if(ListView_GetItemRect(lv, count - 1, &last, LVIR_BOUNDS) && pt.y > last.bottom) return count;
    return count;
}

static void menu_list_clear_insert_mark(HWND lv) {
    LVINSERTMARK mark;
    if(!lv) return;
    ZeroMemory(&mark, sizeof(mark));
    mark.cbSize = sizeof(mark);
    mark.iItem = -1;
    ListView_SetInsertMark(lv, &mark);
}

static void menu_list_apply_insert_mark_color(HWND lv) {
    if (!lv) return;
    SendMessageW(lv, LVM_SETINSERTMARKCOLOR, 0,
                 (LPARAM)(theme_is_dark() ? RGB(235, 235, 235) : RGB(32, 32, 32)));
}

static const WCHAR MENU_DRAG_BADGE_CLASS[] = L"WinMacMenuDragBadge";

static LRESULT CALLBACK MenuDragBadgeProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch(msg) {
    case WM_SETFONT:
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)wParam);
        if(lParam) InvalidateRect(hwnd, NULL, TRUE);
        return 0;
    case WM_GETFONT:
        return GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    case WM_ERASEBKGND:
        return TRUE;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        RECT client;
        WCHAR text[384] = {0};
        HDC dc = BeginPaint(hwnd, &ps);
        HFONT font = (HFONT)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
        HFONT oldFont = font ? (HFONT)SelectObject(dc, font) : NULL;
        GetClientRect(hwnd, &client);
        FillRect(dc, &client, GetSysColorBrush(COLOR_HIGHLIGHT));
        GetWindowTextW(hwnd, text, ARRAYSIZE(text));
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(32, 32, 32));
        DrawTextW(dc, text, -1, &client, DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_END_ELLIPSIS);
        if(oldFont) SelectObject(dc, oldFont);
        EndPaint(hwnd, &ps);
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static BOOL menu_drag_register_badge_class(void) {
    static BOOL registered = FALSE;
    WNDCLASSW wc;

    if(registered) return TRUE;
    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc = MenuDragBadgeProc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = MENU_DRAG_BADGE_CLASS;
    if(!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return FALSE;
    registered = TRUE;
    return TRUE;
}

static void menu_drag_move_badge(SettingsState* st, HWND lv, POINT pt) {
    RECT badge;
    if(!st || !st->hDragBadge || !lv) return;
    GetWindowRect(st->hDragBadge, &badge);
    ClientToScreen(lv, &pt);
    SetWindowPos(st->hDragBadge, HWND_TOPMOST, pt.x + 2, pt.y - ((badge.bottom - badge.top) / 2),
                 0, 0, SWP_NOACTIVATE | SWP_NOSIZE | SWP_SHOWWINDOW);
}

static void menu_drag_show_badge(SettingsState* st, HWND lv, int row, POINT pt) {
    WCHAR label[256] = {0};
    WCHAR type[128] = {0};
    WCHAR text[384] = {0};
    SIZE textSize = {0};
    HDC dc;
    HFONT font;
    HFONT oldFont;

    if(!st || !lv) return;
    ListView_GetItemText(lv, row, 3, label, ARRAYSIZE(label));
    if(!label[0]) ListView_GetItemText(lv, row, 2, type, ARRAYSIZE(type));
    StringCchPrintfW(text, ARRAYSIZE(text), L"%d  %s", row + 1, label[0] ? label : type);

    dc = GetDC(lv);
    font = (HFONT)SendMessageW(lv, WM_GETFONT, 0, 0);
    oldFont = font ? (HFONT)SelectObject(dc, font) : NULL;
    GetTextExtentPoint32W(dc, text, lstrlenW(text), &textSize);
    if(oldFont) SelectObject(dc, oldFont);
    ReleaseDC(lv, dc);

    if(!menu_drag_register_badge_class()) return;
    st->hDragBadge = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT,
        MENU_DRAG_BADGE_CLASS, text, WS_POPUP | WS_DISABLED,
        0, 0, textSize.cx + 16, 22, GetAncestor(lv, GA_ROOT), NULL,
        GetModuleHandleW(NULL), NULL);
    if(st->hDragBadge) {
        if(font) SendMessageW(st->hDragBadge, WM_SETFONT, (WPARAM)font, TRUE);
    }
    menu_drag_move_badge(st, lv, pt);
}

static void menu_drag_update_target(SettingsState* st, HWND lv, POINT pt) {
    if(!st || !lv) return;
    int count = ListView_GetItemCount(lv);
    int drop = menu_list_drop_position(lv, pt);
    if(drop < 0) drop = 0;
    if(drop > count) drop = count;
    st->insertTarget = drop;
    menu_list_apply_insert_mark_color(lv);

    LVINSERTMARK mark;
    ZeroMemory(&mark, sizeof(mark));
    mark.cbSize = sizeof(mark);
    if(count > 0) {
        if(drop <= 0) {
            mark.iItem = 0;
            mark.dwFlags = LVIM_BEFORE;
        } else {
            mark.iItem = drop - 1;
            mark.dwFlags = LVIM_AFTER;
        }
        ListView_SetInsertMark(lv, &mark);
    } else {
        menu_list_clear_insert_mark(lv);
    }
}

static void menu_drag_autoscroll(HWND lv, POINT pt) {
    RECT client;
    RECT row;
    int offset = 0;
    int rowHeight;

    if(!lv || !GetClientRect(lv, &client) || !ListView_GetItemRect(lv, 0, &row, LVIR_BOUNDS)) return;
    rowHeight = row.bottom - row.top;
    if(rowHeight < 1) return;

    if(pt.y < client.top + 16) offset = -rowHeight;
    else if(pt.y > client.bottom - 16) offset = rowHeight;
    if(offset) ListView_Scroll(lv, 0, offset);
}

static void menu_drag_stop(SettingsState* st, HWND lv) {
    if(!st) return;
    st->dragging = FALSE;
    st->dragSource = -1;
    st->insertTarget = -1;

    if(st->hDragBadge) {
        DestroyWindow(st->hDragBadge);
        st->hDragBadge = NULL;
    }
    if(lv) {
        menu_list_clear_insert_mark(lv);
        ListView_SetItemState(lv, -1, 0, LVIS_CUT | LVIS_DROPHILITED);
        if(GetCapture() == lv) ReleaseCapture();
    }
}

static BOOL menu_drag_begin(SettingsState* st, HWND lv, int row, POINT pt) {
    if(!st || !lv || st->dragging) return FALSE;
    if(row < 0 || row >= st->workingCount || row >= ListView_GetItemCount(lv)) return FALSE;

    // The menu list is always displayed in configuration order, so rows and
    // workingItems use the same index throughout one drag operation.
    st->dragSource = row;
    st->dragging = TRUE;
    st->insertTarget = -1;

    // Keep the original row in place but dim it while the drag badge follows the pointer.
    ListView_SetItemState(lv, row, LVIS_CUT, LVIS_CUT);
    SetCapture(lv);
    menu_drag_show_badge(st, lv, row, pt);
    menu_drag_update_target(st, lv, pt);
    SetCursor(NULL);
    return TRUE;
}

static int menu_drag_commit(SettingsState* st) {
    if(!st || st->dragSource < 0 || st->dragSource >= st->workingCount) return -1;
    int target = st->insertTarget;
    if(target < 0) return -1;
    if(st->dragSource < target) target--;
    if(target < 0) target = 0;
    if(target >= st->workingCount) target = st->workingCount - 1;
    if(target == st->dragSource) return -1;

    ConfigItem moved = st->workingItems[st->dragSource];
    if(st->dragSource < target) {
        for(int i = st->dragSource; i < target; ++i) st->workingItems[i] = st->workingItems[i + 1];
    } else {
        for(int i = st->dragSource; i > target; --i) st->workingItems[i] = st->workingItems[i - 1];
    }
    st->workingItems[target] = moved;
    st->workingDirty = TRUE;
    return target;
}

static LRESULT CALLBACK MenuListSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR idSubclass, DWORD_PTR refData) {
    SettingsState* st = (SettingsState*)refData;
    switch(msg) {
    case WM_SETCURSOR:
        if(st && st->dragging) {
            SetCursor(NULL);
            return TRUE;
        }
        if(menu_list_cursor_hit(st, hwnd)) {
            SetCursor(LoadCursorW(NULL, IDC_HAND));
            return TRUE;
        }
        break;
    case WM_MOUSEMOVE: {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        if(st && st->dragging) {
            menu_drag_move_badge(st, hwnd, pt);
            menu_drag_autoscroll(hwnd, pt);
            menu_drag_update_target(st, hwnd, pt);
            SetCursor(NULL);
            return 0;
        }
        break;
    }
    case WM_LBUTTONUP:
        if(st && st->dragging) {
            int destination = menu_drag_commit(st);
            menu_drag_stop(st, hwnd);
            SetCursor(LoadCursorW(NULL, IDC_ARROW));
            if(destination >= 0) {
                refresh_lists(st);
                ListView_SetItemState(hwnd, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
                ListView_SetItemState(hwnd, destination, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
                ListView_EnsureVisible(hwnd, destination, FALSE);
                menu_update_buttons(st, GetParent(hwnd));
                update_apply_button_state(st, GetParent(GetParent(hwnd)));
            }
            return 0;
        }
        break;
    case WM_CAPTURECHANGED:
    case WM_CANCELMODE:
        if(st && st->dragging) menu_drag_stop(st, hwnd);
        break;
    case WM_KEYDOWN:
        if(st && st->dragging && wParam == VK_ESCAPE) {
            menu_drag_stop(st, hwnd);
            SetCursor(LoadCursorW(NULL, IDC_ARROW));
            return 0;
        }
        break;
    case WM_NCDESTROY:
        if(st && st->dragging) menu_drag_stop(st, hwnd);
        RemoveWindowSubclass(hwnd, MenuListSubclassProc, idSubclass);
        break;
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

// -------- General Page --------
static void General_Load(HWND pg, Config* c){
    HWND hRenderer = GetDlgItem(pg, IDC_RENDERER_COMBO);
    if (hRenderer) {
        SendMessageW(hRenderer, CB_RESETCONTENT, 0, 0);
        SendMessageW(hRenderer, CB_ADDSTRING, 0, (LPARAM)L"Windows 10");
        SendMessageW(hRenderer, CB_ADDSTRING, 0, (LPARAM)L"Windows 11");
        SendMessageW(hRenderer, CB_SETCURSEL, c->useWinUI3Menu ? 1 : 0, 0);
        SetWindowLongPtrW(hRenderer, GWLP_USERDATA, (LONG_PTR)(c->useWinUI3Menu ? 1 : (c->rootMenuLargeIcons ? 2 : 0)));
    }
    HWND hShowIcons = GetDlgItem(pg, IDC_SHOWICONS);
    SendMessageW(hShowIcons, CB_RESETCONTENT, 0, 0);
    SendMessageW(hShowIcons, CB_ADDSTRING, 0, (LPARAM)L"Enable");
    SendMessageW(hShowIcons, CB_ADDSTRING, 0, (LPARAM)L"Files only");
    SendMessageW(hShowIcons, CB_ADDSTRING, 0, (LPARAM)L"Folders only");
    SendMessageW(hShowIcons, CB_ADDSTRING, 0, (LPARAM)L"Disable");
    SendMessageW(hShowIcons, CB_SETCURSEL, show_icons_combo_index_from_value(c->showIcons), 0);
    {
        int depth = c->folderMaxDepth;
        if(depth < 0) depth = 0;
        if(depth > 4) depth = 4;
        HWND hDepthEdit = GetDlgItem(pg, IDC_FOLDERDEPTH_EDIT);
        HWND hDepthSpin = GetDlgItem(pg, IDC_FOLDERDEPTH_SPIN);
        if(hDepthEdit) set_int(pg, IDC_FOLDERDEPTH_EDIT, depth);
        if(hDepthSpin) {
            SendMessageW(hDepthSpin, UDM_SETBUDDY, (WPARAM)hDepthEdit, 0);
            SendMessageW(hDepthSpin, UDM_SETRANGE32, 0, 4);
            SendMessageW(hDepthSpin, UDM_SETPOS32, 0, depth);
        }
    }
    set_check(pg,IDC_SHOWONLAUNCH,c->showOnLaunch);
    set_check(pg,IDC_SHOWTRAYICON,c->showTrayIcon);
    set_check(pg,IDC_STARTONLOGIN,c->startOnLogin);
    set_check(pg,IDC_HIDEICONSINROOT,c->hideIconsInRootMenu);
    set_check(pg,IDC_SHOWEXTENSIONS,c->showExtensions);
    set_check(pg,IDC_SHOWHIDDEN,c->showHidden);
    set_check(pg,IDC_SHOWDOTFILES,c->showDotfiles);
    set_check(pg,IDC_MONOTRAYICON,c->monochromeTrayIcon);
    set_check(pg,IDC_WINUI_ALWAYS_SHOW_ICONS,c->showThumbnails);
    set_check(pg,IDC_WINUI_SIZE_COMBO,!lstrcmpiW(c->winuiSize, L"large"));
    {
        int renderer = hRenderer ? (int)SendMessageW(hRenderer, CB_GETCURSEL, 0, 0) : 0;
        BOOL winuiEnabled = (renderer == 1);
        EnableWindow(GetDlgItem(pg, IDC_WINUI_SIZE_COMBO), winuiEnabled);
        EnableWindow(GetDlgItem(pg, IDC_WINUI_ALWAYS_SHOW_ICONS), winuiEnabled);
    }
    update_show_menu_icon_dependencies(pg);
    // Populate filename label (separate static now). Button text remains static in resource.
    HWND hLabel = GetDlgItem(pg, IDC_CONFIG_FILE_LABEL);
    if(hLabel){
        // Apply italic font (create once and reuse)
        SettingsState* st=(SettingsState*)GetWindowLongPtrW(GetParent(pg),GWLP_USERDATA);
        if(st && !st->hItalic){
            LOGFONTW lf; ZeroMemory(&lf,sizeof(lf));
            // Base on dialog font metrics
            HFONT hDlgFont=(HFONT)SendMessageW(pg,WM_GETFONT,0,0);
            if(hDlgFont){ GetObjectW(hDlgFont,sizeof(lf),&lf); }
            if(lf.lfFaceName[0]==0){ lstrcpynW(lf.lfFaceName,L"Segoe UI",ARRAYSIZE(lf.lfFaceName)); lf.lfHeight=-12; }
            lf.lfItalic = TRUE;
            st->hItalic = CreateFontIndirectW(&lf);
        }
        if(st && st->hItalic){ SendMessageW(hLabel,WM_SETFONT,(WPARAM)st->hItalic,TRUE); }
        if(c && c->iniPath[0]){
            const WCHAR* slash = wcsrchr(c->iniPath, L'\\');
            const WCHAR* base = slash? slash+1 : c->iniPath;
            SetWindowTextW(hLabel, base);
        } else {
            SetWindowTextW(hLabel, L"(none)");
        }
    }
    // Ensure button retains the intended static text (defensive in case of legacy configs)
    SetDlgItemTextW(pg, IDC_OPEN_CONFIG_FOLDER, L"Show config folder");
}
static BOOL General_Save(HWND pg, Config* c){
    BOOL ch=FALSE;
    BOOL reloadNeeded=FALSE;
    BOOL b;
    int v;
    BOOL origUseWinUI3Menu = c->useWinUI3Menu;
    BOOL origRootMenuLargeIcons = c->rootMenuLargeIcons;
    BOOL rendererChanged = FALSE;
    HWND hRenderer = GetDlgItem(pg, IDC_RENDERER_COMBO);
    int originalRenderer = hRenderer ? (int)GetWindowLongPtrW(hRenderer, GWLP_USERDATA) : 0;
    v=(int)SendDlgItemMessageW(pg,IDC_RENDERER_COMBO,CB_GETCURSEL,0,0);
    if(v<0 || v>1) v = c->useWinUI3Menu ? 1 : 0;
    BOOL preserveIniLargeRenderer = (originalRenderer == 2 && v == 0);
    if(c->iniPath[0] && !preserveIniLargeRenderer) {
        WCHAR persisted[32];
        GetPrivateProfileStringW(L"Appearance", L"ThemeStyle", L"", persisted, ARRAYSIZE(persisted), c->iniPath);
        if (!persisted[0]) {
            GetPrivateProfileStringW(L"Appearance", L"Style", L"", persisted, ARRAYSIZE(persisted), c->iniPath);
        }
        if (persisted[0]) {
            int persistedRenderer = is_windows_11_or_greater() ? 1 : 0;
            if (!lstrcmpiW(persisted, L"0")) persistedRenderer = 0;
            else if (!lstrcmpiW(persisted, L"1")) persistedRenderer = 1;
            rendererChanged = (persistedRenderer != v);
        }
    }
    if(!preserveIniLargeRenderer) {
        BOOL useWinUI = (v == 1);
        if(c->useWinUI3Menu != useWinUI){ c->useWinUI3Menu = useWinUI; ch=TRUE; }
        if(c->rootMenuLargeIcons != FALSE){ c->rootMenuLargeIcons = FALSE; ch=TRUE; }
    }

    b=get_check(pg,IDC_SHOWONLAUNCH);               if(c->showOnLaunch!=b){c->showOnLaunch=b;ch=TRUE;}
    b=get_check(pg,IDC_SHOWTRAYICON);               if(c->showTrayIcon!=b){c->showTrayIcon=b;ch=TRUE;reloadNeeded=TRUE;}
    b=get_check(pg,IDC_STARTONLOGIN);               if(c->startOnLogin!=b){c->startOnLogin=b;ch=TRUE;}
    v=(int)SendDlgItemMessageW(pg,IDC_SHOWICONS,CB_GETCURSEL,0,0); if(v>=0){ int showIcons=show_icons_value_from_combo_index(v); if(c->showIcons!=showIcons){c->showIcons=showIcons;ch=TRUE;} }
    v=get_int(pg,IDC_FOLDERDEPTH_EDIT,c->folderMaxDepth); if(v<0)v=0; if(v>4)v=4; if(c->folderMaxDepth!=v){c->folderMaxDepth=v;ch=TRUE;}
    b=get_check(pg,IDC_HIDEICONSINROOT); if(c->hideIconsInRootMenu!=b){c->hideIconsInRootMenu=b;ch=TRUE;}
    b=get_check(pg,IDC_SHOWEXTENSIONS);             if(c->showExtensions!=b){c->showExtensions=b;c->recentShowExtensions=b;ch=TRUE;}
    b=get_check(pg,IDC_SHOWHIDDEN);                 if(c->showHidden!=b){c->showHidden=b;ch=TRUE;}
    b=get_check(pg,IDC_SHOWDOTFILES);               if(c->showDotfiles!=b){c->showDotfiles=b;ch=TRUE;}
    b=get_check(pg,IDC_MONOTRAYICON);               if(c->monochromeTrayIcon!=b){c->monochromeTrayIcon=b;ch=TRUE;reloadNeeded=TRUE;}
    b=get_check(pg,IDC_WINUI_ALWAYS_SHOW_ICONS);     if(c->showThumbnails!=b){c->showThumbnails=b;ch=TRUE;}
    {
        const WCHAR* sizeValue = get_check(pg, IDC_WINUI_SIZE_COMBO) ? L"large" : L"default";
        if(lstrcmpiW(c->winuiSize, sizeValue)){
            lstrcpynW(c->winuiSize, sizeValue, ARRAYSIZE(c->winuiSize));
            ch=TRUE;
        }
    }

    if(c->iniPath[0]){
        WCHAR bufDepth[8];
        StringCchPrintfW(bufDepth, ARRAYSIZE(bufDepth), L"%d", c->folderMaxDepth);
        settings_write(L"General", L"FolderSubmenuDepth", bufDepth, c->iniPath);
        settings_write(L"General",L"RunInBackground",   c->runInBackground?L"true":L"false",c->iniPath);
        settings_write(L"General",L"ShowOnLaunch",      c->showOnLaunch?L"true":L"false",c->iniPath);
        settings_write(L"General",L"StartOnLogin",      c->startOnLogin?L"true":L"false",c->iniPath);
        settings_write(L"General",L"ShowTrayIcon",      c->showTrayIcon?L"true":L"false",c->iniPath);
        settings_write(L"General",L"MonochromeTrayIcon",c->monochromeTrayIcon?L"true":L"false",c->iniPath);
        settings_write(L"General",L"ShowIcons",         show_icons_ini_value(c->showIcons),c->iniPath);
        settings_write(L"General",L"HideIconsInRootMenu",c->hideIconsInRootMenu?L"true":L"false",c->iniPath);
        settings_write(L"General",L"KeepMenuOpenAfterContextAction", c->keepMenuOpenAfterContextAction?L"true":L"false",c->iniPath);
        settings_write(L"General",L"ShowHidden",        c->showHidden?L"true":L"false",c->iniPath);
        settings_write(L"General",L"ShowDotfiles",      c->showDotfiles?L"true":L"false",c->iniPath);
        settings_write(L"General",L"ShowFileExtensions",c->showExtensions?L"true":L"false",c->iniPath);
        settings_write(L"Appearance",L"TouchMode",      !lstrcmpiW(c->winuiSize, L"large")?L"true":L"false",c->iniPath);
        settings_write(L"Appearance",L"ShowTrayIcon",   NULL,c->iniPath);
        settings_write(L"Appearance",L"MonochromeTrayIcon",NULL,c->iniPath);
        settings_write(L"Appearance",L"ShowThumbnails", c->showThumbnails?L"true":L"false",c->iniPath);
        settings_write(L"Appearance",L"WinUISize",      NULL,c->iniPath);
        settings_write(L"Appearance",L"WinUIAlwaysShowIcons",NULL,c->iniPath);
        settings_write(L"Appearance",L"AlwaysShowIcons", NULL,c->iniPath);
        settings_write(L"WinUI3",L"Size",              NULL,c->iniPath);
        if(!preserveIniLargeRenderer) {
            settings_write(L"Appearance",L"ThemeStyle",     c->useWinUI3Menu?L"1":L"0",c->iniPath);
            settings_write(L"Appearance",L"Style",          NULL,c->iniPath);
            settings_write(L"Appearance",L"Renderer",       NULL,c->iniPath);
            settings_write(L"General",L"MenuStyle",         NULL,c->iniPath);
            settings_write(L"Appearance",L"LargeMenuIcons", NULL,c->iniPath);
            settings_write(L"Advanced",L"LargeMenuIcons",   NULL,c->iniPath);
            settings_write(L"Appearance",L"UseWinUI3Menu",  NULL,c->iniPath);
        }
    }

    // Do not trigger reload here; let caller decide based on button pressed
    return reloadNeeded ? 2 : ch ? 1 : 0;
}

static void apply_saved_runtime_settings(const Config* before, Config* c) {
    WCHAR exe[MAX_PATH];
    GetModuleFileNameW(NULL, exe, ARRAYSIZE(exe));
    WCHAR cmd[2048];
    if(c->iniPath[0]) {
        WCHAR cfgAbs[MAX_PATH];
        DWORD n = GetFullPathNameW(c->iniPath, ARRAYSIZE(cfgAbs), cfgAbs, NULL);
        if (n == 0 || n >= ARRAYSIZE(cfgAbs)) lstrcpynW(cfgAbs, c->iniPath, ARRAYSIZE(cfgAbs));
        StringCchPrintfW(cmd, ARRAYSIZE(cmd), L"\"%s\" --config \"%s\"", exe, cfgAbs);
    } else {
        StringCchPrintfW(cmd, ARRAYSIZE(cmd), L"\"%s\"", exe);
    }
    WCHAR runName[32];
    lstrcpynW(runName, L"WinMac Menu", ARRAYSIZE(runName));
    if(c->startOnLogin)
        set_run_at_login(runName, cmd);
    else
        remove_run_at_login(runName);

    if (before->useWinUI3Menu != c->useWinUI3Menu ||
        before->rootMenuLargeIcons != c->rootMenuLargeIcons) {
        ShutdownWinUI3Menu();
        if (c->useWinUI3Menu) {
            PreloadWinUI3Menu(c);
        }
    }

}

// -------- Placement Page --------
static void Placement_Load(HWND pg, Config* c){
    set_check(pg,IDC_POINTERRELATIVE,c->pointerRelative);
    HWND hH=GetDlgItem(pg,IDC_HPLACEMENT_COMBO), hV=GetDlgItem(pg,IDC_VPLACEMENT_COMBO);
    const WCHAR* Hs[]={L"Left",L"Center",L"Right"};
    const WCHAR* Vs[]={L"Top",L"Center",L"Bottom"};
    for(int i=0;i<3;i++){ SendMessageW(hH,CB_ADDSTRING,0,(LPARAM)Hs[i]); SendMessageW(hV,CB_ADDSTRING,0,(LPARAM)Vs[i]); }
    SendMessageW(hH,CB_SETCURSEL,c->hPlacement,0);
    SendMessageW(hV,CB_SETCURSEL,c->vPlacement,0);
    if(c->hOffset < 0) c->hOffset = 0;
    if(c->vOffset < 0) c->vOffset = 0;
    set_int(pg,IDC_HOFFSET_EDIT,c->hOffset); set_int(pg,IDC_VOFFSET_EDIT,c->vOffset);
    {
        HWND hHOffsetEdit = GetDlgItem(pg, IDC_HOFFSET_EDIT);
        HWND hVOffsetEdit = GetDlgItem(pg, IDC_VOFFSET_EDIT);
        HWND hHOffsetSpin = GetDlgItem(pg, IDC_HOFFSET_SPIN);
        HWND hVOffsetSpin = GetDlgItem(pg, IDC_VOFFSET_SPIN);
        if(hHOffsetSpin) {
            SendMessageW(hHOffsetSpin, UDM_SETBUDDY, (WPARAM)hHOffsetEdit, 0);
            SendMessageW(hHOffsetSpin, UDM_SETRANGE32, 0, INT_MAX);
            SendMessageW(hHOffsetSpin, UDM_SETPOS32, 0, c->hOffset);
        }
        if(hVOffsetSpin) {
            SendMessageW(hVOffsetSpin, UDM_SETBUDDY, (WPARAM)hVOffsetEdit, 0);
            SendMessageW(hVOffsetSpin, UDM_SETRANGE32, 0, INT_MAX);
            SendMessageW(hVOffsetSpin, UDM_SETPOS32, 0, c->vOffset);
        }
    }
    // Populate IgnoreOffsetWhenCentered combobox
    HWND hCentered = GetDlgItem(pg, IDC_IGNORE_CENTERED_COMBO);
    SendMessageW(hCentered, CB_RESETCONTENT, 0, 0);
    SendMessageW(hCentered, CB_ADDSTRING, 0, (LPARAM)L"True");      // 0
    SendMessageW(hCentered, CB_ADDSTRING, 0, (LPARAM)L"False");     // 1
    SendMessageW(hCentered, CB_ADDSTRING, 0, (LPARAM)L"Horizontal only"); // 2
    SendMessageW(hCentered, CB_ADDSTRING, 0, (LPARAM)L"Vertical only");   // 3
    int centeredSel = 1;
    if (c->ignoreHOffsetWhenCentered && c->ignoreVOffsetWhenCentered) centeredSel = 0;
    else if (c->ignoreHOffsetWhenCentered) centeredSel = 2;
    else if (c->ignoreVOffsetWhenCentered) centeredSel = 3;
    SendMessageW(hCentered, CB_SETCURSEL, centeredSel, 0);
    // Populate IgnoreOffsetWhenRelative combobox
    HWND hRelative = GetDlgItem(pg, IDC_IGNORE_RELATIVE_COMBO);
    SendMessageW(hRelative, CB_RESETCONTENT, 0, 0);
    SendMessageW(hRelative, CB_ADDSTRING, 0, (LPARAM)L"True");      // 0
    SendMessageW(hRelative, CB_ADDSTRING, 0, (LPARAM)L"False");     // 1
    SendMessageW(hRelative, CB_ADDSTRING, 0, (LPARAM)L"Horizontal only"); // 2
    SendMessageW(hRelative, CB_ADDSTRING, 0, (LPARAM)L"Vertical only");   // 3
    int relativeSel = 1;
    if (c->ignoreHOffsetWhenRelative && c->ignoreVOffsetWhenRelative) relativeSel = 0;
    else if (c->ignoreHOffsetWhenRelative) relativeSel = 2;
    else if (c->ignoreVOffsetWhenRelative) relativeSel = 3;
    SendMessageW(hRelative, CB_SETCURSEL, relativeSel, 0);
    // Enable/disable relative combobox based on pointer relative
    EnableWindow(hRelative, c->pointerRelative);
    {
        HWND hRelativeLabel = GetDlgItem(pg, IDC_IGNORE_RELATIVE_LABEL);
        if (hRelativeLabel) EnableWindow(hRelativeLabel, c->pointerRelative);
    }

}
static BOOL Placement_Save(HWND pg, Config* c){
    BOOL ch=FALSE; BOOL b;
    b=get_check(pg,IDC_POINTERRELATIVE); if(c->pointerRelative!=b){c->pointerRelative=b;ch=TRUE;}
    int v;
    v=(int)SendDlgItemMessageW(pg,IDC_HPLACEMENT_COMBO,CB_GETCURSEL,0,0); if(v>=0 && v!=c->hPlacement){c->hPlacement=v;ch=TRUE;}
    v=(int)SendDlgItemMessageW(pg,IDC_VPLACEMENT_COMBO,CB_GETCURSEL,0,0); if(v>=0 && v!=c->vPlacement){c->vPlacement=v;ch=TRUE;}
    v=get_int(pg,IDC_HOFFSET_EDIT,c->hOffset); if(v<0) v=0; if(v!=c->hOffset){c->hOffset=v;ch=TRUE;}
    v=get_int(pg,IDC_VOFFSET_EDIT,c->vOffset); if(v<0) v=0; if(v!=c->vOffset){c->vOffset=v;ch=TRUE;}
    // Save IgnoreOffsetWhenCentered from combobox
    HWND hCentered = GetDlgItem(pg, IDC_IGNORE_CENTERED_COMBO);
    int centeredSel = (int)SendMessageW(hCentered, CB_GETCURSEL, 0, 0);
    BOOL oldIgnoreHCentered = c->ignoreHOffsetWhenCentered;
    BOOL oldIgnoreVCentered = c->ignoreVOffsetWhenCentered;
    c->ignoreHOffsetWhenCentered = c->ignoreVOffsetWhenCentered = FALSE;
    if(centeredSel == 0) { c->ignoreHOffsetWhenCentered = TRUE; c->ignoreVOffsetWhenCentered = TRUE; }
    else if(centeredSel == 2) { c->ignoreHOffsetWhenCentered = TRUE; }
    else if(centeredSel == 3) { c->ignoreVOffsetWhenCentered = TRUE; }
    // Save IgnoreOffsetWhenRelative from combobox
    HWND hRelative = GetDlgItem(pg, IDC_IGNORE_RELATIVE_COMBO);
    int relativeSel = (int)SendMessageW(hRelative, CB_GETCURSEL, 0, 0);
    BOOL oldIgnoreHRelative = c->ignoreHOffsetWhenRelative;
    BOOL oldIgnoreVRelative = c->ignoreVOffsetWhenRelative;
    c->ignoreHOffsetWhenRelative = c->ignoreVOffsetWhenRelative = FALSE;
    if(relativeSel == 0) { c->ignoreHOffsetWhenRelative = TRUE; c->ignoreVOffsetWhenRelative = TRUE; }
    else if(relativeSel == 2) { c->ignoreHOffsetWhenRelative = TRUE; }
    else if(relativeSel == 3) { c->ignoreVOffsetWhenRelative = TRUE; }
    if(c->ignoreHOffsetWhenCentered != oldIgnoreHCentered || c->ignoreVOffsetWhenCentered != oldIgnoreVCentered ||
       c->ignoreHOffsetWhenRelative != oldIgnoreHRelative || c->ignoreVOffsetWhenRelative != oldIgnoreVRelative) ch = TRUE;

    if(!ch) return FALSE;
    if(c->iniPath[0]){
        WCHAR buf[32];
        settings_write(L"Placement",L"PointerRelative",c->pointerRelative?L"true":L"false",c->iniPath);
        const WCHAR* hp=L"right"; if(c->hPlacement==0) hp=L"left"; else if(c->hPlacement==1) hp=L"center";
        const WCHAR* vp=L"bottom"; if(c->vPlacement==0) vp=L"top"; else if(c->vPlacement==1) vp=L"center";
        settings_write(L"Placement",L"Horizontal",hp,c->iniPath);
        settings_write(L"Placement",L"Vertical",vp,c->iniPath);
        StringCchPrintfW(buf, ARRAYSIZE(buf),L"%d",c->hOffset); settings_write(L"Placement",L"HOffset",buf,c->iniPath);
        StringCchPrintfW(buf, ARRAYSIZE(buf),L"%d",c->vOffset); settings_write(L"Placement",L"VOffset",buf,c->iniPath);
        const WCHAR* cen=L"false"; if(c->ignoreHOffsetWhenCentered&&c->ignoreVOffsetWhenCentered) cen=L"true"; else if(c->ignoreHOffsetWhenCentered) cen=L"HOffset"; else if(c->ignoreVOffsetWhenCentered) cen=L"VOffset"; settings_write(L"Placement",L"IgnoreOffsetWhenCentered",cen,c->iniPath);
        const WCHAR* rel=L"false"; if(c->ignoreHOffsetWhenRelative&&c->ignoreVOffsetWhenRelative) rel=L"true"; else if(c->ignoreHOffsetWhenRelative) rel=L"HOffset"; else if(c->ignoreVOffsetWhenRelative) rel=L"VOffset"; settings_write(L"Placement",L"IgnoreOffsetWhenRelative",rel,c->iniPath);
    }
    return TRUE;
}

// -------- Sorting Page --------
static void Sorting_Load(HWND pg, Config* c){
    HWND hSortField = GetDlgItem(pg, IDC_SORT_FIELD);
    if (hSortField) {
        SendMessageW(hSortField, CB_RESETCONTENT, 0, 0);
        SendMessageW(hSortField, CB_ADDSTRING, 0, (LPARAM)L"Name");           // 0
        SendMessageW(hSortField, CB_ADDSTRING, 0, (LPARAM)L"Date Modified");  // 1
        SendMessageW(hSortField, CB_ADDSTRING, 0, (LPARAM)L"Date Created");   // 2
        SendMessageW(hSortField, CB_ADDSTRING, 0, (LPARAM)L"File Type");      // 3
        SendMessageW(hSortField, CB_ADDSTRING, 0, (LPARAM)L"Size");           // 4
        int sel = (c->sortField >= 0 && c->sortField <= 4) ? c->sortField : 0;
        SendMessageW(hSortField, CB_SETCURSEL, sel, 0);
    }
    HWND hSortDir = GetDlgItem(pg, IDC_SORT_DIRECTION);
    if (hSortDir) {
        SendMessageW(hSortDir, CB_RESETCONTENT, 0, 0);
        SendMessageW(hSortDir, CB_ADDSTRING, 0, (LPARAM)L"Ascending");
        SendMessageW(hSortDir, CB_ADDSTRING, 0, (LPARAM)L"Descending");
        SendMessageW(hSortDir, CB_SETCURSEL, c->sortDescending ? 1 : 0, 0);
    }
    HWND hSortObjType = GetDlgItem(pg, IDC_SORT_FOLDERSFIRST);
    if (hSortObjType) {
        SendMessageW(hSortObjType, CB_RESETCONTENT, 0, 0);
        SendMessageW(hSortObjType, CB_ADDSTRING, 0, (LPARAM)L"Disabled"); // 0
        SendMessageW(hSortObjType, CB_ADDSTRING, 0, (LPARAM)L"Folders first");  // 1
        SendMessageW(hSortObjType, CB_ADDSTRING, 0, (LPARAM)L"Files first");    // 2
        int sel = (c->sortObjectTypePriority >= 0 && c->sortObjectTypePriority <= 2) ? c->sortObjectTypePriority : 0;
        SendMessageW(hSortObjType, CB_SETCURSEL, sel, 0);
    }
}

static void Sorting_SyncCombo(SettingsState* st, HWND sourceCombo, int id){
    if(!st || !sourceCombo) return;
    if(id != IDC_SORT_FIELD && id != IDC_SORT_DIRECTION && id != IDC_SORT_FOLDERSFIRST) return;
    int sel = (int)SendMessageW(sourceCombo, CB_GETCURSEL, 0, 0);
    HWND sourcePage = GetParent(sourceCombo);
    HWND pages[] = { st->pages[PAGE_GENERAL], st->pages[PAGE_SORTING] };
    for(int i=0; i<2; ++i){
        HWND page = pages[i];
        if(!page || page == sourcePage) continue;
        HWND combo = GetDlgItem(page, id);
        if(combo) SendMessageW(combo, CB_SETCURSEL, sel, 0);
    }
}
static BOOL Sorting_Save(HWND pg, Config* c){
    BOOL ch=FALSE;
    HWND hSortField = GetDlgItem(pg, IDC_SORT_FIELD);
    int v = (int)SendMessageW(hSortField, CB_GETCURSEL, 0, 0);
    if (v >= 0 && v <= 4 && c->sortField != v) { c->sortField = v; ch = TRUE; }
    HWND hSortDir = GetDlgItem(pg, IDC_SORT_DIRECTION);
    int dir = (int)SendMessageW(hSortDir, CB_GETCURSEL, 0, 0);
    BOOL desc = (dir == 1);
    if (c->sortDescending != desc) { c->sortDescending = desc; ch = TRUE; }
    HWND hSortObjType = GetDlgItem(pg, IDC_SORT_FOLDERSFIRST);
    int objType = (int)SendMessageW(hSortObjType, CB_GETCURSEL, 0, 0);
    if (objType < 0 || objType > 2) objType = 0;
    if (c->sortObjectTypePriority != objType) { c->sortObjectTypePriority = objType; ch = TRUE; }
    if(!ch) return FALSE;
    if(c->iniPath[0]){
        const WCHAR* fields[] = {L"name", L"datemodified", L"datecreated", L"type", L"size"};
        if(c->sortField >= 0 && c->sortField < 5)
            settings_write(L"Sorting",L"SortBy",fields[c->sortField],c->iniPath);
        settings_write(L"Sorting",L"SortDirection",c->sortDescending?L"descending":L"ascending",c->iniPath);
        const WCHAR* objTypeStr = L"false";
        if (c->sortObjectTypePriority == 1) objTypeStr = L"true";
        else if (c->sortObjectTypePriority == 2) objTypeStr = L"files";
        settings_write(L"Sorting",L"FoldersFirst",objTypeStr,c->iniPath);
    }
    return TRUE;
}

// -------- Controls Page --------
static void Controls_Load(HWND pg, Config* c){
    set_check(pg, IDC_CTRL_LEFT_CLICK, c->leftClickTrigger);
    set_check(pg, IDC_CTRL_RIGHT_CLICK, c->rightClickTrigger);
    set_check(pg, IDC_CTRL_MIDDLE_CLICK, c->middleClickTrigger);
    set_check(pg, IDC_CTRL_SHIFT_LEFT_CLICK, c->shiftLeftClickTrigger);
    set_check(pg, IDC_CTRL_SHIFT_RIGHT_CLICK, c->shiftRightClickTrigger);
    set_check(pg, IDC_CTRL_SHIFT_MIDDLE_CLICK, c->shiftMiddleClickTrigger);
    set_check(pg, IDC_CTRL_WINDOWS_KEY, c->windowsKeyTrigger);
    set_check(pg, IDC_CTRL_SHIFT_WINDOWS_KEY, c->shiftWindowsKeyTrigger);
    set_check(pg, IDC_CTRL_WINDOWS_KEY_X, c->windowsKeyXTrigger);
    set_check(pg, IDC_CTRL_LEGACY_START_DETECTION, c->legacyStartButtonDetection);
    {
        HWND combo = GetDlgItem(pg, IDC_CTRL_SETTINGS_ITEM_COMBO);
        if (combo) {
            static const WCHAR* labels[] = { L"Disabled", L"hold Shift", L"Win + X", L"Right click", L"Middle click", L"Always" };
            SendMessageW(combo, CB_RESETCONTENT, 0, 0);
            for (int i = 0; i < ARRAYSIZE(labels); ++i) SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)labels[i]);
            SendMessageW(combo, CB_SETCURSEL, c->showSettingsItem, 0);
        }
    }
    set_check(pg, IDC_IGNORE_FULLSCREEN_CHECK, c->ignoreTriggersWhenFullscreen);
    SetDlgItemTextW(pg, IDC_FULLSCREEN_EXCLUSION_EDIT, c->fullscreenExclusionList);
}

static BOOL Controls_Save(HWND pg, Config* c){
    BOOL ch = FALSE;
    BOOL b;

    b = get_check(pg, IDC_CTRL_LEFT_CLICK); if(c->leftClickTrigger != b){ c->leftClickTrigger = b; ch = TRUE; }
    b = get_check(pg, IDC_CTRL_RIGHT_CLICK); if(c->rightClickTrigger != b){ c->rightClickTrigger = b; ch = TRUE; }
    b = get_check(pg, IDC_CTRL_MIDDLE_CLICK); if(c->middleClickTrigger != b){ c->middleClickTrigger = b; ch = TRUE; }
    b = get_check(pg, IDC_CTRL_SHIFT_LEFT_CLICK); if(c->shiftLeftClickTrigger != b){ c->shiftLeftClickTrigger = b; ch = TRUE; }
    b = get_check(pg, IDC_CTRL_SHIFT_RIGHT_CLICK); if(c->shiftRightClickTrigger != b){ c->shiftRightClickTrigger = b; ch = TRUE; }
    b = get_check(pg, IDC_CTRL_SHIFT_MIDDLE_CLICK); if(c->shiftMiddleClickTrigger != b){ c->shiftMiddleClickTrigger = b; ch = TRUE; }
    b = get_check(pg, IDC_CTRL_WINDOWS_KEY); if(c->windowsKeyTrigger != b){ c->windowsKeyTrigger = b; ch = TRUE; }
    b = get_check(pg, IDC_CTRL_SHIFT_WINDOWS_KEY); if(c->shiftWindowsKeyTrigger != b){ c->shiftWindowsKeyTrigger = b; ch = TRUE; }
    b = get_check(pg, IDC_CTRL_WINDOWS_KEY_X); if(c->windowsKeyXTrigger != b){ c->windowsKeyXTrigger = b; ch = TRUE; }
    b = get_check(pg, IDC_CTRL_LEGACY_START_DETECTION); if(c->legacyStartButtonDetection != b){ c->legacyStartButtonDetection = b; ch = TRUE; }
    {
        HWND combo = GetDlgItem(pg, IDC_CTRL_SETTINGS_ITEM_COMBO);
        int mode = combo ? (int)SendMessageW(combo, CB_GETCURSEL, 0, 0) : SETTINGS_ITEM_DISABLED;
        if (mode < SETTINGS_ITEM_DISABLED || mode > SETTINGS_ITEM_ALWAYS) mode = SETTINGS_ITEM_DISABLED;
        if (c->showSettingsItem != mode) { c->showSettingsItem = (SettingsItemMode)mode; ch = TRUE; }
    }
    b = get_check(pg, IDC_IGNORE_FULLSCREEN_CHECK); if(c->ignoreTriggersWhenFullscreen != b){ c->ignoreTriggersWhenFullscreen = b; ch = TRUE; }
    
    WCHAR exclusionBuf[1024];
    GetDlgItemTextW(pg, IDC_FULLSCREEN_EXCLUSION_EDIT, exclusionBuf, ARRAYSIZE(exclusionBuf));
    if(lstrcmpW(c->fullscreenExclusionList, exclusionBuf) != 0){
        lstrcpynW(c->fullscreenExclusionList, exclusionBuf, ARRAYSIZE(c->fullscreenExclusionList));
        ch = TRUE;
    }

    if(!ch) return FALSE;
    if(c->iniPath[0]){
        settings_write(L"Controls", L"LeftClick", c->leftClickTrigger?L"true":L"false", c->iniPath);
        settings_write(L"Controls", L"RightClick", c->rightClickTrigger?L"true":L"false", c->iniPath);
        settings_write(L"Controls", L"MiddleClick", c->middleClickTrigger?L"true":L"false", c->iniPath);
        settings_write(L"Controls", L"ShiftLeftClick", c->shiftLeftClickTrigger?L"true":L"false", c->iniPath);
        settings_write(L"Controls", L"ShiftRightClick", c->shiftRightClickTrigger?L"true":L"false", c->iniPath);
        settings_write(L"Controls", L"ShiftMiddleClick", c->shiftMiddleClickTrigger?L"true":L"false", c->iniPath);
        settings_write(L"Controls", L"WindowsKey", c->windowsKeyTrigger?L"true":L"false", c->iniPath);
        settings_write(L"Controls", L"ShiftWindowsKey", c->shiftWindowsKeyTrigger?L"true":L"false", c->iniPath);
        settings_write(L"Controls", L"WindowsKeyX", c->windowsKeyXTrigger?L"true":L"false", c->iniPath);
        settings_write(L"Controls", L"LegacyStartButtonDetection", c->legacyStartButtonDetection?L"true":L"false", c->iniPath);
        {
            static const WCHAR* values[] = { L"disabled", L"shift", L"winx", L"rightclick", L"middleclick", L"always" };
            settings_write(L"Controls", L"ShowSettingsItem", values[c->showSettingsItem], c->iniPath);
        }
        settings_write(L"Controls", L"IgnoreTriggersWhenFullscreen", c->ignoreTriggersWhenFullscreen?L"true":L"false", c->iniPath);
        settings_write(L"Controls", L"FullscreenExclusionList", c->fullscreenExclusionList, c->iniPath);
    }
    return TRUE;
}

// -------- Advanced Page --------
static void Advanced_Load(HWND pg, Config* c){
    set_check(pg,IDC_RECENT_SHOW_CLEAN,c->recentShowCleanItems);
    set_check(pg,IDC_RECENT_SEPARATE_ITEMS,c->separateItems);
    set_check(pg,IDC_RECENT_HIDE_PROTOCOL_SHORTCUTS,c->recentHideProtocolShortcuts);
    set_check(pg,IDC_THISPC_AS_SUBMENU,c->thisPCAsSubmenu);
    set_check(pg,IDC_THISPC_ITEMS_AS_SUBMENUS,c->thisPCItemsAsSubmenus);
    set_check(pg,IDC_HOME_AS_SUBMENU,c->homeAsSubmenu);
    set_check(pg,IDC_HOME_ITEMS_AS_SUBMENUS,c->homeItemsAsSubmenus);
    set_check(pg,IDC_TASKKILL_ALL_DESKTOPS,c->taskKillAllDesktops);
    set_check(pg,IDC_TASKKILL_IGNORE_SYSTEM,c->taskKillIgnoreSystem);
    set_check(pg,IDC_TASKKILL_LIST_WINDOWS,c->taskKillListWindows);
    set_check(pg,IDC_TASKKILL_SHOW_PROCESS_IDS,c->taskKillShowProcessIds);
    set_check(pg, IDC_POWER_HIBERNATE, !c->excludeHibernate);
    set_check(pg, IDC_POWER_LOCK,      !c->excludeLock);
    set_check(pg, IDC_POWER_LOGOFF,    !c->excludeLogoff);
    set_check(pg, IDC_POWER_RESTART,   !c->excludeRestart);
    set_check(pg, IDC_POWER_SHUTDOWN,  !c->excludeShutdown);
    set_check(pg, IDC_POWER_SLEEP,     !c->excludeSleep);
    set_check(pg, IDC_POWER_CONFIRM,   c->powerAskConfirmation);
    set_int(pg,IDC_TASKKILL_MAX_COMBO,c->taskKillMax);
    if(c->taskKillExcludes[0]){
        SetDlgItemTextW(pg,IDC_TASKKILL_EXCLUDES,c->taskKillExcludes);
    }else{
        // Set cue banner (placeholder) if empty
        HWND hEdit = GetDlgItem(pg, IDC_TASKKILL_EXCLUDES);
        if(hEdit){
            // Windows >= Vista: EM_SETCUEBANNER = 0x1501
            SendMessageW(hEdit, 0x1501, TRUE, (LPARAM)L"mspaint.exe,xboxpcapp.exe");
            SetDlgItemTextW(pg,IDC_TASKKILL_EXCLUDES,L"");
        }
    }
    // Populate name display combo (ID 1702): 0=Full path,1=File name
    HWND hCombo=GetDlgItem(pg,1702);
    if(hCombo){
        SendMessageW(hCombo,CB_RESETCONTENT,0,0);
        SendMessageW(hCombo,CB_ADDSTRING,0,(LPARAM)L"Full path");
        SendMessageW(hCombo,CB_ADDSTRING,0,(LPARAM)L"File name");
        int sel=(c->recentLabelMode==1)?1:0;
        SendMessageW(hCombo,CB_SETCURSEL,sel,0);
    }
    SetDlgItemTextW(pg,IDC_DEFAULT_ICON,c->defaultIconPath);
    SetDlgItemTextW(pg,IDC_DEFAULT_ICON_LIGHT,c->defaultIconPathLight);
    SetDlgItemTextW(pg,IDC_DEFAULT_ICON_DARK,c->defaultIconPathDark);
    {
        HWND hRecentMaxEdit = GetDlgItem(pg, IDC_RECENTMAX_EDIT);
        HWND hRecentMaxSpin = GetDlgItem(pg, IDC_RECENTMAX_SPIN);
        set_int(pg, IDC_RECENTMAX_EDIT, c->recentMax);
        if(hRecentMaxSpin) {
            SendMessageW(hRecentMaxSpin, UDM_SETBUDDY, (WPARAM)hRecentMaxEdit, 0);
            SendMessageW(hRecentMaxSpin, UDM_SETRANGE32, 1, 99);
            SendMessageW(hRecentMaxSpin, UDM_SETPOS32, 0, c->recentMax);
        }
    }
}
static BOOL Advanced_Save(HWND pg, Config* c){
    BOOL ch=FALSE; BOOL b;
    b=get_check(pg,IDC_RECENT_SHOW_CLEAN); if(c->recentShowCleanItems!=b){c->recentShowCleanItems=b;ch=TRUE;}
    b=get_check(pg,IDC_RECENT_SEPARATE_ITEMS); if(c->separateItems!=b){c->separateItems=b;ch=TRUE;}
    b=get_check(pg,IDC_RECENT_HIDE_PROTOCOL_SHORTCUTS); if(c->recentHideProtocolShortcuts!=b){c->recentHideProtocolShortcuts=b;ch=TRUE;}
    b=get_check(pg,IDC_THISPC_AS_SUBMENU); if(c->thisPCAsSubmenu!=b){c->thisPCAsSubmenu=b;ch=TRUE;}
    b=get_check(pg,IDC_THISPC_ITEMS_AS_SUBMENUS); if(c->thisPCItemsAsSubmenus!=b){c->thisPCItemsAsSubmenus=b;ch=TRUE;}
    b=get_check(pg,IDC_HOME_AS_SUBMENU); if(c->homeAsSubmenu!=b){c->homeAsSubmenu=b;ch=TRUE;}
    b=get_check(pg,IDC_HOME_ITEMS_AS_SUBMENUS); if(c->homeItemsAsSubmenus!=b){c->homeItemsAsSubmenus=b;ch=TRUE;}
    b=get_check(pg,IDC_TASKKILL_ALL_DESKTOPS); if(c->taskKillAllDesktops!=b){c->taskKillAllDesktops=b;ch=TRUE;}
    b=get_check(pg,IDC_TASKKILL_IGNORE_SYSTEM); if(c->taskKillIgnoreSystem!=b){c->taskKillIgnoreSystem=b;ch=TRUE;}
    b=get_check(pg,IDC_TASKKILL_LIST_WINDOWS); if(c->taskKillListWindows!=b){c->taskKillListWindows=b;ch=TRUE;}
    b=get_check(pg,IDC_TASKKILL_SHOW_PROCESS_IDS); if(c->taskKillShowProcessIds!=b){c->taskKillShowProcessIds=b;ch=TRUE;}
    int v2=get_int(pg,IDC_TASKKILL_MAX_COMBO,c->taskKillMax); if(v2!=c->taskKillMax){c->taskKillMax=v2;ch=TRUE;}
    WCHAR buf2[512] = L"";
    GetDlgItemTextW(pg,IDC_TASKKILL_EXCLUDES,buf2,ARRAYSIZE(buf2));
    if(lstrcmpW(buf2,c->taskKillExcludes)!=0){
        lstrcpynW(c->taskKillExcludes,buf2,ARRAYSIZE(c->taskKillExcludes));
        ch=TRUE;
    }
    {
        BOOL oldHibernate = c->excludeHibernate;
        BOOL oldLock = c->excludeLock;
        BOOL oldLogoff = c->excludeLogoff;
        BOOL oldRestart = c->excludeRestart;
        BOOL oldShutdown = c->excludeShutdown;
        BOOL oldSleep = c->excludeSleep;
        BOOL oldConfirm = c->powerAskConfirmation;
        c->excludeHibernate = !get_check(pg, IDC_POWER_HIBERNATE);
        c->excludeLock      = !get_check(pg, IDC_POWER_LOCK);
        c->excludeLogoff    = !get_check(pg, IDC_POWER_LOGOFF);
        c->excludeRestart   = !get_check(pg, IDC_POWER_RESTART);
        c->excludeShutdown  = !get_check(pg, IDC_POWER_SHUTDOWN);
        c->excludeSleep     = !get_check(pg, IDC_POWER_SLEEP);
        c->powerAskConfirmation = get_check(pg, IDC_POWER_CONFIRM);
        if (oldHibernate != c->excludeHibernate || oldLock != c->excludeLock ||
            oldLogoff != c->excludeLogoff || oldRestart != c->excludeRestart ||
            oldShutdown != c->excludeShutdown || oldSleep != c->excludeSleep ||
            oldConfirm != c->powerAskConfirmation) ch = TRUE;
    }
    HWND hCombo=GetDlgItem(pg,1702);
    if(hCombo){
        int mode=(int)SendMessageW(hCombo,CB_GETCURSEL,0,0);
        if(mode<0) mode=0;
        if(c->recentLabelMode!=mode){c->recentLabelMode=mode;ch=TRUE;}
    }
    int v=get_int(pg,IDC_RECENTMAX_EDIT,c->recentMax); if(v!=c->recentMax){c->recentMax=v;ch=TRUE;}
    WCHAR buf[MAX_PATH];
    if(GetDlgItemTextW(pg,IDC_DEFAULT_ICON,buf,ARRAYSIZE(buf))){ if(lstrcmpW(buf,c->defaultIconPath)!=0){ lstrcpynW(c->defaultIconPath,buf,ARRAYSIZE(c->defaultIconPath)); ch=TRUE; }}
    if(GetDlgItemTextW(pg,IDC_DEFAULT_ICON_LIGHT,buf,ARRAYSIZE(buf))){ if(lstrcmpW(buf,c->defaultIconPathLight)!=0){ lstrcpynW(c->defaultIconPathLight,buf,ARRAYSIZE(c->defaultIconPathLight)); ch=TRUE; }}
    if(GetDlgItemTextW(pg,IDC_DEFAULT_ICON_DARK,buf,ARRAYSIZE(buf))){ if(lstrcmpW(buf,c->defaultIconPathDark)!=0){ lstrcpynW(c->defaultIconPathDark,buf,ARRAYSIZE(c->defaultIconPathDark)); ch=TRUE; }}
    if(!ch) return FALSE;
    if(c->iniPath[0]){
        settings_write(L"RecentItems",L"RecentShowCleanItems",c->recentShowCleanItems?L"true":L"false",c->iniPath);
        settings_write(L"RecentItems",L"SeparateItems",c->separateItems?L"true":L"false",c->iniPath);
        settings_write(L"RecentItems",L"RecentHideProtocolShortcuts",c->recentHideProtocolShortcuts?L"true":L"false",c->iniPath);
        settings_write(L"RecentItems",L"RecentGroupFoldersFirst",NULL,c->iniPath);
        settings_write(L"RecentItems",L"RecentLabel", c->recentLabelMode==0?L"fullpath":L"name", c->iniPath);
        settings_write(L"ThisPC",L"ThisPCAsSubmenu",c->thisPCAsSubmenu?L"true":L"false",c->iniPath);
        settings_write(L"ThisPC",L"ThisPCItemsAsSubmenus",c->thisPCItemsAsSubmenus?L"true":L"false",c->iniPath);
        settings_write(L"Home",L"HomeAsSubmenu",c->homeAsSubmenu?L"true":L"false",c->iniPath);
        settings_write(L"Home",L"HomeItemsAsSubmenus",c->homeItemsAsSubmenus?L"true":L"false",c->iniPath);
        settings_write(L"TaskKill",L"TaskKillAllDesktops",c->taskKillAllDesktops?L"true":L"false",c->iniPath);
        settings_write(L"TaskKill",L"TaskKillIgnoreSystem",c->taskKillIgnoreSystem?L"true":L"false",c->iniPath);
        settings_write(L"TaskKill",L"TaskKillListWindows",c->taskKillListWindows?L"true":L"false",c->iniPath);
        settings_write(L"TaskKill",L"TaskKillShowProcessIds",c->taskKillShowProcessIds?L"true":L"false",c->iniPath);
        WCHAR num2[32]; StringCchPrintfW(num2, ARRAYSIZE(num2),L"%d",c->taskKillMax); settings_write(L"TaskKill",L"TaskKillMax",num2,c->iniPath);
        settings_write(L"TaskKill",L"TaskKillExcludes",c->taskKillExcludes,c->iniPath);
        WCHAR num[32]; StringCchPrintfW(num, ARRAYSIZE(num),L"%d",c->recentMax); settings_write(L"RecentItems",L"RecentMax",num,c->iniPath);
        if(c->defaultIconPath[0]) settings_write(L"General",L"DefaultIcon",c->defaultIconPath,c->iniPath); else settings_write(L"General",L"DefaultIcon",NULL,c->iniPath);
        if(c->defaultIconPathLight[0]) settings_write(L"General",L"DefaultIconLight",c->defaultIconPathLight,c->iniPath); else settings_write(L"General",L"DefaultIconLight",NULL,c->iniPath);
        if(c->defaultIconPathDark[0]) settings_write(L"General",L"DefaultIconDark",c->defaultIconPathDark,c->iniPath); else settings_write(L"General",L"DefaultIconDark",NULL,c->iniPath);
        if(c->excludeSleep) settings_write(L"Power",L"Sleep",L"0",c->iniPath); else settings_write(L"Power",L"Sleep",NULL,c->iniPath);
        if(c->excludeHibernate) settings_write(L"Power",L"Hibernate",L"0",c->iniPath); else settings_write(L"Power",L"Hibernate",NULL,c->iniPath);
        if(c->excludeShutdown) settings_write(L"Power",L"Shutdown",L"0",c->iniPath); else settings_write(L"Power",L"Shutdown",NULL,c->iniPath);
        if(c->excludeRestart) settings_write(L"Power",L"Restart",L"0",c->iniPath); else settings_write(L"Power",L"Restart",NULL,c->iniPath);
        if(c->excludeLock) settings_write(L"Power",L"Lock",L"0",c->iniPath); else settings_write(L"Power",L"Lock",NULL,c->iniPath);
        if(c->excludeLogoff) settings_write(L"Power",L"Logoff",L"0",c->iniPath); else settings_write(L"Power",L"Logoff",NULL,c->iniPath);
        settings_write(L"Power",L"AskForConfirmation",c->powerAskConfirmation?L"true":L"false",c->iniPath);
    }
    return TRUE;
}

// -------- Menu & Icons Pages (read-only skeleton) --------
static const WCHAR* item_type_name(ConfigItemType t){
    switch(t){
        case CI_SEPARATOR: return L"Separator";
        case CI_URI: return L"URI";
        case CI_FILE: return L"File";
        case CI_CMD: return L"Command";
        case CI_FOLDER: return L"Folder";
        case CI_FOLDER_SUBMENU: return L"Folder as a submenu";
        case CI_POWER_SLEEP: return L"Sleep";
        case CI_POWER_HIBERNATE: return L"Hibernate";
        case CI_POWER_SHUTDOWN: return L"Shut down";
        case CI_POWER_RESTART: return L"Restart";
        case CI_POWER_LOCK: return L"Lock";
        case CI_POWER_LOGOFF: return L"Sign out";
        case CI_RECENT_SUBMENU: return L"Recent Items";
        case CI_POWER_MENU: return L"Power Menu";
        case CI_THISPC: return L"This PC";
        case CI_HOME: return L"Home";
        case CI_SETTINGS: return L"Menu Settings";
        case CI_TASKKILL: return L"Task Kill";
    }
    return L"?";
}
static void lv_add_col_aligned(HWND lv,int i,int w,const WCHAR* txt,int fmt){
    LVCOLUMNW c; ZeroMemory(&c,sizeof(c));
    c.mask = LVCF_WIDTH|LVCF_TEXT|LVCF_SUBITEM|LVCF_FMT;
    c.fmt = fmt;
    c.cx   = w;
    WCHAR tmp[128];
    if(!txt) txt=L"";
    lstrcpynW(tmp, txt, ARRAYSIZE(tmp));
    c.pszText = tmp;
    c.iSubItem = i;
    ListView_InsertColumn(lv,i,&c);
}
static void lv_add_col(HWND lv,int i,int w,const WCHAR* txt){
    lv_add_col_aligned(lv, i, w, txt, LVCFMT_LEFT);
}
static void lv_autosize_cols(HWND lv){
    if(!lv) return;
    HWND header = ListView_GetHeader(lv);
    int colCount = header ? Header_GetItemCount(header) : 0;
    for(int i=0; i<colCount; ++i){
        // Win32 always left-aligns the first visible column. Keep column zero
        // hidden so the displayed ID column can use the requested alignment.
        if(i==0){
            ListView_SetColumnWidth(lv, i, 0);
            continue;
        }
        int widthContent;
        int widthHeader;
        ListView_SetColumnWidth(lv, i, LVSCW_AUTOSIZE);
        widthContent = ListView_GetColumnWidth(lv, i);
        ListView_SetColumnWidth(lv, i, LVSCW_AUTOSIZE_USEHEADER);
        widthHeader = ListView_GetColumnWidth(lv, i);
        ListView_SetColumnWidth(lv, i, (widthContent > widthHeader) ? widthContent : widthHeader);
    }
}
static void lv_set_text(HWND lv,int row,int col,const WCHAR* text){ WCHAR tmp[512]; if(!text) text=L""; lstrcpynW(tmp,text,ARRAYSIZE(tmp)); ListView_SetItemText(lv,row,col,tmp);} 
static void Menu_Load(HWND pg, Config* c){
    HWND lv=GetDlgItem(pg,IDC_MENU_LIST);
    if(!lv||!c) return;
    // Determine if we have a SettingsState working copy (parent is the tab child dialog)
    SettingsState* st=(SettingsState*)GetWindowLongPtrW(GetParent(pg),GWLP_USERDATA);
    ListView_SetExtendedListViewStyle(lv,LVS_EX_FULLROWSELECT|LVS_EX_GRIDLINES|LVS_EX_DOUBLEBUFFER);
    while(ListView_DeleteColumn(lv,0));
    // The hidden first column allows the visible ID column to be right-aligned.
    lv_add_col(lv,0,0,L"");
    lv_add_col_aligned(lv,1,40,L"#",LVCFMT_RIGHT);
    lv_add_col(lv,2,90,L"Type");
    lv_add_col(lv,3,110,L"Label");
    lv_add_col(lv,4,160,L"Path"); // widen path to reclaim removed column width
    lv_add_col(lv,5,110,L"Parameters");
    ListView_DeleteAllItems(lv);
    ConfigItem* arr=c->items; int cnt=c->count;
    if(st && st->workingCount>0){ arr=st->workingItems; cnt=st->workingCount; }
    for(int i=0;i<cnt;i++){
        ConfigItem* it=&arr[i];
        WCHAR idx[12]; StringCchPrintfW(idx, ARRAYSIZE(idx),L"%d",i+1);
        WCHAR idxCopy[12]; lstrcpynW(idxCopy,idx,ARRAYSIZE(idxCopy));
        LVITEMW li; ZeroMemory(&li,sizeof(li));
        li.mask=LVIF_TEXT|LVIF_PARAM; li.iItem=i; li.pszText=L""; li.lParam=i;
        ListView_InsertItem(lv,&li);
        ListView_SetItemText(lv,i,1,idxCopy);
        {
            const WCHAR* tname = item_type_name(it->type);
            WCHAR tmpType[64]; lstrcpynW(tmpType,tname? tname : L"?",ARRAYSIZE(tmpType));
            ListView_SetItemText(lv,i,2,tmpType);
        }
        if(it->label[0]){ ListView_SetItemText(lv,i,3,it->label); }
        else if(it->type==CI_SEPARATOR){ lv_set_text(lv,i,3,L"(separator)"); }
        else { lv_set_text(lv,i,3,L""); }
        if(it->type==CI_URI||it->type==CI_FILE||it->type==CI_CMD||it->type==CI_FOLDER||it->type==CI_FOLDER_SUBMENU){ ListView_SetItemText(lv,i,4,it->path); }
        else if(it->type==CI_POWER_MENU||it->type==CI_RECENT_SUBMENU){ lv_set_text(lv,i,4,L""); }
        else { lv_set_text(lv,i,4,L""); }
        if(it->params[0]) ListView_SetItemText(lv,i,5,it->params);
    }
    // Avoid autosizing columns while user is actively dragging to prevent flicker
    st = (SettingsState*)GetWindowLongPtrW(GetParent(pg),GWLP_USERDATA);
    if (!(st && st->dragging)) {
        lv_autosize_cols(lv);
    }
}
// Map internal enum to legacy textual token used in original INI format
static const WCHAR* item_type_token(ConfigItemType t){
    switch(t){
        case CI_SEPARATOR: return L"SEPARATOR";
        case CI_URI: return L"URI";
        case CI_FILE: return L"FILE";
        case CI_CMD: return L"CMD";
        case CI_FOLDER: return L"FOLDER";
        case CI_FOLDER_SUBMENU: return L"FOLDER_SUBMENU";
        case CI_POWER_SLEEP: return L"POWER_SLEEP";
        case CI_POWER_HIBERNATE: return L"POWER_HIBERNATE";
        case CI_POWER_SHUTDOWN: return L"POWER_SHUTDOWN";
        case CI_POWER_RESTART: return L"POWER_RESTART";
        case CI_POWER_LOCK: return L"POWER_LOCK";
        case CI_POWER_LOGOFF: return L"POWER_LOGOFF";
        case CI_RECENT_SUBMENU: return L"RECENT_SUBMENU";
        case CI_POWER_MENU: return L"POWER_MENU";
        case CI_TASKKILL: return L"TASKKILL";
        case CI_THISPC: return L"THISPC";
        case CI_HOME: return L"HOME";
        case CI_SETTINGS: return L"SETTINGS";
        default: return L"UNKNOWN";
    }
}
static BOOL Menu_Save(HWND pg, Config* c){ UNREFERENCED_PARAMETER(pg); if(!c||!c->iniPath[0]) return FALSE;
    // Legacy format: ItemN=Label|TYPE|Path|(optional Params)
    // We overwrite each ItemN key preserving compatibility with existing parser.
    WCHAR key[32]; WCHAR line[2048];
    for(int i=0;i<c->count;i++){
        ConfigItem* it=&c->items[i]; StringCchPrintfW(key, ARRAYSIZE(key),L"Item%d",i+1);
        const WCHAR* token=item_type_token(it->type);
        if(it->type==CI_SEPARATOR){ // Always write canonical separator form
            StringCchPrintfW(line, ARRAYSIZE(line),L"---|SEPARATOR|");
        } else {
            // For special types that don't use path, ensure correct serialization
            if(
                it->type==CI_POWER_MENU ||
                it->type==CI_RECENT_SUBMENU ||
                it->type==CI_TASKKILL ||
                it->type==CI_SETTINGS
            ){
                // These types do not use path/params, just label and type
                StringCchPrintfW(line, ARRAYSIZE(line),L"%s|%s|",it->label,token);
            } else if(it->path[0]){
                if(it->params[0]) StringCchPrintfW(line, ARRAYSIZE(line),L"%s|%s|%s|%s",it->label,token,it->path,it->params);
                else StringCchPrintfW(line, ARRAYSIZE(line),L"%s|%s|%s",it->label,token,it->path);
            } else {
                // Ensure trailing bar to indicate empty path field when historically present (power items)
                if(it->params[0]) StringCchPrintfW(line, ARRAYSIZE(line),L"%s|%s||%s",it->label,token,it->params);
                else StringCchPrintfW(line, ARRAYSIZE(line),L"%s|%s|",it->label,token);
            }
        }
        settings_write(L"Menu",key,line,c->iniPath);
    }
    // Remove entries left behind when the list becomes shorter. The parser
    // scans Item1..Item64 and would otherwise load deleted trailing items.
    for(int i=c->count;i<64;i++){
        StringCchPrintfW(key, ARRAYSIZE(key),L"Item%d",i+1);
        settings_write(L"Menu",key,NULL,c->iniPath);
    }
    // Write Count for convenience (parser ignores if absent)
    WCHAR buf[16]; StringCchPrintfW(buf, ARRAYSIZE(buf),L"%d",c->count); settings_write(L"Menu",L"Count",buf,c->iniPath);
    return TRUE; }
static void Icons_Load(HWND pg, Config* c){
    HWND lv=GetDlgItem(pg,IDC_ICONS_LIST); if(!lv||!c) return;
    SettingsState* st=(SettingsState*)GetWindowLongPtrW(GetParent(pg),GWLP_USERDATA);
    ListView_SetExtendedListViewStyle(lv,LVS_EX_FULLROWSELECT|LVS_EX_GRIDLINES|LVS_EX_DOUBLEBUFFER);
    while(ListView_DeleteColumn(lv,0));
    lv_add_col(lv,0,0,L"");
    lv_add_col_aligned(lv,1,40,L"#",LVCFMT_RIGHT);
    lv_add_col(lv,2,120,L"Label");
    lv_add_col(lv,3,125,L"Default");
    lv_add_col(lv,4,110,L"Light");
    lv_add_col(lv,5,110,L"Dark");
    ListView_DeleteAllItems(lv);
    ConfigItem* arr=c->items; int cnt=c->count;
    if(st && st->workingCount>0){ arr=st->workingItems; cnt=st->workingCount; }
    for(int i=0;i<cnt;i++){
        ConfigItem* it=&arr[i];
        if(it->type==CI_SEPARATOR) continue;
        int row=ListView_GetItemCount(lv);
        WCHAR idx[12]; StringCchPrintfW(idx, ARRAYSIZE(idx),L"%d",i+1);
        LVITEMW li={0}; li.mask=LVIF_TEXT|LVIF_PARAM; li.iItem=row; li.pszText=L""; li.lParam=i; ListView_InsertItem(lv,&li);
        ListView_SetItemText(lv,row,1,idx);
        if(it->label[0]){
            ListView_SetItemText(lv,row,2,it->label);
        } else {
            const WCHAR* tname = item_type_name(it->type);
            WCHAR tmpType[64]; lstrcpynW(tmpType,tname? tname : L"?",ARRAYSIZE(tmpType));
            ListView_SetItemText(lv,row,2,tmpType);
        }
        // Show nothing when unset instead of placeholders
    if(it->iconPath[0]) { ListView_SetItemText(lv,row,3,it->iconPath); } else { lv_set_text(lv,row,3,L""); }
    if(it->iconPathLight[0]) { ListView_SetItemText(lv,row,4,it->iconPathLight); } else { lv_set_text(lv,row,4,L""); }
    if(it->iconPathDark[0]) { ListView_SetItemText(lv,row,5,it->iconPathDark); } else { lv_set_text(lv,row,5,L""); }
    }
    lv_autosize_cols(lv);
    ListView_SetColumnWidth(lv, 3, 115); // Default icon values remain readable.
    ListView_SetColumnWidth(lv, 4, 65);
    ListView_SetColumnWidth(lv, 5, 65);
}
// Selection helper for Icons list (placed early so later helpers can call without forward decl)
static int icons_get_selected_index(HWND lv){
    int sel=(int)SendMessageW(lv,LVM_GETNEXTITEM,(WPARAM)-1,LVNI_SELECTED);
        if(sel<0) return -1; 
    LVITEMW li; ZeroMemory(&li,sizeof(li));
    li.iItem=sel; li.mask=LVIF_PARAM;
    if(SendMessageW(lv,LVM_GETITEM,0,(LPARAM)&li)) return (int)li.lParam;
    return -1;
}
// Enable/disable Icons page buttons based on current selection/state
static void icons_update_buttons(SettingsState* st, HWND page){
    if(!st||!page) return; HWND lv=GetDlgItem(page,IDC_ICONS_LIST); if(!lv) return; int idx=icons_get_selected_index(lv);
    BOOL can=FALSE, canClear=FALSE; if(idx>=0 && idx<st->workingCount){ ConfigItem* it=&st->workingItems[idx]; if(it->type!=CI_SEPARATOR){ can=TRUE; if(it->iconPath[0]||it->iconPathLight[0]||it->iconPathDark[0]) canClear=TRUE; }}
    EnableWindow(GetDlgItem(page,IDC_ICON_BROWSE_FILE),can);
    EnableWindow(GetDlgItem(page,IDC_ICON_BROWSE_LIGHT),can);
    EnableWindow(GetDlgItem(page,IDC_ICON_BROWSE_DARK),can);
    EnableWindow(GetDlgItem(page,IDC_ICON_CLEAR),can && canClear);
}

// Persist icon paths in legacy compatible sections
static BOOL Icons_Save(HWND pg, Config* c){ UNREFERENCED_PARAMETER(pg); if(!c||!c->iniPath[0]) return FALSE; BOOL any=FALSE;
    WCHAR key[32];
    for(int i=0;i<c->count;i++){
        ConfigItem* it=&c->items[i];
        StringCchPrintfW(key, ARRAYSIZE(key),L"Icon%d",i+1);
        if(it->iconPath[0]){ settings_write(L"Icons",key,it->iconPath,c->iniPath); any=TRUE; } else { settings_write(L"Icons",key,NULL,c->iniPath); }
        StringCchPrintfW(key, ARRAYSIZE(key),L"Icon%d",i+1);
        if(it->iconPathLight[0]){ settings_write(L"IconsLight",key,it->iconPathLight,c->iniPath); any=TRUE; } else { settings_write(L"IconsLight",key,NULL,c->iniPath); }
        StringCchPrintfW(key, ARRAYSIZE(key),L"Icon%d",i+1);
        if(it->iconPathDark[0]){ settings_write(L"IconsDark",key,it->iconPathDark,c->iniPath); any=TRUE; } else { settings_write(L"IconsDark",key,NULL,c->iniPath); }
        // Remove obsolete experimental triplet
        StringCchPrintfW(key, ARRAYSIZE(key),L"Item%d",i+1); settings_write(L"Icons",key,NULL,c->iniPath);
    }
    for(int i=c->count;i<64;i++){
        StringCchPrintfW(key, ARRAYSIZE(key),L"Icon%d",i+1);
        settings_write(L"Icons",key,NULL,c->iniPath);
        settings_write(L"IconsLight",key,NULL,c->iniPath);
        settings_write(L"IconsDark",key,NULL,c->iniPath);
        StringCchPrintfW(key, ARRAYSIZE(key),L"Item%d",i+1);
        settings_write(L"Icons",key,NULL,c->iniPath);
    }
    // Keep section-level defaults and unknown keys when clearing per-item icons.
    return any;
}
// (menu_update_buttons & refresh_lists implemented later with working copy helpers)

// ---------------- Item Edit Dialog -----------------
typedef struct ItemEditCtx {
    ConfigItem tmp;
    ConfigItem original;
    BOOL editing;
    ConfigItemType displayedType;
    ConfigItemType originalType;
    BOOL labelCached[CI_SETTINGS + 1];
    WCHAR labelByType[CI_SETTINGS + 1][128];
    BOOL pathCached[CI_SETTINGS + 1];
    WCHAR pathByType[CI_SETTINGS + 1][MAX_PATH];
    BOOL paramsCached[CI_SETTINGS + 1];
    WCHAR paramsByType[CI_SETTINGS + 1][256];
} ItemEditCtx;
static BOOL item_type_cache_index(ConfigItemType t, int* index){
    if (t < CI_SEPARATOR || t > CI_SETTINGS) return FALSE;
    if (index) *index = (int)t;
    return TRUE;
}
static void item_type_visible_fields(ConfigItemType t, BOOL* showLabel, BOOL* showPath, BOOL* showParams){
    BOOL label = FALSE;
    BOOL path = FALSE;
    BOOL params = FALSE;
    switch(t){
        case CI_URI:
        case CI_FILE:
        case CI_CMD:
        case CI_FOLDER:
            label = TRUE;
            path = TRUE;
            params = TRUE;
            break;
        case CI_FOLDER_SUBMENU:
            label = TRUE;
            path = TRUE;
            break;
        case CI_RECENT_SUBMENU:
        case CI_POWER_MENU:
        case CI_THISPC:
        case CI_HOME:
        case CI_SETTINGS:
        case CI_TASKKILL:
            label = TRUE;
            break;
        default:
            break;
    }
    if (showLabel) *showLabel = label;
    if (showPath) *showPath = path;
    if (showParams) *showParams = params;
}
static BOOL item_type_browse_uses_file(ConfigItemType t){
    return t == CI_FILE;
}
static BOOL item_type_browse_uses_folder(ConfigItemType t){
    return (t == CI_FOLDER || t == CI_FOLDER_SUBMENU);
}
static BOOL item_browse_file(HWND owner, WCHAR* out, int cap, BOOL executableHint){
    OPENFILENAMEW ofn;
    WCHAR fileBuf[MAX_PATH] = {0};
    ZeroMemory(&ofn, sizeof(ofn));
    if (out && out[0]) lstrcpynW(fileBuf, out, ARRAYSIZE(fileBuf));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFile = fileBuf;
    ofn.nMaxFile = ARRAYSIZE(fileBuf);
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
    if (executableHint) {
        ofn.lpstrFilter = L"Executables (*.exe;*.bat;*.cmd)\0*.exe;*.bat;*.cmd\0All Files (*.*)\0*.*\0";
        ofn.lpstrTitle = L"Select Command";
    } else {
        ofn.lpstrFilter = L"All Files (*.*)\0*.*\0";
        ofn.lpstrTitle = L"Select File";
    }
    if (!GetOpenFileNameW(&ofn)) return FALSE;
    lstrcpynW(out, fileBuf, cap);
    return TRUE;
}
static BOOL item_browse_folder(HWND owner, WCHAR* out, int cap){
    BROWSEINFOW bi;
    LPITEMIDLIST pidl;
    WCHAR path[MAX_PATH] = {0};
    ZeroMemory(&bi, sizeof(bi));
    bi.hwndOwner = owner;
    bi.lpszTitle = L"Select Folder";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    pidl = SHBrowseForFolderW(&bi);
    if (!pidl) return FALSE;
    if (!SHGetPathFromIDListW(pidl, path)) {
        CoTaskMemFree(pidl);
        return FALSE;
    }
    CoTaskMemFree(pidl);
    lstrcpynW(out, path, cap);
    return TRUE;
}
static ConfigItemType item_get_selected_type(HWND dlg){
    HWND hType = GetDlgItem(dlg, IDC_ITEM_TYPE_COMBO);
    int sel = (int)SendMessageW(hType, CB_GETCURSEL, 0, 0);
    if (sel >= 0) return (ConfigItemType)SendMessageW(hType, CB_GETITEMDATA, sel, 0);
    return CI_FILE;
}
static void item_fill_folder_params_combo(HWND h){
    SendMessageW(h, CB_ADDSTRING, 0, (LPARAM)L"Default");
    SendMessageW(h, CB_ADDSTRING, 0, (LPARAM)L"Inline");
    SendMessageW(h, CB_ADDSTRING, 0, (LPARAM)L"Inline without title");
    SendMessageW(h, CB_ADDSTRING, 0, (LPARAM)L"Inline with clickable title");
}
static int item_folder_params_index(const WCHAR* params){
    if (!params || !params[0]) return 0;
    if (!lstrcmpiW(params, L"inline")) return 1;
    if (!lstrcmpiW(params, L"inlinenotitle")) return 2;
    if (!lstrcmpiW(params, L"inlineopen")) return 3;
    return 0;
}
static const WCHAR* item_folder_params_value(int index){
    if (index == 1) return L"inline";
    if (index == 2) return L"inlinenotitle";
    if (index == 3) return L"inlineopen";
    return L"";
}
static void item_set_folder_params(HWND dlg, const WCHAR* params){
    SendDlgItemMessageW(dlg, IDC_ITEM_FOLDER_PARAMS, CB_SETCURSEL,
                        item_folder_params_index(params), 0);
}
static void item_get_folder_params(HWND dlg, WCHAR* out, int cap){
    int sel = (int)SendDlgItemMessageW(dlg, IDC_ITEM_FOLDER_PARAMS, CB_GETCURSEL, 0, 0);
    lstrcpynW(out, item_folder_params_value(sel), cap);
}
static void item_cache_label_value(ItemEditCtx* ctx, ConfigItemType t, const WCHAR* label){
    int index;
    if (!ctx || !item_type_cache_index(t, &index)) return;
    lstrcpynW(ctx->labelByType[index], label ? label : L"", ARRAYSIZE(ctx->labelByType[index]));
    ctx->labelCached[index] = TRUE;
}
static void item_capture_label_for_type(HWND dlg, ItemEditCtx* ctx, ConfigItemType t){
    WCHAR label[128] = L"";
    if (!ctx) return;
    GetDlgItemTextW(dlg, IDC_ITEM_LABEL, label, ARRAYSIZE(label));
    item_cache_label_value(ctx, t, label);
}
static const WCHAR* item_cached_label_for_type(ItemEditCtx* ctx, ConfigItemType t){
    int index;
    if (!ctx || !item_type_cache_index(t, &index) || !ctx->labelCached[index]) return L"";
    return ctx->labelByType[index];
}
static void item_restore_label_for_type(HWND dlg, ItemEditCtx* ctx, ConfigItemType t){
    const WCHAR* label = item_cached_label_for_type(ctx, t);
    SetDlgItemTextW(dlg, IDC_ITEM_LABEL, label);
}
static void item_cache_path_value(ItemEditCtx* ctx, ConfigItemType t, const WCHAR* path){
    int index;
    if (!ctx || !item_type_cache_index(t, &index)) return;
    lstrcpynW(ctx->pathByType[index], path ? path : L"", ARRAYSIZE(ctx->pathByType[index]));
    ctx->pathCached[index] = TRUE;
}
static void item_capture_path_for_type(HWND dlg, ItemEditCtx* ctx, ConfigItemType t){
    WCHAR path[MAX_PATH] = L"";
    if (!ctx) return;
    GetDlgItemTextW(dlg, IDC_ITEM_PATH, path, ARRAYSIZE(path));
    item_cache_path_value(ctx, t, path);
}
static const WCHAR* item_cached_path_for_type(ItemEditCtx* ctx, ConfigItemType t){
    int index;
    if (!ctx || !item_type_cache_index(t, &index) || !ctx->pathCached[index]) return L"";
    return ctx->pathByType[index];
}
static void item_restore_path_for_type(HWND dlg, ItemEditCtx* ctx, ConfigItemType t){
    SetDlgItemTextW(dlg, IDC_ITEM_PATH, item_cached_path_for_type(ctx, t));
}
static void item_cache_params_value(ItemEditCtx* ctx, ConfigItemType t, const WCHAR* params){
    int index;
    if (!ctx || !item_type_cache_index(t, &index)) return;
    lstrcpynW(ctx->paramsByType[index], params ? params : L"", ARRAYSIZE(ctx->paramsByType[index]));
    ctx->paramsCached[index] = TRUE;
}
static void item_capture_params_for_type(HWND dlg, ItemEditCtx* ctx, ConfigItemType t){
    WCHAR params[256] = L"";
    if (!ctx) return;
    if (t == CI_FOLDER)
        item_get_folder_params(dlg, params, ARRAYSIZE(params));
    else
        GetDlgItemTextW(dlg, IDC_ITEM_PARAMS, params, ARRAYSIZE(params));
    item_cache_params_value(ctx, t, params);
}
static const WCHAR* item_cached_params_for_type(ItemEditCtx* ctx, ConfigItemType t){
    int index;
    if (!ctx || !item_type_cache_index(t, &index) || !ctx->paramsCached[index]) return L"";
    return ctx->paramsByType[index];
}
static void item_restore_params_for_type(HWND dlg, ItemEditCtx* ctx, ConfigItemType t){
    const WCHAR* params = item_cached_params_for_type(ctx, t);
    if (t == CI_FOLDER)
        item_set_folder_params(dlg, params);
    else
        SetDlgItemTextW(dlg, IDC_ITEM_PARAMS, params);
}
static void item_update_path_controls(HWND dlg, ConfigItemType t){
    BOOL showLabel, showPath, showParams;
    BOOL canBrowse = item_type_browse_uses_file(t) || item_type_browse_uses_folder(t);
    BOOL folderParams = (t == CI_FOLDER);
    item_type_visible_fields(t, &showLabel, &showPath, &showParams);
    ShowWindow(GetDlgItem(dlg,IDC_ITEM_LABEL_LABEL), showLabel ? SW_SHOW : SW_HIDE);
    ShowWindow(GetDlgItem(dlg,IDC_ITEM_LABEL), showLabel ? SW_SHOW : SW_HIDE);
    ShowWindow(GetDlgItem(dlg,IDC_ITEM_PATH_LABEL), showPath ? SW_SHOW : SW_HIDE);
    ShowWindow(GetDlgItem(dlg,IDC_ITEM_PATH), showPath ? SW_SHOW : SW_HIDE);
    ShowWindow(GetDlgItem(dlg,IDC_ITEM_PARAMS_LABEL), showParams ? SW_SHOW : SW_HIDE);
    ShowWindow(GetDlgItem(dlg,IDC_ITEM_PARAMS), (showParams && !folderParams) ? SW_SHOW : SW_HIDE);
    ShowWindow(GetDlgItem(dlg,IDC_ITEM_FOLDER_PARAMS), (showParams && folderParams) ? SW_SHOW : SW_HIDE);
    ShowWindow(GetDlgItem(dlg,IDC_ITEM_BROWSE), (showPath && canBrowse) ? SW_SHOW : SW_HIDE);
    SetDlgItemTextW(dlg, IDC_ITEM_PATH_LABEL,
                    t == CI_URI ? L"URI:" : t == CI_CMD ? L"Command:" : L"Path:");
    SetDlgItemTextW(dlg, IDC_ITEM_PARAMS_LABEL, folderParams ? L"Mode:" : L"Parameters:");
    EnableWindow(GetDlgItem(dlg,IDC_ITEM_LABEL),showLabel);
    EnableWindow(GetDlgItem(dlg,IDC_ITEM_PATH),showPath);
    EnableWindow(GetDlgItem(dlg,IDC_ITEM_PARAMS),showParams && !folderParams);
    EnableWindow(GetDlgItem(dlg,IDC_ITEM_FOLDER_PARAMS),showParams && folderParams);
    EnableWindow(GetDlgItem(dlg,IDC_ITEM_BROWSE),showPath && canBrowse);
    SetDlgItemTextW(dlg, IDC_ITEM_BROWSE, L"Browse");
}
static void item_fill_type_combo(HWND h){
    const struct { ConfigItemType t; const WCHAR* n; } types[]={
        {CI_CMD,L"Command"},
        {CI_URI,L"URI"},
        {CI_FILE,L"File"},
        {CI_FOLDER,L"Folder"},
        {CI_FOLDER_SUBMENU,L"Folder as a submenu"},
        {CI_HOME,L"Home"},
        {CI_RECENT_SUBMENU,L"Recent Items"},
        {CI_TASKKILL,L"Task Kill"},
        {CI_THISPC,L"This PC"},
        {CI_POWER_MENU,L"Power Menu"},
        {CI_POWER_HIBERNATE,L"Hibernate"},
        {CI_POWER_LOCK,L"Lock"},
        {CI_POWER_RESTART,L"Restart"},
        {CI_POWER_SHUTDOWN,L"Shut down"},
        {CI_POWER_LOGOFF,L"Sign out"},
        {CI_POWER_SLEEP,L"Sleep"},
        {CI_SEPARATOR,L"Separator"},
        {CI_SETTINGS,L"Menu Settings"}
    };
    for(int i=0;i< (int)(sizeof(types)/sizeof(types[0])); i++){
        int idx=(int)SendMessageW(h,CB_ADDSTRING,0,(LPARAM)types[i].n);
        SendMessageW(h,CB_SETITEMDATA,idx,types[i].t);
    }
}
static INT_PTR CALLBACK ItemEditDlg(HWND dlg, UINT msg, WPARAM wParam, LPARAM lParam){
    ItemEditCtx* ctx=(ItemEditCtx*)GetWindowLongPtrW(dlg,GWLP_USERDATA);
    switch(msg){
    case WM_INITDIALOG:{
        ctx=(ItemEditCtx*)lParam; SetWindowLongPtrW(dlg,GWLP_USERDATA,(LONG_PTR)ctx);
        HWND hType=GetDlgItem(dlg,IDC_ITEM_TYPE_COMBO); item_fill_type_combo(hType);
        item_fill_folder_params_combo(GetDlgItem(dlg, IDC_ITEM_FOLDER_PARAMS));
        // Preselect type
        int count=(int)SendMessageW(hType,CB_GETCOUNT,0,0); for(int i=0;i<count;i++){ if((ConfigItemType)SendMessageW(hType,CB_GETITEMDATA,i,0)==ctx->tmp.type){ SendMessageW(hType,CB_SETCURSEL,i,0); break; } }
        SetDlgItemTextW(dlg,IDC_ITEM_LABEL,ctx->tmp.label);
        SetDlgItemTextW(dlg,IDC_ITEM_PATH,ctx->tmp.path);
        SetDlgItemTextW(dlg,IDC_ITEM_PARAMS,ctx->tmp.params);
        item_set_folder_params(dlg, ctx->tmp.params);
        item_cache_label_value(ctx, ctx->tmp.type, ctx->tmp.label);
        item_cache_path_value(ctx, ctx->tmp.type, ctx->tmp.path);
        item_cache_params_value(ctx, ctx->tmp.type, ctx->tmp.params);
        ctx->displayedType = ctx->tmp.type;
        item_update_path_controls(dlg, ctx->tmp.type);
        return TRUE; }
    case WM_COMMAND:
        switch(LOWORD(wParam)){
        case IDC_ITEM_TYPE_COMBO:{
            if(HIWORD(wParam)==CBN_SELCHANGE){
                ConfigItemType selectedType = item_get_selected_type(dlg);
                item_capture_label_for_type(dlg, ctx, ctx->displayedType);
                item_capture_path_for_type(dlg, ctx, ctx->displayedType);
                item_capture_params_for_type(dlg, ctx, ctx->displayedType);
                ctx->displayedType = selectedType;
                item_restore_label_for_type(dlg, ctx, selectedType);
                item_restore_path_for_type(dlg, ctx, selectedType);
                item_restore_params_for_type(dlg, ctx, selectedType);
                item_update_path_controls(dlg, selectedType);
                RedrawWindow(dlg,NULL,NULL,RDW_INVALIDATE|RDW_UPDATENOW);
            }
            break;
        }
        case IDC_ITEM_BROWSE:{
            WCHAR picked[MAX_PATH] = {0};
            ConfigItemType t = item_get_selected_type(dlg);
            if (item_type_browse_uses_folder(t)) {
                if (item_browse_folder(dlg, picked, ARRAYSIZE(picked))) {
                    SetDlgItemTextW(dlg, IDC_ITEM_PATH, picked);
                }
            } else if (item_type_browse_uses_file(t)) {
                if (item_browse_file(dlg, picked, ARRAYSIZE(picked), t == CI_CMD)) {
                    SetDlgItemTextW(dlg, IDC_ITEM_PATH, picked);
                }
            }
            return TRUE;
        }
        case IDOK:{
            HWND hType=GetDlgItem(dlg,IDC_ITEM_TYPE_COMBO); int sel=(int)SendMessageW(hType,CB_GETCURSEL,0,0); if(sel>=0){ ctx->tmp.type=(ConfigItemType)SendMessageW(hType,CB_GETITEMDATA,sel,0);} else ctx->tmp.type=CI_FILE;
            BOOL showLabel, showPath, showParams;
            item_type_visible_fields(ctx->tmp.type, &showLabel, &showPath, &showParams);
            if (showLabel) {
                GetDlgItemTextW(dlg,IDC_ITEM_LABEL,ctx->tmp.label,ARRAYSIZE(ctx->tmp.label));
                item_cache_label_value(ctx, ctx->tmp.type, ctx->tmp.label);
            } else if (ctx->tmp.type == CI_RECENT_SUBMENU)
                lstrcpynW(ctx->tmp.label, item_cached_label_for_type(ctx, ctx->tmp.type), ARRAYSIZE(ctx->tmp.label));
            else if (ctx->editing && ctx->tmp.type == ctx->originalType)
                lstrcpynW(ctx->tmp.label, ctx->original.label, ARRAYSIZE(ctx->tmp.label));
            else
                ctx->tmp.label[0] = 0;
            if (showPath) {
                GetDlgItemTextW(dlg,IDC_ITEM_PATH,ctx->tmp.path,ARRAYSIZE(ctx->tmp.path));
                item_cache_path_value(ctx, ctx->tmp.type, ctx->tmp.path);
            } else if (ctx->editing && ctx->tmp.type == ctx->originalType)
                lstrcpynW(ctx->tmp.path, ctx->original.path, ARRAYSIZE(ctx->tmp.path));
            else
                ctx->tmp.path[0] = 0;
            if (showParams) {
                item_capture_params_for_type(dlg, ctx, ctx->tmp.type);
                lstrcpynW(ctx->tmp.params, item_cached_params_for_type(ctx, ctx->tmp.type), ARRAYSIZE(ctx->tmp.params));
            } else if (ctx->editing && ctx->tmp.type == ctx->originalType) {
                lstrcpynW(ctx->tmp.params, item_cached_params_for_type(ctx, ctx->tmp.type), ARRAYSIZE(ctx->tmp.params));
            } else {
                ctx->tmp.params[0] = 0;
            }
            // submenu flag is implied by CI_FOLDER_SUBMENU item type now; clear for others
            ctx->tmp.submenu = (ctx->tmp.type==CI_FOLDER_SUBMENU);
            // Basic validation
            if(ctx->tmp.type==CI_FILE||ctx->tmp.type==CI_CMD||ctx->tmp.type==CI_FOLDER){ if(!ctx->tmp.path[0]){ MessageBoxW(dlg,L"Path required for this type",L"Validation",MB_ICONWARNING); return TRUE; } }
            // URI validation relaxed: allow arbitrary text (user may supply custom handler forms)
            EndDialog(dlg,IDOK); return TRUE; }
        case IDCANCEL: EndDialog(dlg,IDCANCEL); return TRUE; }
        break;
    }
    return FALSE;
}

static BOOL edit_item_modal(HWND parent, ConfigItem* outItem, BOOL editing){
    ItemEditCtx ctx; ZeroMemory(&ctx,sizeof(ctx));
    if(outItem) {
        ctx.tmp=*outItem;
        ctx.original=*outItem;
    } else {
        ctx.tmp.type=CI_FILE;
        ctx.original.type=CI_FILE;
    }
    ctx.originalType=ctx.tmp.type;
    ctx.editing=editing;
    INT_PTR r=DialogBoxParamW(GetModuleHandleW(NULL),MAKEINTRESOURCEW(IDD_ITEM_EDIT),parent,ItemEditDlg,(LPARAM)&ctx);
    if(r==IDOK){
        if(outItem) *outItem=ctx.tmp;
        return TRUE;
    }
    return FALSE;
}

// ---------------- Menu actions -----------------
static int menu_get_selected_index(HWND lv){ int sel=(int)SendMessageW(lv,LVM_GETNEXTITEM,(WPARAM)-1,LVNI_SELECTED); if(sel<0) return -1; LVITEMW li; ZeroMemory(&li,sizeof(li)); li.iItem=sel; li.mask=LVIF_PARAM; if(SendMessageW(lv,LVM_GETITEM,0,(LPARAM)&li)) return (int)li.lParam; return -1; }
static void menu_action_add(SettingsState* st, HWND pg){ if(!st) return; if(st->workingCount>=64){ MessageBoxW(pg,L"Maximum items reached",L"Menu",MB_ICONINFORMATION); return; } ConfigItem ni; ZeroMemory(&ni,sizeof(ni)); ni.type=CI_FILE; if(edit_item_modal(pg,&ni,FALSE)){ st->workingItems[st->workingCount++]=ni; st->workingDirty=TRUE; refresh_lists(st);} }
static void menu_action_edit(SettingsState* st, HWND pg){ if(!st) return; HWND lv=GetDlgItem(pg,IDC_MENU_LIST); int idx=menu_get_selected_index(lv); if(idx<0||idx>=st->workingCount) return; ConfigItem tmp=st->workingItems[idx]; if(edit_item_modal(pg,&tmp,TRUE)){ st->workingItems[idx]=tmp; st->workingDirty=TRUE; refresh_lists(st);} }
static void menu_action_delete(SettingsState* st, HWND pg){ if(!st) return; HWND lv=GetDlgItem(pg,IDC_MENU_LIST); int idx=menu_get_selected_index(lv); if(idx<0||idx>=st->workingCount) return; const ConfigItem* item=&st->workingItems[idx]; WCHAR body[2048]; if(item->type==CI_SEPARATOR){StringCchPrintfW(body, ARRAYSIZE(body),L"Are you sure that you want to delete this item?\r\n\r\n#%d - Separator",idx+1);}else{const WCHAR* display=item->label[0]?item->label:(item->name[0]?item->name:item_type_name(item->type)); StringCchPrintfW(body, ARRAYSIZE(body),L"Are you sure that you want to delete this item?\r\n\r\n#%d - %s\r\nItem type: %s%s%s%s%s",idx+1,display,item_type_name(item->type),item->path[0]?L"\r\nPath: ":L"",item->path[0]?item->path:L"",item->params[0]?L"\r\nParameters: ":L"",item->params[0]?item->params:L"");} if(MessageBoxW(pg,body,L"Confirm Deletion",MB_ICONQUESTION|MB_YESNO)!=IDYES) return; for(int i=idx;i<st->workingCount-1;i++) st->workingItems[i]=st->workingItems[i+1]; st->workingCount--; st->workingDirty=TRUE; refresh_lists(st); }
static void menu_action_move(SettingsState* st, HWND pg, int dir){ if(!st) return; HWND lv=GetDlgItem(pg,IDC_MENU_LIST); int idx=menu_get_selected_index(lv); if(idx<0) return; int ni=idx+dir; if(ni<0||ni>=st->workingCount) return; ConfigItem t=st->workingItems[idx]; st->workingItems[idx]=st->workingItems[ni]; st->workingItems[ni]=t; st->workingDirty=TRUE; refresh_lists(st); // reselect moved item
    HWND lv2=GetDlgItem(pg,IDC_MENU_LIST); int rowCount=(int)SendMessageW(lv2,LVM_GETITEMCOUNT,0,0); for(int r=0;r<rowCount;r++){ ListView_SetItemState(lv2,r,0,LVIS_SELECTED); }
    // Find new visual row with lParam==ni
    for(int r=0;r<rowCount;r++){ LVITEMW li; ZeroMemory(&li,sizeof(li)); li.iItem=r; li.mask=LVIF_PARAM; if(SendMessageW(lv2,LVM_GETITEM,0,(LPARAM)&li) && li.lParam==ni){ ListView_SetItemState(lv2,r,LVIS_SELECTED,LVIS_SELECTED); break; } }
}

// ---------------- Icons actions -----------------
static BOOL browse_icon(HWND owner, WCHAR* out, int cap){ OPENFILENAMEW ofn; ZeroMemory(&ofn,sizeof(ofn)); ofn.lStructSize=sizeof(ofn); ofn.hwndOwner=owner; ofn.lpstrFilter=L"Icons (*.ico)\0*.ico\0All Files (*.*)\0*.*\0"; ofn.nFilterIndex=1; ofn.lpstrFile=out; ofn.nMaxFile=cap; ofn.Flags=OFN_PATHMUSTEXIST|OFN_FILEMUSTEXIST; ofn.lpstrTitle=L"Select Icon"; return GetOpenFileNameW(&ofn); }
static void icons_action_browse(SettingsState* st, HWND pg, int which){ if(!st) return; HWND lv=GetDlgItem(pg,IDC_ICONS_LIST); int idx=icons_get_selected_index(lv); if(idx<0||idx>=st->workingCount) return; ConfigItem* it=&st->workingItems[idx]; WCHAR path[MAX_PATH]={0}; if(browse_icon(pg,path,ARRAYSIZE(path))){ if(which==0) lstrcpynW(it->iconPath,path,ARRAYSIZE(it->iconPath)); else if(which==1) lstrcpynW(it->iconPathLight,path,ARRAYSIZE(it->iconPathLight)); else if(which==2) lstrcpynW(it->iconPathDark,path,ARRAYSIZE(it->iconPathDark)); st->workingDirty=TRUE; refresh_lists(st);} }
static void icons_action_clear(SettingsState* st, HWND pg){ if(!st) return; HWND lv=GetDlgItem(pg,IDC_ICONS_LIST); int idx=icons_get_selected_index(lv); if(idx<0||idx>=st->workingCount) return; ConfigItem* it=&st->workingItems[idx]; it->iconPath[0]=0; it->iconPathLight[0]=0; it->iconPathDark[0]=0; st->workingDirty=TRUE; refresh_lists(st); }

// ---------------- Working copy helpers (restored) -----------------
static void working_clone(SettingsState* st){
    if(!st||!st->cfg) return;
    st->workingCount = st->cfg->count;
    if(st->workingCount>64) st->workingCount=64;
    for(int i=0;i<st->workingCount;i++) st->workingItems[i]=st->cfg->items[i];
    st->workingDirty=FALSE;
}
static void working_commit(SettingsState* st){
    if(!st||!st->cfg) return;
    if(st->workingCount<0) st->workingCount=0;
    if(st->workingCount>64) st->workingCount=64;
    st->cfg->count=st->workingCount;
    for(int i=0;i<st->workingCount;i++) st->cfg->items[i]=st->workingItems[i];
    st->workingDirty=FALSE;
}
// Enable/disable Menu buttons (Add always enabled; others depend on selection & position)
static void menu_update_buttons(SettingsState* st, HWND page){
    if(!st||!page) return;
    HWND lv=GetDlgItem(page,IDC_MENU_LIST); if(!lv) return;
    int idx=menu_get_selected_index(lv);
    int count=st->workingCount;
    BOOL hasSel=(idx>=0 && idx<count);
    EnableWindow(GetDlgItem(page,IDC_MENU_ADD), TRUE); // always enabled
    EnableWindow(GetDlgItem(page,IDC_MENU_EDIT), hasSel);
    EnableWindow(GetDlgItem(page,IDC_MENU_DELETE), hasSel);
    EnableWindow(GetDlgItem(page,IDC_MENU_UP), hasSel && idx>0);
    EnableWindow(GetDlgItem(page,IDC_MENU_DOWN), hasSel && idx < (count-1));
}
// Refresh both list views after working copy changes
static void refresh_lists(SettingsState* st){
    if(!st) return;
    if(st->pages[PAGE_MENU]){ Menu_Load(st->pages[PAGE_MENU],st->cfg); menu_update_buttons(st, st->pages[PAGE_MENU]); }
    if(st->pages[PAGE_ICONS]){ Icons_Load(st->pages[PAGE_ICONS],st->cfg); icons_update_buttons(st, st->pages[PAGE_ICONS]); }
}

static void layout_pages(SettingsState* st, int x, int y, int w, int h){
    if(!st) return;
    for(int i=0; i<PAGE_COUNT; i++){
        if(!st->pages[i]) continue;
        SetWindowPos(st->pages[i], NULL, x, y, w, h, SWP_NOZORDER);
        relayout_page(st->pages[i], w, h);
    }
}

static void relayout_fixed_page_groupboxes(HWND page, int pageWidth){
    if(!page) return;
    const int sideGap = 15;
    int groupWidth = pageWidth - (sideGap * 2);
    if(groupWidth < 0) groupWidth = 0;

    for(HWND child = GetWindow(page, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)){
        WCHAR className[16] = {0};
        if(!GetClassNameW(child, className, ARRAYSIZE(className))) continue;
        if(lstrcmpiW(className, L"Button") != 0) continue;

        LONG_PTR style = GetWindowLongPtrW(child, GWL_STYLE);
        if((style & BS_TYPEMASK) != BS_GROUPBOX) continue;

        WCHAR title[32] = {0};
        GetWindowTextW(child, title, ARRAYSIZE(title));
        if(!lstrcmpW(title, L"This PC") || !lstrcmpW(title, L"Home") || !lstrcmpW(title, L"Power Menu"))
            continue;

        RECT rc;
        GetWindowRect(child, &rc);
        MapWindowPoints(NULL, page, (POINT*)&rc, 2);
        SetWindowPos(child, NULL, sideGap, rc.top, groupWidth, rc.bottom - rc.top, SWP_NOZORDER);
    }
}


static void update_tab_item_widths(SettingsState* st){
    if(!st || !st->hTabs) return;
    int count = TabCtrl_GetItemCount(st->hTabs);
    if(count <= 0) return;

    RECT client;
    GetClientRect(st->hTabs, &client);
    RECT display = client;
    TabCtrl_AdjustRect(st->hTabs, FALSE, &display);

    RECT itemRc;
    int height = 0;
    if(TabCtrl_GetItemRect(st->hTabs, 0, &itemRc))
        height = itemRc.bottom - itemRc.top;
    if(height < 1) height = 20;

    int width = (display.right - display.left) / count;
    if(width < 24) width = 24;

    for(int i = 0; i < 3; ++i){
        SendMessageW(st->hTabs, TCM_SETITEMSIZE, 0, MAKELPARAM(width, height));
        RECT lastRc;
        if(!TabCtrl_GetItemRect(st->hTabs, count - 1, &lastRc)) break;
        int delta = lastRc.right - display.right;
        if(delta == 0) break;
        int step = delta / count;
        if(step == 0) step = (delta > 0) ? 1 : -1;
        width -= step;
        if(width < 24){ width = 24; break; }
    }

    width += 0;
    SendMessageW(st->hTabs, TCM_SETITEMSIZE, 0, MAKELPARAM(width, height));
    InvalidateRect(st->hTabs, NULL, TRUE);
}
static void init_tabs(HWND dlg, SettingsState* st){
    st->hTabs=GetDlgItem(dlg,IDC_SETTINGS_TABS);
    if(st->hTabs) SetWindowLongPtrW(st->hTabs, GWL_STYLE, (GetWindowLongPtrW(st->hTabs, GWL_STYLE) | TCS_FIXEDWIDTH) & ~TCS_FORCEICONLEFT & ~TCS_FORCELABELLEFT);
    TCITEMW ti; ZeroMemory(&ti,sizeof(ti)); ti.mask=TCIF_TEXT; WCHAR label[32];
    const WCHAR* names[] = {L"General",L"Placement",L"Menu",L"Icons",L"Controls",L"Advanced"};
    for(int i=0;i<(int)ARRAYSIZE(names);i++){
        lstrcpynW(label,names[i],ARRAYSIZE(label));
        ti.pszText=label;
        TabCtrl_InsertItem(st->hTabs,i,&ti);
    }
    update_tab_item_widths(st);

    RECT rcClient; GetClientRect(st->hTabs,&rcClient); // client of tab control (0,0 origin)
    RECT rcDisplay = rcClient; TabCtrl_AdjustRect(st->hTabs,FALSE,&rcDisplay); // area for pages (still tab-local)

    // Map to dialog coordinates.
    MapWindowPoints(st->hTabs, dlg, (POINT*)&rcDisplay, 2);

    const int inset = 0;
    int x = rcDisplay.left + inset;
    int y = rcDisplay.top + inset;
    int w = (rcDisplay.right - rcDisplay.left) - (inset * 2);
    int h = (rcDisplay.bottom - rcDisplay.top) - (inset * 2);
    if(w < 0) w = 0;
    if(h < 0) h = 0;

    st->pages[PAGE_GENERAL]=CreateDialogParamW(GetModuleHandleW(NULL),MAKEINTRESOURCEW(IDD_PAGE_GENERAL),dlg,PageDlgProc,(LPARAM)st);
    st->pages[PAGE_PLACEMENT]=CreateDialogParamW(GetModuleHandleW(NULL),MAKEINTRESOURCEW(IDD_PAGE_PLACEMENT),dlg,PageDlgProc,(LPARAM)st);
    st->pages[PAGE_MENU]=CreateDialogParamW(GetModuleHandleW(NULL),MAKEINTRESOURCEW(IDD_PAGE_MENU),dlg,PageDlgProc,(LPARAM)st);
    st->pages[PAGE_ICONS]=CreateDialogParamW(GetModuleHandleW(NULL),MAKEINTRESOURCEW(IDD_PAGE_ICONS),dlg,PageDlgProc,(LPARAM)st);
    st->pages[PAGE_SORTING]=CreateDialogParamW(GetModuleHandleW(NULL),MAKEINTRESOURCEW(IDD_PAGE_SORTING),dlg,PageDlgProc,(LPARAM)st);
    st->pages[PAGE_CONTROLS]=CreateDialogParamW(GetModuleHandleW(NULL),MAKEINTRESOURCEW(IDD_PAGE_CONTROLS),dlg,PageDlgProc,(LPARAM)st);
    st->pages[PAGE_ADVANCED]=CreateDialogParamW(GetModuleHandleW(NULL),MAKEINTRESOURCEW(IDD_PAGE_ADVANCED),dlg,PageDlgProc,(LPARAM)st);

    layout_pages(st, x, y, w, h);
    show_page(st, PAGE_GENERAL);

    // Attach st pointer to parent dialog for child retrieval (used in loads)
    SetWindowLongPtrW(dlg,GWLP_USERDATA,(LONG_PTR)st);
    {
        HWND lvMenu = GetDlgItem(st->pages[PAGE_MENU], IDC_MENU_LIST);
        if(lvMenu) SetWindowSubclass(lvMenu, MenuListSubclassProc, 1, (DWORD_PTR)st);
    }
    working_clone(st);
    General_Load(st->pages[PAGE_GENERAL],st->cfg);
    Placement_Load(st->pages[PAGE_PLACEMENT],st->cfg);
    Menu_Load(st->pages[PAGE_MENU],st->cfg);
    Icons_Load(st->pages[PAGE_ICONS],st->cfg);
    Sorting_Load(st->pages[PAGE_GENERAL],st->cfg);
    Sorting_Load(st->pages[PAGE_SORTING],st->cfg);
    Controls_Load(st->pages[PAGE_CONTROLS],st->cfg);
    Advanced_Load(st->pages[PAGE_ADVANCED],st->cfg);
    update_apply_button_state(st, dlg);
}
// Re-layout controls inside a page (currently only Menu & Icons) when page resized
static void relayout_page(HWND page, int w, int h){
    if(!page) return;
    // Determine which page by child IDs present
    HWND lvMenu=GetDlgItem(page,IDC_MENU_LIST);
    if(lvMenu){
        // Keep Menu geometry aligned with Icons: same list width and button width.
        int margin=6; int btnSpacing=4; int btnPad=14; // horizontal padding inside button text area
        const int btnIds[]={IDC_MENU_ADD,IDC_MENU_EDIT,IDC_MENU_DELETE,IDC_MENU_UP,IDC_MENU_DOWN};
        int btnWidth=82; // minimum; +12 vs previous menu baseline to match Icons page
        HDC hdc=GetDC(page); HFONT hf=(HFONT)SendMessageW(page,WM_GETFONT,0,0); HFONT of=(HFONT)SelectObject(hdc,hf);
        for(int i=0;i< (int)(sizeof(btnIds)/sizeof(btnIds[0])); i++){ HWND b=GetDlgItem(page,btnIds[i]); if(!b) continue; WCHAR txt[64]; if(GetWindowTextW(b,txt,ARRAYSIZE(txt))){ SIZE sz; if(GetTextExtentPoint32W(hdc,txt,lstrlenW(txt),&sz)){ int wCandidate=sz.cx+btnPad; if(wCandidate>btnWidth) btnWidth=wCandidate; } } }
        SelectObject(hdc,of); ReleaseDC(page,hdc);
        if(btnWidth>140) btnWidth=140; // cap
        int listRightPadding= (btnWidth + 2*margin);
        int listW = w - margin - listRightPadding; if(listW<80) listW=80;
        int listH = h - 2*margin; if(listH<40) listH=40;
        SetWindowPos(lvMenu,NULL,6,10,listW,listH,SWP_NOZORDER);
    int bx = 6 + listW + margin; // align buttons just to right of list
    int y = 10;
    int bh=28; // enlarged: resource 20 + extra vertical padding
        HWND b;
        int vgap=6; int groupGap=10;
    b=GetDlgItem(page,IDC_MENU_ADD); if(b) SetWindowPos(b,NULL,bx,y,btnWidth,bh,SWP_NOZORDER); y+=bh+vgap;
        b=GetDlgItem(page,IDC_MENU_EDIT); if(b) SetWindowPos(b,NULL,bx,y,btnWidth,bh,SWP_NOZORDER); y+=bh+vgap;
        b=GetDlgItem(page,IDC_MENU_DELETE); if(b) SetWindowPos(b,NULL,bx,y,btnWidth,bh,SWP_NOZORDER); y+=bh+groupGap;
        b=GetDlgItem(page,IDC_MENU_UP); if(b) SetWindowPos(b,NULL,bx,y,btnWidth,bh,SWP_NOZORDER); y+=bh+vgap;
        b=GetDlgItem(page,IDC_MENU_DOWN); if(b) SetWindowPos(b,NULL,bx,y,btnWidth,bh,SWP_NOZORDER);
        return;
    }
    HWND lvIcons=GetDlgItem(page,IDC_ICONS_LIST);
    if(lvIcons){
        int margin=6; int btnPad=14; int btnSpacing=6; const int btnIds[]={IDC_ICON_BROWSE_FILE,IDC_ICON_BROWSE_LIGHT,IDC_ICON_BROWSE_DARK,IDC_ICON_CLEAR};
        int btnWidth=82; HDC hdc=GetDC(page); HFONT hf=(HFONT)SendMessageW(page,WM_GETFONT,0,0); HFONT of=(HFONT)SelectObject(hdc,hf); for(int i=0;i< (int)(sizeof(btnIds)/sizeof(btnIds[0])); i++){ HWND b=GetDlgItem(page,btnIds[i]); if(!b) continue; WCHAR txt[64]; if(GetWindowTextW(b,txt,ARRAYSIZE(txt))){ SIZE sz; if(GetTextExtentPoint32W(hdc,txt,lstrlenW(txt),&sz)){ int wCandidate=sz.cx+btnPad; if(wCandidate>btnWidth) btnWidth=wCandidate; } } } SelectObject(hdc,of); ReleaseDC(page,hdc); if(btnWidth>160) btnWidth=160; int listRightPadding=(btnWidth + 2*margin);
        int listW = w - margin - listRightPadding; if(listW<80) listW=80;
        int listH = h - 2*margin; if(listH<40) listH=40;
        SetWindowPos(lvIcons,NULL,6,10,listW,listH,SWP_NOZORDER);
    int bx=6 + listW + margin; int y=10; int bh=28; HWND b;
        b=GetDlgItem(page,IDC_ICON_BROWSE_FILE); if(b) SetWindowPos(b,NULL,bx,y,btnWidth,bh,SWP_NOZORDER); y+=bh+btnSpacing;
        b=GetDlgItem(page,IDC_ICON_BROWSE_LIGHT); if(b) SetWindowPos(b,NULL,bx,y,btnWidth,bh,SWP_NOZORDER); y+=bh+btnSpacing;
        b=GetDlgItem(page,IDC_ICON_BROWSE_DARK); if(b) SetWindowPos(b,NULL,bx,y,btnWidth,bh,SWP_NOZORDER); y+=bh+btnSpacing;
        b=GetDlgItem(page,IDC_ICON_CLEAR); if(b) SetWindowPos(b,NULL,bx,y,btnWidth,bh,SWP_NOZORDER);
        return;
    }

    relayout_fixed_page_groupboxes(page, w);
}
static void show_page(SettingsState* st,int idx){ for(int i=0;i<PAGE_COUNT;i++){ if(st->pages[i]) ShowWindow(st->pages[i], i==idx?SW_SHOW:SW_HIDE); } }
static BOOL save_all(SettingsState* st) {
    if (!st || !st->cfg) return FALSE;
    Config* original = st->cfg;
    Config* draft = (Config*)malloc(sizeof(*draft));
    if (!draft) return FALSE;
    *draft = *original;
    BOOL dirty = st->workingDirty;
    BOOL oldReload = st->reloadNeeded;
    BOOL success = FALSE;
    if (!settings_config_stamp_matches(st)) {
        int answer = MessageBoxW(GetParent(st->pages[PAGE_GENERAL]),
            L"The configuration file changed on disk while Settings was open.\r\n\r\nSave anyway and overwrite the newer file?",
            L"WinMac Menu", MB_ICONWARNING | MB_YESNO | MB_DEFBUTTON2);
        if (answer != IDYES) goto failed_silent;
    }
    if (!ini_transaction_begin(&g_saveTransaction, original->iniPath)) goto failed;
    lstrcpynW(draft->iniPath, g_saveTransaction.temporary, ARRAYSIZE(draft->iniPath));
    st->cfg = draft;
    if (st->workingDirty) working_commit(st);
    int generalResult = General_Save(st->pages[PAGE_GENERAL], draft);
    Sorting_Save(st->pages[PAGE_GENERAL], draft);
    Placement_Save(st->pages[PAGE_PLACEMENT], draft);
    Controls_Save(st->pages[PAGE_CONTROLS], draft);
    Advanced_Save(st->pages[PAGE_ADVANCED], draft);
    Menu_Save(st->pages[PAGE_MENU], draft);
    Icons_Save(st->pages[PAGE_ICONS], draft);
    if (!ini_transaction_commit(&g_saveTransaction)) goto failed;
    lstrcpynW(draft->iniPath, original->iniPath, ARRAYSIZE(draft->iniPath));
    apply_saved_runtime_settings(original, draft);
    *original = *draft;
    if (generalResult == 2) st->reloadNeeded = TRUE;
    config_load(original);
    settings_get_config_stamp(original->iniPath, &st->loadedConfigWriteTime, &st->loadedConfigSize);
    RefreshWinUI3MenuConfig(original);
    success = TRUE;
failed:
    st->cfg = original;
    if (!success) {
        st->workingDirty = dirty;
        st->reloadNeeded = oldReload;
        MessageBoxW(GetParent(st->pages[PAGE_GENERAL]),
            L"Could not save the configuration. Check that the file and folder are writable. Your edits are still available in this dialog.",
            L"WinMac Menu", MB_OK | MB_ICONERROR);
    }
    ini_transaction_discard(&g_saveTransaction);
    free(draft);
    return success;
failed_silent:
    st->cfg = original;
    st->workingDirty = dirty;
    st->reloadNeeded = oldReload;
    ini_transaction_discard(&g_saveTransaction);
    free(draft);
    return FALSE;
}

static INT_PTR CALLBACK MainDlgProc(HWND dlg, UINT msg, WPARAM wParam, LPARAM lParam){
    static SettingsState* st = NULL;
    switch (msg) {
        case WM_GETMINMAXINFO:
            if (st) {
                MINMAXINFO* mmi = (MINMAXINFO*)lParam;
                mmi->ptMinTrackSize.x = st->baseW;
                mmi->ptMinTrackSize.y = st->baseH;
            }
            break;
        case WM_SYSCOMMAND:
            if ((wParam & 0xFFF0) == SC_CLOSE) {
                EndDialog(dlg, IDCANCEL);
                return TRUE;
            }
            break;
        case WM_CLOSE:
            EndDialog(dlg, IDCANCEL);
            return TRUE;
        case WM_SETCURSOR:
            if (st && st->dragging) {
                SetCursor(NULL);
                return TRUE;
            }
            break;
        case WM_INITDIALOG:
            g_activeSettingsDlg = dlg;
            st = (SettingsState*)calloc(1, sizeof(SettingsState));
            st->cfg = (Config*)lParam;
            st->dragging = FALSE;
            st->dragSource = -1;
            // Store original power option states
            st->origExcludeSleep = st->cfg->excludeSleep;
            st->origExcludeHibernate = st->cfg->excludeHibernate;
            st->origExcludeShutdown = st->cfg->excludeShutdown;
            st->origExcludeRestart = st->cfg->excludeRestart;
            st->origExcludeLock = st->cfg->excludeLock;
            st->origExcludeLogoff = st->cfg->excludeLogoff;
            settings_get_config_stamp(st->cfg->iniPath, &st->loadedConfigWriteTime, &st->loadedConfigSize);
            {
                RECT rc; GetWindowRect(dlg, &rc); st->baseW = rc.right - rc.left; st->baseH = rc.bottom - rc.top;
                LONG_PTR ex = GetWindowLongPtrW(dlg, GWL_EXSTYLE); SetWindowLongPtrW(dlg, GWL_EXSTYLE, ex | WS_EX_APPWINDOW);
            }
            apply_dialog_icon(dlg);
            apply_settings_title(dlg, st->cfg);
            init_tabs(dlg, st);
            { RECT client; GetClientRect(dlg, &client); SendMessageW(dlg, WM_SIZE, 0, MAKELPARAM(client.right, client.bottom)); }
            // Center window manually (DS_CENTER removed). Use work area.
            {
                RECT rc; GetWindowRect(dlg, &rc); RECT wa; SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
                int w = rc.right - rc.left; int h = rc.bottom - rc.top;
                int x = wa.left + ((wa.right - wa.left) - w) / 2; int y = wa.top + ((wa.bottom - wa.top) - h) / 2;
                SetWindowPos(dlg, NULL, x, y, 0, 0, SWP_NOZORDER | SWP_NOSIZE);
            }
            return TRUE;
        case WM_DESTROY:
            if (g_activeSettingsDlg == dlg) g_activeSettingsDlg = NULL;
            break;
        case WM_SIZE:
            if (st) {
                int cw = LOWORD(lParam); int ch = HIWORD(lParam);
                // Updates on the left; save/exit actions on the right.
                HWND hUpdates = GetDlgItem(dlg, IDC_CHECK_UPDATES);
                HWND hApply = GetDlgItem(dlg, IDC_APPLY);
                HWND hSave = GetDlgItem(dlg, IDC_SAVEEXIT);
                HWND hExit = GetDlgItem(dlg, IDC_EXIT);
                HWND hCancel = GetDlgItem(dlg, IDC_CANCEL);
                RECT rb; GetWindowRect(hApply, &rb); MapWindowPoints(NULL, dlg, (POINT*)&rb, 2); int btnH = rb.bottom - rb.top; int btnW = rb.right - rb.left;
                int margin = 6; int gap = 4;
                RECT rcBtn;
                int cancelW, cancelH; GetWindowRect(hCancel, &rcBtn); MapWindowPoints(NULL, dlg, (POINT*)&rcBtn, 2); cancelW = rcBtn.right - rcBtn.left; cancelH = rcBtn.bottom - rcBtn.top;
                int exitW, exitH; GetWindowRect(hExit, &rcBtn); MapWindowPoints(NULL, dlg, (POINT*)&rcBtn, 2); exitW = rcBtn.right - rcBtn.left; exitH = rcBtn.bottom - rcBtn.top;
                int saveW, saveH; GetWindowRect(hSave, &rcBtn); MapWindowPoints(NULL, dlg, (POINT*)&rcBtn, 2); saveW = rcBtn.right - rcBtn.left; saveH = rcBtn.bottom - rcBtn.top;
                int applyW, applyH; GetWindowRect(hApply, &rcBtn); MapWindowPoints(NULL, dlg, (POINT*)&rcBtn, 2); applyW = rcBtn.right - rcBtn.left; applyH = rcBtn.bottom - rcBtn.top;
                int btnY = ch - margin - btnH;
                int xUpdates = margin - 1;
                int xCancel = cw - margin - cancelW;
                int xExit = xCancel - gap - exitW;
                int xSave = xExit - gap - saveW;
                int xApply = xSave - gap - applyW;
                SetWindowPos(hApply, NULL, xApply, btnY, 0, 0, SWP_NOZORDER | SWP_NOSIZE);
                SetWindowPos(hSave, NULL, xSave, btnY, 0, 0, SWP_NOZORDER | SWP_NOSIZE);
                SetWindowPos(hUpdates, NULL, xUpdates, btnY, 0, 0, SWP_NOZORDER | SWP_NOSIZE);
                SetWindowPos(hExit, NULL, xExit, btnY, 0, 0, SWP_NOZORDER | SWP_NOSIZE);
                SetWindowPos(hCancel, NULL, xCancel, btnY, 0, 0, SWP_NOZORDER | SWP_NOSIZE);
                // Resize tab control
                HWND hTabs = st->hTabs; if (hTabs) {
                    RECT rTabs; GetWindowRect(hTabs, &rTabs); MapWindowPoints(NULL, dlg, (POINT*)&rTabs, 2);
                    int tabsX = rTabs.left; int tabsY = rTabs.top; // keep original top-left
                    int tabsW = cw - tabsX - margin + 1;
                    int tabsH = btnY - tabsY - margin;
                    if (tabsW < 100) tabsW = 100; if (tabsH < 100) tabsH = 100;
                    SetWindowPos(hTabs, NULL, tabsX, tabsY, tabsW, tabsH, SWP_NOZORDER);
                    update_tab_item_widths(st);
                    // Adjust pages to new tab display area
                    RECT rcClient; GetClientRect(hTabs, &rcClient); RECT rcDisplay = rcClient; TabCtrl_AdjustRect(hTabs, FALSE, &rcDisplay); MapWindowPoints(hTabs, dlg, (POINT*)&rcDisplay, 2);
                    const int inset = 0;
                    int x = rcDisplay.left + inset;
                    int y = rcDisplay.top + inset;
                    int w = (rcDisplay.right - rcDisplay.left) - (inset * 2);
                    int h = (rcDisplay.bottom - rcDisplay.top) - (inset * 2);
                    if (w < 0) w = 0;
                    if (h < 0) h = 0;
                    layout_pages(st, x, y, w, h);
                }
            }
            return 0;
        case WM_NOTIFY: {
            LPNMHDR nh = (LPNMHDR)lParam;
            if ((nh->idFrom == IDC_MENU_LIST || nh->idFrom == IDC_ICONS_LIST) && nh->code == NM_CUSTOMDRAW) {
                return settings_draw_list_selection((const NMLVCUSTOMDRAW*)lParam);
            }
            if (nh->idFrom == IDC_SETTINGS_TABS && nh->code == TCN_SELCHANGE) {
                int sel = TabCtrl_GetCurSel(st->hTabs);
                int page = (sel >= PAGE_SORTING) ? sel + 1 : sel;
                show_page(st, page);
                if (page == PAGE_MENU) menu_update_buttons(st, st->pages[PAGE_MENU]);
                if (page == PAGE_ICONS) icons_update_buttons(st, st->pages[PAGE_ICONS]);
            }
            // Live selection changes in Icons list
            if (nh->idFrom == IDC_ICONS_LIST && nh->code == LVN_ITEMCHANGED) {
                LPNMLISTVIEW lv = (LPNMLISTVIEW)nh; if ((lv->uChanged & LVIF_STATE) && ((lv->uNewState ^ lv->uOldState) & LVIS_SELECTED)) {
                    icons_update_buttons(st, st->pages[PAGE_ICONS]);
                }
            }
            // Live selection changes in Menu list
            if (nh->idFrom == IDC_MENU_LIST && nh->code == LVN_ITEMCHANGED) {
                LPNMLISTVIEW lv = (LPNMLISTVIEW)nh; if ((lv->uChanged & LVIF_STATE) && ((lv->uNewState ^ lv->uOldState) & LVIS_SELECTED)) {
                    menu_update_buttons(st, st->pages[PAGE_MENU]);
                }
            }
            // Let the native list view determine when a pointer gesture becomes a drag.
            // The list-view subclass then owns capture, feedback, and the single commit.
            if (nh->idFrom == IDC_MENU_LIST && nh->code == LVN_BEGINDRAG) {
                LPNMLISTVIEW lvn = (LPNMLISTVIEW)nh;
                HWND lv = GetDlgItem(st->pages[PAGE_MENU], IDC_MENU_LIST);
                if(lv) menu_drag_begin(st, lv, lvn->iItem, lvn->ptAction);
                return TRUE;
            }
            break;
        }
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDC_APPLY:
                    save_all(st);
                    update_apply_button_state(st, dlg);
                    // Do NOT reload app on Apply for tray icon changes
                    return TRUE;
                case IDC_SAVEEXIT:
                    if (!save_all(st)) return TRUE;
                    // Only reload app if tray icon settings changed and Save & Close was clicked
                    if (st && st->reloadNeeded && g_settingsOwnerHwnd) {
                        PostMessageW(g_settingsOwnerHwnd, WM_COMMAND, 10010, 0);
                    }
                    EndDialog(dlg, IDOK);
                    return TRUE;
                case IDC_CHECK_UPDATES:
                    ShowUpdateCheck(dlg);
                    return TRUE;
                case IDC_EXIT:
                    if (g_settingsOwnerHwnd) {
                        PostMessageW(g_settingsOwnerHwnd, WM_CLOSE, 0, 0);
                    }
                    EndDialog(dlg, IDCANCEL);
                    return TRUE;
                case IDC_CANCEL: EndDialog(dlg, IDCANCEL); return TRUE;
                case IDC_OPEN_CONFIG_FOLDER: {
                    if (st && st->cfg && st->cfg->iniPath[0]) {
                        WCHAR folder[MAX_PATH]; lstrcpynW(folder, st->cfg->iniPath, ARRAYSIZE(folder));
                        PathRemoveFileSpecW(folder);
                        if (folder[0]) ShellExecuteW(dlg, L"open", folder, NULL, NULL, SW_SHOWNORMAL);
                    }
                    return TRUE;
                }
                case IDC_MENU_ADD: menu_action_add(st, st->pages[PAGE_MENU]); update_apply_button_state(st, dlg); return TRUE;
                case IDC_MENU_EDIT: menu_action_edit(st, st->pages[PAGE_MENU]); update_apply_button_state(st, dlg); return TRUE;
                case IDC_MENU_DELETE: menu_action_delete(st, st->pages[PAGE_MENU]); update_apply_button_state(st, dlg); return TRUE;
                case IDC_MENU_UP: menu_action_move(st, st->pages[PAGE_MENU], -1); update_apply_button_state(st, dlg); return TRUE;
                case IDC_MENU_DOWN: menu_action_move(st, st->pages[PAGE_MENU], 1); update_apply_button_state(st, dlg); return TRUE;
                case IDC_ICON_BROWSE_FILE: icons_action_browse(st, st->pages[PAGE_ICONS], 0); update_apply_button_state(st, dlg); return TRUE;
                case IDC_ICON_BROWSE_LIGHT: icons_action_browse(st, st->pages[PAGE_ICONS], 1); update_apply_button_state(st, dlg); return TRUE;
                case IDC_ICON_BROWSE_DARK: icons_action_browse(st, st->pages[PAGE_ICONS], 2); update_apply_button_state(st, dlg); return TRUE;
                case IDC_ICON_CLEAR: icons_action_clear(st, st->pages[PAGE_ICONS]); update_apply_button_state(st, dlg); return TRUE;
                case IDC_POINTERRELATIVE: {
                    // Live enable/disable of the Ignore Relative dropdown
                    if (st && st->pages[PAGE_PLACEMENT]) {
                        HWND hRelative = GetDlgItem(st->pages[PAGE_PLACEMENT], IDC_IGNORE_RELATIVE_COMBO);
                        HWND hRelativeLabel = GetDlgItem(st->pages[PAGE_PLACEMENT], IDC_IGNORE_RELATIVE_LABEL);
                        BOOL enabled = IsDlgButtonChecked(st->pages[PAGE_PLACEMENT], IDC_POINTERRELATIVE) == BST_CHECKED;
                        EnableWindow(hRelative, enabled);
                        if (hRelativeLabel) EnableWindow(hRelativeLabel, enabled);
                    }
                    update_apply_button_state(st, dlg);
                    return TRUE;
                }
                case IDC_SHOWICONS:
                    if (HIWORD(wParam) == CBN_SELCHANGE) {
                        update_show_menu_icon_dependencies(st ? st->pages[PAGE_GENERAL] : NULL);
                        update_apply_button_state(st, dlg);
                    }
                    return TRUE;
                case IDC_SORT_FIELD:
                case IDC_SORT_DIRECTION:
                case IDC_SORT_FOLDERSFIRST:
                    if (HIWORD(wParam) == CBN_SELCHANGE) {
                        Sorting_SyncCombo(st, (HWND)lParam, LOWORD(wParam));
                        update_apply_button_state(st, dlg);
                    }
                    return TRUE;
                case IDC_RENDERER_COMBO: {
                    if (st && st->pages[PAGE_GENERAL]) {
                        int renderer = (int)SendDlgItemMessageW(st->pages[PAGE_GENERAL], IDC_RENDERER_COMBO, CB_GETCURSEL, 0, 0);
                        BOOL winuiEnabled = (renderer == 1);
                        EnableWindow(GetDlgItem(st->pages[PAGE_GENERAL], IDC_WINUI_SIZE_COMBO), winuiEnabled);
                        EnableWindow(GetDlgItem(st->pages[PAGE_GENERAL], IDC_WINUI_ALWAYS_SHOW_ICONS), winuiEnabled);
                    }
                    update_apply_button_state(st, dlg);
                    return TRUE;
                }
            }
            if (st && settings_command_may_change_state(HIWORD(wParam))) {
                update_apply_button_state(st, dlg);
                return TRUE;
            }
            break;
    }
    return FALSE;
}













