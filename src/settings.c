
// Reconstructed clean settings.c to address prior hidden corruption causing C2181.
#include "settings.h"
#include "settings_extra.h"
#include "resource.h"
#include "util.h"
#include <windows.h>
#include <commctrl.h>
#include <shlwapi.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>

// Forward declaration for dialog procedure
static INT_PTR CALLBACK MainDlgProc(HWND dlg, UINT msg, WPARAM wParam, LPARAM lParam);

// Shows the settings dialog (modal). Returns TRUE if any values were changed and saved.
BOOL ShowSettingsDialog(HWND owner, Config* cfg) {
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
    g_settingsOwnerHwnd = NULL;
    return (result == IDOK) ? TRUE : FALSE;
}

typedef struct SettingsState {
    Config* cfg;
    // Track original power option states
    BOOL origExcludeSleep, origExcludeHibernate, origExcludeShutdown, origExcludeRestart, origExcludeLock, origExcludeLogoff;
    HWND hTabs;
    HWND pages[8]; // General, Placement, Menu, Icons, Sorting, Controls, Appearance, Advanced
    // Working copy of menu/icon items so Apply/Save commits atomically
    ConfigItem workingItems[64];
    int workingCount;
    BOOL workingDirty; // set when workingItems modified
    int baseW, baseH; // initial dialog size for min constraint
    HFONT hItalic; // italic font for filename label
    BOOL reloadNeeded; // set if tray reload is needed after Save & Close
} SettingsState;

enum {
    PAGE_GENERAL = 0,
    PAGE_PLACEMENT,
    PAGE_MENU,
    PAGE_ICONS,
    PAGE_SORTING,
    PAGE_CONTROLS,
    PAGE_APPEARANCE,
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
static void Controls_Load(HWND pg, Config* c);
static BOOL Controls_Save(HWND pg, Config* c);
static void relayout_page(HWND page, int w, int h);
static void show_page(SettingsState* st,int idx);

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
            // Forward to main settings dialog to handle
            SendMessageW(parent, WM_COMMAND, wParam, lParam);
            return TRUE;
        }
        break; }
    }
    return FALSE;
}

static void set_check(HWND d,int id,BOOL v){ CheckDlgButton(d,id,v?BST_CHECKED:BST_UNCHECKED); }
static BOOL get_check(HWND d,int id){ return IsDlgButtonChecked(d,id)==BST_CHECKED; }
static void set_int(HWND d,int id,int v){ WCHAR b[32]; wsprintfW(b,L"%d",v); SetDlgItemTextW(d,id,b);} 
static int  get_int(HWND d,int id,int def){ WCHAR b[32]; if(!GetDlgItemTextW(d,id,b,ARRAYSIZE(b))) return def; int v=_wtoi(b); return (v==0)?def:v; }

static int show_icons_combo_index_from_value(int showIcons){
    if(showIcons == 1) return 0;
    if(showIcons == 2) return 1;
    return 2;
}

static int show_icons_value_from_combo_index(int sel){
    if(sel == 0) return 1;
    if(sel == 1) return 2;
    return 0;
}

static const WCHAR* show_icons_ini_value(int showIcons){
    if(showIcons == 1) return L"true";
    if(showIcons == 2) return L"other";
    return L"false";
}

static void apply_dialog_icon(HWND dlg,const Config* cfg){
    int cx=GetSystemMetrics(SM_CXICON), cy=GetSystemMetrics(SM_CYICON);
    int cxs=GetSystemMetrics(SM_CXSMICON), cys=GetSystemMetrics(SM_CYSMICON);
    HICON icoBig=NULL, icoSmall=NULL;
    const WCHAR *paths[3]={NULL,NULL,NULL};
    if(cfg){
        if(cfg->trayIconPath[0])      paths[0]=cfg->trayIconPath;
        if(cfg->trayIconPathLight[0]) paths[1]=cfg->trayIconPathLight;
        if(cfg->trayIconPathDark[0])  paths[2]=cfg->trayIconPathDark;
        for(int i=0;i<3 && !icoBig;i++) if(paths[i]) icoBig=(HICON)LoadImageW(NULL,paths[i],IMAGE_ICON,cx,cy,LR_LOADFROMFILE);
        for(int i=0;i<3 && !icoSmall;i++) if(paths[i]) icoSmall=(HICON)LoadImageW(NULL,paths[i],IMAGE_ICON,cxs,cys,LR_LOADFROMFILE);
    }
    if(!icoBig)   icoBig=(HICON)LoadImageW(GetModuleHandleW(NULL),MAKEINTRESOURCEW(IDI_APPICON),IMAGE_ICON,cx,cy,LR_DEFAULTCOLOR);
    if(!icoSmall) icoSmall=(HICON)LoadImageW(GetModuleHandleW(NULL),MAKEINTRESOURCEW(IDI_APPICON),IMAGE_ICON,cxs,cys,LR_DEFAULTCOLOR);
    if(icoBig)   SendMessageW(dlg,WM_SETICON,ICON_BIG,(LPARAM)icoBig);
    if(icoSmall) SendMessageW(dlg,WM_SETICON,ICON_SMALL,(LPARAM)icoSmall);
}

// -------- General Page --------
static void General_Load(HWND pg, Config* c){
    HWND hShowIcons = GetDlgItem(pg, IDC_SHOWICONS);
    SendMessageW(hShowIcons, CB_RESETCONTENT, 0, 0);
    SendMessageW(hShowIcons, CB_ADDSTRING, 0, (LPARAM)L"Enable");
    SendMessageW(hShowIcons, CB_ADDSTRING, 0, (LPARAM)L"Submenus only");
    SendMessageW(hShowIcons, CB_ADDSTRING, 0, (LPARAM)L"Disable");
    SendMessageW(hShowIcons, CB_SETCURSEL, show_icons_combo_index_from_value(c->showIcons), 0);
        set_check(pg, IDC_POWER_HIBERNATE, !c->excludeHibernate);
        set_check(pg, IDC_POWER_LOCK,      !c->excludeLock);
        set_check(pg, IDC_POWER_LOGOFF,    !c->excludeLogoff);
        set_check(pg, IDC_POWER_RESTART,   !c->excludeRestart);
        set_check(pg, IDC_POWER_SHUTDOWN,  !c->excludeShutdown);
        set_check(pg, IDC_POWER_SLEEP,     !c->excludeSleep);
    set_check(pg,IDC_SHOWONLAUNCH,c->showOnLaunch);
    set_check(pg,IDC_SHOWTRAYICON,c->showTrayIcon);
    set_check(pg,IDC_STARTONLOGIN,c->startOnLogin);
    set_check(pg,IDC_SHOWFOLDERICONS,c->showFolderIcons);
    set_check(pg,IDC_SHOWFILEICONS,c->showFileIcons);
    set_check(pg,IDC_KEEPCTXOPEN,c->keepMenuOpenAfterContextAction);
    set_check(pg,IDC_SHOWEXTENSIONS,c->showExtensions);
    set_check(pg,IDC_SHOWHIDDEN,c->showHidden);
    set_check(pg,IDC_SHOWDOTFILES,c->showDotfiles);
    set_check(pg,IDC_MONOTRAYICON,c->monochromeTrayIcon);
    // ...existing code...
    // Power Menu checkboxes removed from GUI
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
    BOOL origExcludeHibernate = c->excludeHibernate;
    BOOL origExcludeLock = c->excludeLock;
    BOOL origExcludeLogoff = c->excludeLogoff;
    BOOL origExcludeRestart = c->excludeRestart;
    BOOL origExcludeShutdown = c->excludeShutdown;
    BOOL origExcludeSleep = c->excludeSleep;

    c->excludeHibernate = !get_check(pg, IDC_POWER_HIBERNATE);
    c->excludeLock      = !get_check(pg, IDC_POWER_LOCK);
    c->excludeLogoff    = !get_check(pg, IDC_POWER_LOGOFF);
    c->excludeRestart   = !get_check(pg, IDC_POWER_RESTART);
    c->excludeShutdown  = !get_check(pg, IDC_POWER_SHUTDOWN);
    c->excludeSleep     = !get_check(pg, IDC_POWER_SLEEP);

    if (origExcludeHibernate != c->excludeHibernate || origExcludeLock != c->excludeLock || origExcludeLogoff != c->excludeLogoff || origExcludeRestart != c->excludeRestart || origExcludeShutdown != c->excludeShutdown || origExcludeSleep != c->excludeSleep) ch = TRUE;

    b=get_check(pg,IDC_SHOWONLAUNCH);               if(c->showOnLaunch!=b){c->showOnLaunch=b;ch=TRUE;}
    b=get_check(pg,IDC_SHOWTRAYICON);               if(c->showTrayIcon!=b){c->showTrayIcon=b;ch=TRUE;reloadNeeded=TRUE;}
    b=get_check(pg,IDC_STARTONLOGIN);               if(c->startOnLogin!=b){c->startOnLogin=b;ch=TRUE;}
    v=(int)SendDlgItemMessageW(pg,IDC_SHOWICONS,CB_GETCURSEL,0,0); if(v>=0){ int showIcons=show_icons_value_from_combo_index(v); if(c->showIcons!=showIcons){c->showIcons=showIcons;ch=TRUE;} }
    b=get_check(pg,IDC_SHOWFOLDERICONS);            if(c->showFolderIcons!=b){c->showFolderIcons=b;ch=TRUE;}
    b=get_check(pg,IDC_SHOWFILEICONS);              if(c->showFileIcons!=b){c->showFileIcons=b;ch=TRUE;}
    b=get_check(pg,IDC_KEEPCTXOPEN);                if(c->keepMenuOpenAfterContextAction!=b){c->keepMenuOpenAfterContextAction=b;ch=TRUE;}
    b=get_check(pg,IDC_SHOWEXTENSIONS);             if(c->showExtensions!=b){c->showExtensions=b;ch=TRUE;}
    b=get_check(pg,IDC_SHOWHIDDEN);                 if(c->showHidden!=b){c->showHidden=b;ch=TRUE;}
    b=get_check(pg,IDC_SHOWDOTFILES);               if(c->showDotfiles!=b){c->showDotfiles=b;ch=TRUE;}
    b=get_check(pg,IDC_MONOTRAYICON);               if(c->monochromeTrayIcon!=b){c->monochromeTrayIcon=b;ch=TRUE;reloadNeeded=TRUE;}

    if(c->iniPath[0]){
        WCHAR bufDepth[8];
        wsprintfW(bufDepth, L"%d", c->folderMaxDepth);
        WritePrivateProfileStringW(L"General", L"FolderSubmenuDepth", bufDepth, c->iniPath);
        WritePrivateProfileStringW(L"General",L"RunInBackground",   c->runInBackground?L"true":L"false",c->iniPath);
        WritePrivateProfileStringW(L"General",L"ShowOnLaunch",      c->showOnLaunch?L"true":L"false",c->iniPath);
        WritePrivateProfileStringW(L"General",L"ShowTrayIcon",      c->showTrayIcon?L"true":L"false",c->iniPath);
        WritePrivateProfileStringW(L"General",L"StartOnLogin",      c->startOnLogin?L"true":L"false",c->iniPath);
        WritePrivateProfileStringW(L"General",L"ShowIcons",         show_icons_ini_value(c->showIcons),c->iniPath);
        WritePrivateProfileStringW(L"General",L"ShowFolderIcons",   c->showFolderIcons?L"true":L"false",c->iniPath);
        WritePrivateProfileStringW(L"General",L"ShowFileIcons",     c->showFileIcons?L"true":L"false",c->iniPath);
        WritePrivateProfileStringW(L"General",L"KeepMenuOpenAfterContextAction", c->keepMenuOpenAfterContextAction?L"true":L"false",c->iniPath);
        WritePrivateProfileStringW(L"General",L"ShowHidden",        c->showHidden?L"true":L"false",c->iniPath);
        WritePrivateProfileStringW(L"General",L"ShowDotfiles",      c->showDotfiles?L"true":L"false",c->iniPath);
        WritePrivateProfileStringW(L"General",L"MonochromeTrayIcon",c->monochromeTrayIcon?L"true":L"false",c->iniPath);
        // Power options: only write keys for excluded (unchecked) options as Name=0, remove when included (checked)
        int excluded = 0;
        if (c->excludeSleep) { WritePrivateProfileStringW(L"Power", L"Sleep", L"0", c->iniPath); excluded++; } else { WritePrivateProfileStringW(L"Power", L"Sleep", NULL, c->iniPath); }
        if (c->excludeHibernate) { WritePrivateProfileStringW(L"Power", L"Hibernate", L"0", c->iniPath); excluded++; } else { WritePrivateProfileStringW(L"Power", L"Hibernate", NULL, c->iniPath); }
        if (c->excludeShutdown) { WritePrivateProfileStringW(L"Power", L"Shutdown", L"0", c->iniPath); excluded++; } else { WritePrivateProfileStringW(L"Power", L"Shutdown", NULL, c->iniPath); }
        if (c->excludeRestart) { WritePrivateProfileStringW(L"Power", L"Restart", L"0", c->iniPath); excluded++; } else { WritePrivateProfileStringW(L"Power", L"Restart", NULL, c->iniPath); }
        if (c->excludeLock) { WritePrivateProfileStringW(L"Power", L"Lock", L"0", c->iniPath); excluded++; } else { WritePrivateProfileStringW(L"Power", L"Lock", NULL, c->iniPath); }
        if (c->excludeLogoff) { WritePrivateProfileStringW(L"Power", L"Logoff", L"0", c->iniPath); excluded++; } else { WritePrivateProfileStringW(L"Power", L"Logoff", NULL, c->iniPath); }
        if (excluded == 0) { WritePrivateProfileStringW(L"Power", NULL, NULL, c->iniPath); }
    }

    WCHAR exe[MAX_PATH];
    GetModuleFileNameW(NULL, exe, ARRAYSIZE(exe));
    WCHAR cmd[2048];
    if(c->iniPath[0]) {
        WCHAR cfgAbs[MAX_PATH];
        DWORD n = GetFullPathNameW(c->iniPath, ARRAYSIZE(cfgAbs), cfgAbs, NULL);
        if (n == 0 || n >= ARRAYSIZE(cfgAbs)) lstrcpynW(cfgAbs, c->iniPath, ARRAYSIZE(cfgAbs));
        wsprintfW(cmd, L"\"%s\" --config \"%s\"", exe, cfgAbs);
    } else {
        wsprintfW(cmd, L"\"%s\"", exe);
    }
    WCHAR runName[32];
    lstrcpynW(runName, L"WinMac Menu", ARRAYSIZE(runName));
    if(c->startOnLogin)
        set_run_at_login(runName, cmd);
    else
        remove_run_at_login(runName);

    // Do not trigger reload here; let caller decide based on button pressed
    return reloadNeeded ? 2 : ch ? 1 : 0;
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
    set_int(pg,IDC_HOFFSET_EDIT,c->hOffset); set_int(pg,IDC_VOFFSET_EDIT,c->vOffset);
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
    v=get_int(pg,IDC_HOFFSET_EDIT,c->hOffset); if(v!=c->hOffset){c->hOffset=v;ch=TRUE;}
    v=get_int(pg,IDC_VOFFSET_EDIT,c->vOffset); if(v!=c->vOffset){c->vOffset=v;ch=TRUE;}
    // Save IgnoreOffsetWhenCentered from combobox
    HWND hCentered = GetDlgItem(pg, IDC_IGNORE_CENTERED_COMBO);
    int centeredSel = (int)SendMessageW(hCentered, CB_GETCURSEL, 0, 0);
    c->ignoreHOffsetWhenCentered = c->ignoreVOffsetWhenCentered = FALSE;
    if(centeredSel == 0) { c->ignoreHOffsetWhenCentered = TRUE; c->ignoreVOffsetWhenCentered = TRUE; }
    else if(centeredSel == 2) { c->ignoreHOffsetWhenCentered = TRUE; }
    else if(centeredSel == 3) { c->ignoreVOffsetWhenCentered = TRUE; }
    // Save IgnoreOffsetWhenRelative from combobox
    HWND hRelative = GetDlgItem(pg, IDC_IGNORE_RELATIVE_COMBO);
    int relativeSel = (int)SendMessageW(hRelative, CB_GETCURSEL, 0, 0);
    c->ignoreHOffsetWhenRelative = c->ignoreVOffsetWhenRelative = FALSE;
    if(relativeSel == 0) { c->ignoreHOffsetWhenRelative = TRUE; c->ignoreVOffsetWhenRelative = TRUE; }
    else if(relativeSel == 2) { c->ignoreHOffsetWhenRelative = TRUE; }
    else if(relativeSel == 3) { c->ignoreVOffsetWhenRelative = TRUE; }

    if(!ch) return FALSE;
    if(c->iniPath[0]){
        WCHAR buf[32];
        WritePrivateProfileStringW(L"Placement",L"PointerRelative",c->pointerRelative?L"true":L"false",c->iniPath);
        const WCHAR* hp=L"right"; if(c->hPlacement==0) hp=L"left"; else if(c->hPlacement==1) hp=L"center";
        const WCHAR* vp=L"bottom"; if(c->vPlacement==0) vp=L"top"; else if(c->vPlacement==1) vp=L"center";
        WritePrivateProfileStringW(L"Placement",L"Horizontal",hp,c->iniPath);
        WritePrivateProfileStringW(L"Placement",L"Vertical",vp,c->iniPath);
        wsprintfW(buf,L"%d",c->hOffset); WritePrivateProfileStringW(L"Placement",L"HOffset",buf,c->iniPath);
        wsprintfW(buf,L"%d",c->vOffset); WritePrivateProfileStringW(L"Placement",L"VOffset",buf,c->iniPath);
        const WCHAR* cen=L"false"; if(c->ignoreHOffsetWhenCentered&&c->ignoreVOffsetWhenCentered) cen=L"true"; else if(c->ignoreHOffsetWhenCentered) cen=L"HOffset"; else if(c->ignoreVOffsetWhenCentered) cen=L"VOffset"; WritePrivateProfileStringW(L"Placement",L"IgnoreOffsetWhenCentered",cen,c->iniPath);
        const WCHAR* rel=L"false"; if(c->ignoreHOffsetWhenRelative&&c->ignoreVOffsetWhenRelative) rel=L"true"; else if(c->ignoreHOffsetWhenRelative) rel=L"HOffset"; else if(c->ignoreVOffsetWhenRelative) rel=L"VOffset"; WritePrivateProfileStringW(L"Placement",L"IgnoreOffsetWhenRelative",rel,c->iniPath);
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
            WritePrivateProfileStringW(L"Sorting",L"SortBy",fields[c->sortField],c->iniPath);
        WritePrivateProfileStringW(L"Sorting",L"SortDirection",c->sortDescending?L"descending":L"ascending",c->iniPath);
        const WCHAR* objTypeStr = L"false";
        if (c->sortObjectTypePriority == 1) objTypeStr = L"true";
        else if (c->sortObjectTypePriority == 2) objTypeStr = L"files";
        WritePrivateProfileStringW(L"Sorting",L"FoldersFirst",objTypeStr,c->iniPath);
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
    b = get_check(pg, IDC_IGNORE_FULLSCREEN_CHECK); if(c->ignoreTriggersWhenFullscreen != b){ c->ignoreTriggersWhenFullscreen = b; ch = TRUE; }
    
    WCHAR exclusionBuf[1024];
    GetDlgItemTextW(pg, IDC_FULLSCREEN_EXCLUSION_EDIT, exclusionBuf, ARRAYSIZE(exclusionBuf));
    if(lstrcmpW(c->fullscreenExclusionList, exclusionBuf) != 0){
        lstrcpynW(c->fullscreenExclusionList, exclusionBuf, ARRAYSIZE(c->fullscreenExclusionList));
        ch = TRUE;
    }

    if(!ch) return FALSE;
    if(c->iniPath[0]){
        WritePrivateProfileStringW(L"Controls", L"LeftClick", c->leftClickTrigger?L"true":L"false", c->iniPath);
        WritePrivateProfileStringW(L"Controls", L"RightClick", c->rightClickTrigger?L"true":L"false", c->iniPath);
        WritePrivateProfileStringW(L"Controls", L"MiddleClick", c->middleClickTrigger?L"true":L"false", c->iniPath);
        WritePrivateProfileStringW(L"Controls", L"ShiftLeftClick", c->shiftLeftClickTrigger?L"true":L"false", c->iniPath);
        WritePrivateProfileStringW(L"Controls", L"ShiftRightClick", c->shiftRightClickTrigger?L"true":L"false", c->iniPath);
        WritePrivateProfileStringW(L"Controls", L"ShiftMiddleClick", c->shiftMiddleClickTrigger?L"true":L"false", c->iniPath);
        WritePrivateProfileStringW(L"Controls", L"WindowsKey", c->windowsKeyTrigger?L"true":L"false", c->iniPath);
        WritePrivateProfileStringW(L"Controls", L"ShiftWindowsKey", c->shiftWindowsKeyTrigger?L"true":L"false", c->iniPath);
        WritePrivateProfileStringW(L"Controls", L"IgnoreTriggersWhenFullscreen", c->ignoreTriggersWhenFullscreen?L"true":L"false", c->iniPath);
        WritePrivateProfileStringW(L"Controls", L"FullscreenExclusionList", c->fullscreenExclusionList, c->iniPath);
    }
    return TRUE;
}

// -------- Appearance Page --------
static void Appearance_Load(HWND pg, Config* c){
    set_check(pg, IDC_ROOT_MENU_ICON_SIZE, c->rootMenuLargeIcons);
    set_check(pg, IDC_KEEP_LARGE_MENU_HIGHLIGHT_TEXT_COLOR, c->keepLargeMenuHighlightTextColor);
    {
        HWND hAnim = GetDlgItem(pg, IDC_ANIMATION_COMBO);
        if (hAnim) {
            SendMessageW(hAnim, CB_RESETCONTENT, 0, 0);
            SendMessageW(hAnim, CB_ADDSTRING, 0, (LPARAM)L"Disabled");
            SendMessageW(hAnim, CB_ADDSTRING, 0, (LPARAM)L"Top");
            SendMessageW(hAnim, CB_ADDSTRING, 0, (LPARAM)L"Bottom");
            SendMessageW(hAnim, CB_ADDSTRING, 0, (LPARAM)L"Left");
            SendMessageW(hAnim, CB_ADDSTRING, 0, (LPARAM)L"Right");
            {
                int animSel = 2;
                if (c->animationDirection == ANIM_AUTO) animSel = 0;
                else if (c->animationDirection == ANIM_TOP) animSel = 1;
                else if (c->animationDirection == ANIM_LEFT) animSel = 3;
                else if (c->animationDirection == ANIM_RIGHT) animSel = 4;
                SendMessageW(hAnim, CB_SETCURSEL, animSel, 0);
            }
            BOOL animEnabled = IsDlgButtonChecked(pg, IDC_ROOT_MENU_ICON_SIZE) == BST_CHECKED;
            EnableWindow(hAnim, animEnabled);
            {
                HWND hAnimLabel = GetDlgItem(pg, IDC_ANIMATION_LABEL);
                if (hAnimLabel) EnableWindow(hAnimLabel, animEnabled);
            }
        }
    }
}

static BOOL Appearance_Save(HWND pg, Config* c){
    BOOL ch=FALSE; BOOL b;
    b=get_check(pg,IDC_ROOT_MENU_ICON_SIZE); if(c->rootMenuLargeIcons!=b){c->rootMenuLargeIcons=b;ch=TRUE;}
    b=get_check(pg,IDC_KEEP_LARGE_MENU_HIGHLIGHT_TEXT_COLOR); if(c->keepLargeMenuHighlightTextColor!=b){c->keepLargeMenuHighlightTextColor=b;ch=TRUE;}
    {
        HWND hAnim = GetDlgItem(pg, IDC_ANIMATION_COMBO);
        if (hAnim) {
            int animSel = (int)SendMessageW(hAnim, CB_GETCURSEL, 0, 0);
            int animValue = ANIM_BOTTOM;
            if (animSel == 0) animValue = ANIM_AUTO;
            else if (animSel == 1) animValue = ANIM_TOP;
            else if (animSel == 3) animValue = ANIM_LEFT;
            else if (animSel == 4) animValue = ANIM_RIGHT;
            if (c->animationDirection != animValue) { c->animationDirection = animValue; ch = TRUE; }
        }
    }
    if(!ch) return FALSE;
    if(c->iniPath[0]){
        WritePrivateProfileStringW(L"Appearance",L"LargeMenuIcons",c->rootMenuLargeIcons?L"true":L"false",c->iniPath);
        WritePrivateProfileStringW(L"Appearance",L"KeepLargeMenuHighlightTextColor",c->keepLargeMenuHighlightTextColor?L"true":L"false",c->iniPath);
        {
            const WCHAR* anim = L"bottom";
            if (c->animationDirection == ANIM_AUTO) anim = L"disabled";
            else if (c->animationDirection == ANIM_TOP) anim = L"top";
            else if (c->animationDirection == ANIM_LEFT) anim = L"left";
            else if (c->animationDirection == ANIM_RIGHT) anim = L"right";
            WritePrivateProfileStringW(L"Appearance", L"LargeMenuAnimation", anim, c->iniPath);
        }
        // Cleanup old location keys after migration.
        WritePrivateProfileStringW(L"Advanced",L"LargeMenuIcons",NULL,c->iniPath);
        WritePrivateProfileStringW(L"Advanced",L"LargeMenuAnimation",NULL,c->iniPath);
        WritePrivateProfileStringW(L"Advanced",L"KeepLargeMenuHighlightTextColor",NULL,c->iniPath);
    }
    return TRUE;
}

// -------- Advanced Page --------
static void Advanced_Load(HWND pg, Config* c){
    set_check(pg,IDC_RECENT_SHOW_EXT,c->recentShowExtensions);
    set_check(pg,IDC_RECENT_SHOW_CLEAN,c->recentShowCleanItems);
    set_check(pg,IDC_THISPC_AS_SUBMENU,c->thisPCAsSubmenu);
    set_check(pg,IDC_THISPC_ITEMS_AS_SUBMENUS,c->thisPCItemsAsSubmenus);
    set_check(pg,IDC_THISPC_SHOW_ICONS,c->thisPCShowIcons);
    set_check(pg,IDC_HOME_AS_SUBMENU,c->homeAsSubmenu);
    set_check(pg,IDC_HOME_ITEMS_AS_SUBMENUS,c->homeItemsAsSubmenus);
    set_check(pg,IDC_HOME_SHOW_ICONS,c->homeShowIcons);
    set_check(pg,IDC_TASKKILL_ALL_DESKTOPS,c->taskKillAllDesktops);
    set_check(pg,IDC_TASKKILL_IGNORE_SYSTEM,c->taskKillIgnoreSystem);
    set_check(pg,IDC_TASKKILL_LIST_WINDOWS,c->taskKillListWindows);
    set_check(pg,IDC_TASKKILL_SHOW_ICONS,c->taskKillShowIcons);
    set_int(pg,IDC_TASKKILL_MAX_COMBO,c->taskKillMax);
    if(c->taskKillExcludes[0]){
        SetDlgItemTextW(pg,IDC_TASKKILL_EXCLUDES,c->taskKillExcludes);
    }else{
        // Set cue banner (placeholder) if empty
        HWND hEdit = GetDlgItem(pg, IDC_TASKKILL_EXCLUDES);
        if(hEdit){
            // Windows >= Vista: EM_SETCUEBANNER = 0x1501
            SendMessageW(hEdit, 0x1501, TRUE, (LPARAM)L"mspaint.exe,xboxapp.exe");
            SetDlgItemTextW(pg,IDC_TASKKILL_EXCLUDES,L"");
        }
    }
    // Power exclusions removed from Advanced page
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
    set_int(pg,IDC_RECENTMAX_EDIT,c->recentMax); SendDlgItemMessageW(pg,IDC_RECENTMAX_SPIN,UDM_SETRANGE,0,MAKELPARAM(99,1));
}
static BOOL Advanced_Save(HWND pg, Config* c){
    BOOL ch=FALSE; BOOL b;
    b=get_check(pg,IDC_RECENT_SHOW_EXT); if(c->recentShowExtensions!=b){c->recentShowExtensions=b;ch=TRUE;}
    b=get_check(pg,IDC_RECENT_SHOW_CLEAN); if(c->recentShowCleanItems!=b){c->recentShowCleanItems=b;ch=TRUE;}
    b=get_check(pg,IDC_THISPC_AS_SUBMENU); if(c->thisPCAsSubmenu!=b){c->thisPCAsSubmenu=b;ch=TRUE;}
    b=get_check(pg,IDC_THISPC_ITEMS_AS_SUBMENUS); if(c->thisPCItemsAsSubmenus!=b){c->thisPCItemsAsSubmenus=b;ch=TRUE;}
    b=get_check(pg,IDC_THISPC_SHOW_ICONS); if(c->thisPCShowIcons!=b){c->thisPCShowIcons=b;ch=TRUE;}
    b=get_check(pg,IDC_HOME_AS_SUBMENU); if(c->homeAsSubmenu!=b){c->homeAsSubmenu=b;ch=TRUE;}
    b=get_check(pg,IDC_HOME_ITEMS_AS_SUBMENUS); if(c->homeItemsAsSubmenus!=b){c->homeItemsAsSubmenus=b;ch=TRUE;}
    b=get_check(pg,IDC_HOME_SHOW_ICONS); if(c->homeShowIcons!=b){c->homeShowIcons=b;ch=TRUE;}
    b=get_check(pg,IDC_TASKKILL_ALL_DESKTOPS); if(c->taskKillAllDesktops!=b){c->taskKillAllDesktops=b;ch=TRUE;}
    b=get_check(pg,IDC_TASKKILL_IGNORE_SYSTEM); if(c->taskKillIgnoreSystem!=b){c->taskKillIgnoreSystem=b;ch=TRUE;}
    b=get_check(pg,IDC_TASKKILL_LIST_WINDOWS); if(c->taskKillListWindows!=b){c->taskKillListWindows=b;ch=TRUE;}
    b=get_check(pg,IDC_TASKKILL_SHOW_ICONS); if(c->taskKillShowIcons!=b){c->taskKillShowIcons=b;ch=TRUE;}
    int v2=get_int(pg,IDC_TASKKILL_MAX_COMBO,c->taskKillMax); if(v2!=c->taskKillMax){c->taskKillMax=v2;ch=TRUE;}
    WCHAR buf2[512];
    if(GetDlgItemTextW(pg,IDC_TASKKILL_EXCLUDES,buf2,ARRAYSIZE(buf2))){
        if(lstrcmpW(buf2,c->taskKillExcludes)!=0){
            lstrcpynW(c->taskKillExcludes,buf2,ARRAYSIZE(c->taskKillExcludes));
            ch=TRUE;
        }
    }
    // Power inclusion checkboxes removed from Advanced page
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
        WritePrivateProfileStringW(L"RecentItems",L"RecentShowExtensions",c->recentShowExtensions?L"true":L"false",c->iniPath);
        WritePrivateProfileStringW(L"RecentItems",L"RecentShowCleanItems",c->recentShowCleanItems?L"true":L"false",c->iniPath);
        WritePrivateProfileStringW(L"RecentItems",L"RecentLabel", c->recentLabelMode==0?L"fullpath":L"name", c->iniPath);
        WritePrivateProfileStringW(L"ThisPC",L"ThisPCAsSubmenu",c->thisPCAsSubmenu?L"true":L"false",c->iniPath);
        WritePrivateProfileStringW(L"ThisPC",L"ThisPCItemsAsSubmenus",c->thisPCItemsAsSubmenus?L"true":L"false",c->iniPath);
        WritePrivateProfileStringW(L"ThisPC",L"ThisPCShowIcons",c->thisPCShowIcons?L"true":L"false",c->iniPath);
        WritePrivateProfileStringW(L"Home",L"HomeAsSubmenu",c->homeAsSubmenu?L"true":L"false",c->iniPath);
        WritePrivateProfileStringW(L"Home",L"HomeItemsAsSubmenus",c->homeItemsAsSubmenus?L"true":L"false",c->iniPath);
        WritePrivateProfileStringW(L"Home",L"HomeShowIcons",c->homeShowIcons?L"true":L"false",c->iniPath);
        WritePrivateProfileStringW(L"TaskKill",L"TaskKillAllDesktops",c->taskKillAllDesktops?L"true":L"false",c->iniPath);
        WritePrivateProfileStringW(L"TaskKill",L"TaskKillIgnoreSystem",c->taskKillIgnoreSystem?L"true":L"false",c->iniPath);
        WritePrivateProfileStringW(L"TaskKill",L"TaskKillListWindows",c->taskKillListWindows?L"true":L"false",c->iniPath);
        WCHAR num2[32]; wsprintfW(num2,L"%d",c->taskKillMax); WritePrivateProfileStringW(L"TaskKill",L"TaskKillMax",num2,c->iniPath);
        WritePrivateProfileStringW(L"TaskKill",L"TaskKillShowIcons",c->taskKillShowIcons?L"true":L"false",c->iniPath);
        WritePrivateProfileStringW(L"TaskKill",L"TaskKillExcludes",c->taskKillExcludes,c->iniPath);
        WCHAR num[32]; wsprintfW(num,L"%d",c->recentMax); WritePrivateProfileStringW(L"RecentItems",L"RecentMax",num,c->iniPath);
        if(c->defaultIconPath[0]) WritePrivateProfileStringW(L"General",L"DefaultIcon",c->defaultIconPath,c->iniPath); else WritePrivateProfileStringW(L"General",L"DefaultIcon",NULL,c->iniPath);
        if(c->defaultIconPathLight[0]) WritePrivateProfileStringW(L"General",L"DefaultIconLight",c->defaultIconPathLight,c->iniPath); else WritePrivateProfileStringW(L"General",L"DefaultIconLight",NULL,c->iniPath);
        if(c->defaultIconPathDark[0]) WritePrivateProfileStringW(L"General",L"DefaultIconDark",c->defaultIconPathDark,c->iniPath); else WritePrivateProfileStringW(L"General",L"DefaultIconDark",NULL,c->iniPath);
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
        case CI_FOLDER_SUBMENU: return L"Folder (submenu)";
        case CI_POWER_SLEEP: return L"Sleep";
        case CI_POWER_HIBERNATE: return L"Hibernate";
        case CI_POWER_SHUTDOWN: return L"Shutdown";
        case CI_POWER_RESTART: return L"Restart";
        case CI_POWER_LOCK: return L"Lock";
        case CI_POWER_LOGOFF: return L"Logoff";
        case CI_RECENT_SUBMENU: return L"Recent";
        case CI_POWER_MENU: return L"Power Menu";
        case CI_THISPC: return L"This PC";
        case CI_HOME: return L"Home";
        case CI_TASKKILL: return L"Task Kill";
    }
    return L"?";
}
static void lv_add_col(HWND lv,int i,int w,const WCHAR* txt){
    LVCOLUMNW c; ZeroMemory(&c,sizeof(c));
    c.mask = LVCF_WIDTH|LVCF_TEXT|LVCF_SUBITEM;
    c.cx   = w;
    WCHAR tmp[128];
    if(!txt) txt=L"";
    lstrcpynW(tmp, txt, ARRAYSIZE(tmp));
    c.pszText = tmp;
    c.iSubItem = i;
    ListView_InsertColumn(lv,i,&c);
}
static void lv_autosize_cols(HWND lv){
    if(!lv) return;
    HWND header = ListView_GetHeader(lv);
    int colCount = header ? Header_GetItemCount(header) : 0;
    for(int i=0; i<colCount; ++i){
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
    lv_add_col(lv,0,40,L"#");
    lv_add_col(lv,1,90,L"Type");
    lv_add_col(lv,2,110,L"Label");
    lv_add_col(lv,3,160,L"Path"); // widen path to reclaim removed column width
    lv_add_col(lv,4,110,L"Params");
    ListView_DeleteAllItems(lv);
    ConfigItem* arr=c->items; int cnt=c->count;
    if(st && st->workingCount>0){ arr=st->workingItems; cnt=st->workingCount; }
    for(int i=0;i<cnt;i++){
        ConfigItem* it=&arr[i];
        WCHAR idx[12]; wsprintfW(idx,L"%d",i+1);
        WCHAR idxCopy[12]; lstrcpynW(idxCopy,idx,ARRAYSIZE(idxCopy));
        LVITEMW li; ZeroMemory(&li,sizeof(li));
        li.mask=LVIF_TEXT|LVIF_PARAM; li.iItem=i; li.pszText=idxCopy; li.lParam=i;
        ListView_InsertItem(lv,&li);
        {
            const WCHAR* tname = item_type_name(it->type);
            WCHAR tmpType[64]; lstrcpynW(tmpType,tname? tname : L"?",ARRAYSIZE(tmpType));
            ListView_SetItemText(lv,i,1,tmpType);
        }
        if(it->label[0]){ ListView_SetItemText(lv,i,2,it->label); }
        else if(it->type==CI_SEPARATOR){ lv_set_text(lv,i,2,L"(separator)"); }
        else { lv_set_text(lv,i,2,L""); }
        if(it->type==CI_URI||it->type==CI_FILE||it->type==CI_CMD||it->type==CI_FOLDER||it->type==CI_FOLDER_SUBMENU){ ListView_SetItemText(lv,i,3,it->path); }
        else if(it->type==CI_POWER_MENU||it->type==CI_RECENT_SUBMENU){ lv_set_text(lv,i,3,L"(auto)"); }
        else { lv_set_text(lv,i,3,L""); }
        if(it->params[0]) ListView_SetItemText(lv,i,4,it->params);
    }
    lv_autosize_cols(lv);
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
        default: return L"UNKNOWN";
    }
}
static BOOL Menu_Save(HWND pg, Config* c){ UNREFERENCED_PARAMETER(pg); if(!c||!c->iniPath[0]) return FALSE; BOOL any=FALSE;
    // Legacy format: ItemN=Label|TYPE|Path|(optional Params)
    // We overwrite each ItemN key preserving compatibility with existing parser.
    WCHAR key[32]; WCHAR line[2048];
    for(int i=0;i<c->count;i++){
        ConfigItem* it=&c->items[i]; wsprintfW(key,L"Item%d",i+1);
        const WCHAR* token=item_type_token(it->type);
        if(it->type==CI_SEPARATOR){ // Always write canonical separator form
            wsprintfW(line,L"---|SEPARATOR|");
        } else {
            // For special types that don't use path, ensure correct serialization
            if(
                it->type==CI_POWER_MENU ||
                it->type==CI_RECENT_SUBMENU ||
                it->type==CI_TASKKILL ||
                it->type==CI_THISPC ||
                it->type==CI_HOME
            ){
                // These types do not use path/params, just label and type
                wsprintfW(line,L"%s|%s|",it->label,token);
            } else if(it->path[0]){
                if(it->params[0]) wsprintfW(line,L"%s|%s|%s|%s",it->label,token,it->path,it->params);
                else wsprintfW(line,L"%s|%s|%s",it->label,token,it->path);
            } else {
                // Ensure trailing bar to indicate empty path field when historically present (power items)
                if(it->params[0]) wsprintfW(line,L"%s|%s||%s",it->label,token,it->params);
                else wsprintfW(line,L"%s|%s|",it->label,token);
            }
        }
        WritePrivateProfileStringW(L"Menu",key,line,c->iniPath); any=TRUE;
    }
    // Write Count for convenience (parser ignores if absent)
    WCHAR buf[16]; wsprintfW(buf,L"%d",c->count); WritePrivateProfileStringW(L"Menu",L"Count",buf,c->iniPath);
    return any; }
static void Icons_Load(HWND pg, Config* c){
    HWND lv=GetDlgItem(pg,IDC_ICONS_LIST); if(!lv||!c) return;
    SettingsState* st=(SettingsState*)GetWindowLongPtrW(GetParent(pg),GWLP_USERDATA);
    ListView_SetExtendedListViewStyle(lv,LVS_EX_FULLROWSELECT|LVS_EX_GRIDLINES|LVS_EX_DOUBLEBUFFER);
    while(ListView_DeleteColumn(lv,0));
    lv_add_col(lv,0,40,L"#");
    lv_add_col(lv,1,120,L"Label");
    lv_add_col(lv,2,110,L"Main");
    lv_add_col(lv,3,110,L"Light");
    lv_add_col(lv,4,110,L"Dark");
    ListView_DeleteAllItems(lv);
    ConfigItem* arr=c->items; int cnt=c->count;
    if(st && st->workingCount>0){ arr=st->workingItems; cnt=st->workingCount; }
    for(int i=0;i<cnt;i++){
        ConfigItem* it=&arr[i];
        if(it->type==CI_SEPARATOR) continue;
        int row=ListView_GetItemCount(lv);
        WCHAR idx[12]; wsprintfW(idx,L"%d",i+1);
        LVITEMW li={0}; li.mask=LVIF_TEXT|LVIF_PARAM; li.iItem=row; li.pszText=idx; li.lParam=i; ListView_InsertItem(lv,&li);
        if(it->label[0]){
            ListView_SetItemText(lv,row,1,it->label);
        } else {
            const WCHAR* tname = item_type_name(it->type);
            WCHAR tmpType[64]; lstrcpynW(tmpType,tname? tname : L"?",ARRAYSIZE(tmpType));
            ListView_SetItemText(lv,row,1,tmpType);
        }
        // Show nothing when unset instead of placeholders
    if(it->iconPath[0]) { ListView_SetItemText(lv,row,2,it->iconPath); } else { lv_set_text(lv,row,2,L""); }
    if(it->iconPathLight[0]) { ListView_SetItemText(lv,row,3,it->iconPathLight); } else { lv_set_text(lv,row,3,L""); }
    if(it->iconPathDark[0]) { ListView_SetItemText(lv,row,4,it->iconPathDark); } else { lv_set_text(lv,row,4,L""); }
    }
    lv_autosize_cols(lv);
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
        wsprintfW(key,L"Icon%d",i+1);
        if(it->iconPath[0]){ WritePrivateProfileStringW(L"Icons",key,it->iconPath,c->iniPath); any=TRUE; } else { WritePrivateProfileStringW(L"Icons",key,NULL,c->iniPath); }
        wsprintfW(key,L"Icon%d",i+1);
        if(it->iconPathLight[0]){ WritePrivateProfileStringW(L"IconsLight",key,it->iconPathLight,c->iniPath); any=TRUE; } else { WritePrivateProfileStringW(L"IconsLight",key,NULL,c->iniPath); }
        wsprintfW(key,L"Icon%d",i+1);
        if(it->iconPathDark[0]){ WritePrivateProfileStringW(L"IconsDark",key,it->iconPathDark,c->iniPath); any=TRUE; } else { WritePrivateProfileStringW(L"IconsDark",key,NULL,c->iniPath); }
        // Remove obsolete experimental triplet
        wsprintfW(key,L"Item%d",i+1); WritePrivateProfileStringW(L"Icons",key,NULL,c->iniPath);
    }
    for(int i=c->count;i<64;i++){ wsprintfW(key,L"Item%d",i+1); WritePrivateProfileStringW(L"Icons",key,NULL,c->iniPath); }
    if(!any){
        // No icons referenced in any of the three sections: remove them entirely
        WritePrivateProfileStringW(L"Icons", NULL, NULL, c->iniPath);
        WritePrivateProfileStringW(L"IconsLight", NULL, NULL, c->iniPath);
        WritePrivateProfileStringW(L"IconsDark", NULL, NULL, c->iniPath);
    } else {
        // If there were only light/dark variants but not main we still keep sections with entries removed above
        // Clean empty companion sections (heuristic): check if main had none but flags set? Simplicity: rely on any flag.
        // Optional refinement could parse file again; omitted for performance.
    }
    return any;
}
// (menu_update_buttons & refresh_lists implemented later with working copy helpers)

// ---------------- Item Edit Dialog -----------------
typedef struct ItemEditCtx { ConfigItem tmp; BOOL editing; } ItemEditCtx;
static BOOL item_type_uses_path_params(ConfigItemType t){
    switch(t){
        case CI_URI:
        case CI_FILE:
        case CI_CMD:
        case CI_FOLDER:
        case CI_FOLDER_SUBMENU:
            return TRUE;
        default:
            return FALSE;
    }
}
static BOOL item_type_browse_uses_file(ConfigItemType t){
    return (t == CI_FILE || t == CI_CMD);
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
static void item_update_path_controls(HWND dlg, ConfigItemType t){
    BOOL usesPathParams = item_type_uses_path_params(t);
    BOOL canBrowse = item_type_browse_uses_file(t) || item_type_browse_uses_folder(t);
    EnableWindow(GetDlgItem(dlg,IDC_ITEM_PATH),usesPathParams);
    EnableWindow(GetDlgItem(dlg,IDC_ITEM_PARAMS),usesPathParams);
    EnableWindow(GetDlgItem(dlg,IDC_ITEM_PATH_LABEL),usesPathParams);
    EnableWindow(GetDlgItem(dlg,IDC_ITEM_PARAMS_LABEL),usesPathParams);
    EnableWindow(GetDlgItem(dlg,IDC_ITEM_BROWSE), canBrowse && usesPathParams);
    SetDlgItemTextW(dlg, IDC_ITEM_BROWSE, L"Browse");
}
static void item_fill_type_combo(HWND h){
    const struct { ConfigItemType t; const WCHAR* n; } types[]={
        {CI_SEPARATOR,L"Separator"},
        {CI_URI,L"URI"},
        {CI_FILE,L"File"},
        {CI_CMD,L"Command"},
        {CI_FOLDER,L"Folder"},
        {CI_FOLDER_SUBMENU,L"Folder (submenu)"},
        {CI_POWER_SLEEP,L"Sleep"},
        {CI_POWER_HIBERNATE,L"Hibernate"},
        {CI_POWER_SHUTDOWN,L"Shutdown"},
        {CI_POWER_RESTART,L"Restart"},
        {CI_POWER_LOCK,L"Lock"},
        {CI_POWER_LOGOFF,L"Logoff"},
        {CI_RECENT_SUBMENU,L"Recent"},
        {CI_POWER_MENU,L"Power Menu"},
        {CI_THISPC,L"This PC"},
        {CI_HOME,L"Home"},
        {CI_TASKKILL,L"Task Kill"}
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
        // Preselect type
        int count=(int)SendMessageW(hType,CB_GETCOUNT,0,0); for(int i=0;i<count;i++){ if((ConfigItemType)SendMessageW(hType,CB_GETITEMDATA,i,0)==ctx->tmp.type){ SendMessageW(hType,CB_SETCURSEL,i,0); break; } }
        SetDlgItemTextW(dlg,IDC_ITEM_LABEL,ctx->tmp.label);
        SetDlgItemTextW(dlg,IDC_ITEM_PATH,ctx->tmp.path);
        SetDlgItemTextW(dlg,IDC_ITEM_PARAMS,ctx->tmp.params);
        item_update_path_controls(dlg, ctx->tmp.type);
        return TRUE; }
    case WM_COMMAND:
        switch(LOWORD(wParam)){
        case IDC_ITEM_TYPE_COMBO:{
            if(HIWORD(wParam)==CBN_SELCHANGE){
                item_update_path_controls(dlg, item_get_selected_type(dlg));
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
            GetDlgItemTextW(dlg,IDC_ITEM_LABEL,ctx->tmp.label,ARRAYSIZE(ctx->tmp.label));
            GetDlgItemTextW(dlg,IDC_ITEM_PATH,ctx->tmp.path,ARRAYSIZE(ctx->tmp.path));
            GetDlgItemTextW(dlg,IDC_ITEM_PARAMS,ctx->tmp.params,ARRAYSIZE(ctx->tmp.params));
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
    if(outItem) ctx.tmp=*outItem; else ctx.tmp.type=CI_FILE;
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
static void menu_action_delete(SettingsState* st, HWND pg){ if(!st) return; HWND lv=GetDlgItem(pg,IDC_MENU_LIST); int idx=menu_get_selected_index(lv); if(idx<0||idx>=st->workingCount) return; if(MessageBoxW(pg,L"Delete selected item?",L"Confirm",MB_ICONQUESTION|MB_OKCANCEL)!=IDOK) return; for(int i=idx;i<st->workingCount-1;i++) st->workingItems[i]=st->workingItems[i+1]; st->workingCount--; st->workingDirty=TRUE; refresh_lists(st); }
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


static void init_tabs(HWND dlg, SettingsState* st){
    st->hTabs=GetDlgItem(dlg,IDC_SETTINGS_TABS);
    TCITEMW ti; ZeroMemory(&ti,sizeof(ti)); ti.mask=TCIF_TEXT; WCHAR label[32];
    const WCHAR* names[] = {L"General",L"Placement",L"Menu",L"Icons",L"Sorting",L"Controls",L"Appearance",L"Advanced"};
    for(int i=0;i<PAGE_COUNT;i++){
        lstrcpynW(label,names[i],ARRAYSIZE(label));
        ti.pszText=label;
        TabCtrl_InsertItem(st->hTabs,i,&ti);
    }

    RECT rcClient; GetClientRect(st->hTabs,&rcClient); // client of tab control (0,0 origin)
    RECT rcDisplay = rcClient; TabCtrl_AdjustRect(st->hTabs,FALSE,&rcDisplay); // area for pages (still tab-local)

    // Map to dialog coordinates.
    MapWindowPoints(st->hTabs, dlg, (POINT*)&rcDisplay, 2);

    const int inset = 3;
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
    st->pages[PAGE_APPEARANCE]=CreateDialogParamW(GetModuleHandleW(NULL),MAKEINTRESOURCEW(IDD_PAGE_APPEARANCE),dlg,PageDlgProc,(LPARAM)st);
    st->pages[PAGE_ADVANCED]=CreateDialogParamW(GetModuleHandleW(NULL),MAKEINTRESOURCEW(IDD_PAGE_ADVANCED),dlg,PageDlgProc,(LPARAM)st);

    layout_pages(st, x, y, w, h);
    show_page(st, PAGE_GENERAL);

    // Attach st pointer to parent dialog for child retrieval (used in loads)
    SetWindowLongPtrW(dlg,GWLP_USERDATA,(LONG_PTR)st);
    working_clone(st);
    General_Load(st->pages[PAGE_GENERAL],st->cfg);
    Placement_Load(st->pages[PAGE_PLACEMENT],st->cfg);
    Menu_Load(st->pages[PAGE_MENU],st->cfg);
    Icons_Load(st->pages[PAGE_ICONS],st->cfg);
    Sorting_Load(st->pages[PAGE_SORTING],st->cfg);
    Controls_Load(st->pages[PAGE_CONTROLS],st->cfg);
    Appearance_Load(st->pages[PAGE_APPEARANCE],st->cfg);
    Advanced_Load(st->pages[PAGE_ADVANCED],st->cfg);
}
// Re-layout controls inside a page (currently only Menu & Icons) when page resized
static void relayout_page(HWND page, int w, int h){
    if(!page) return;
    // Determine which page by child IDs present
    HWND lvMenu=GetDlgItem(page,IDC_MENU_LIST);
    if(lvMenu){
        // Compute natural widest button text to size buttons
        int margin=6; int btnSpacing=4; int btnPad=14; // horizontal padding inside button text area
        const int btnIds[]={IDC_MENU_ADD,IDC_MENU_EDIT,IDC_MENU_DELETE,IDC_MENU_UP,IDC_MENU_DOWN};
        int btnWidth=70; // minimum
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
        int btnWidth=70; HDC hdc=GetDC(page); HFONT hf=(HFONT)SendMessageW(page,WM_GETFONT,0,0); HFONT of=(HFONT)SelectObject(hdc,hf); for(int i=0;i< (int)(sizeof(btnIds)/sizeof(btnIds[0])); i++){ HWND b=GetDlgItem(page,btnIds[i]); if(!b) continue; WCHAR txt[64]; if(GetWindowTextW(b,txt,ARRAYSIZE(txt))){ SIZE sz; if(GetTextExtentPoint32W(hdc,txt,lstrlenW(txt),&sz)){ int wCandidate=sz.cx+btnPad; if(wCandidate>btnWidth) btnWidth=wCandidate; } } } SelectObject(hdc,of); ReleaseDC(page,hdc); if(btnWidth>160) btnWidth=160; int listRightPadding=(btnWidth + 2*margin);
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
}
static void show_page(SettingsState* st,int idx){ for(int i=0;i<PAGE_COUNT;i++){ if(st->pages[i]) ShowWindow(st->pages[i], i==idx?SW_SHOW:SW_HIDE); } }
static BOOL save_all(SettingsState* st) {
    if(!st) return FALSE;
    if(st->workingDirty){ working_commit(st); }
    int generalResult = General_Save(st->pages[0],st->cfg);
    if (generalResult == 2)
        st->reloadNeeded = TRUE;
    BOOL any = (generalResult != 0);
    any |= Placement_Save(st->pages[PAGE_PLACEMENT],st->cfg);
    any |= Controls_Save(st->pages[PAGE_CONTROLS],st->cfg);
    any |= Appearance_Save(st->pages[PAGE_APPEARANCE],st->cfg);
    any |= Advanced_Save(st->pages[PAGE_ADVANCED],st->cfg);
    any |= Menu_Save(st->pages[PAGE_MENU],st->cfg);
    any |= Icons_Save(st->pages[PAGE_ICONS],st->cfg);
    any |= Sorting_Save(st->pages[PAGE_SORTING],st->cfg);
    return any;
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
        case WM_INITDIALOG:
            st = (SettingsState*)calloc(1, sizeof(SettingsState));
            st->cfg = (Config*)lParam;
            // Store original power option states
            st->origExcludeSleep = st->cfg->excludeSleep;
            st->origExcludeHibernate = st->cfg->excludeHibernate;
            st->origExcludeShutdown = st->cfg->excludeShutdown;
            st->origExcludeRestart = st->cfg->excludeRestart;
            st->origExcludeLock = st->cfg->excludeLock;
            st->origExcludeLogoff = st->cfg->excludeLogoff;
            {
                RECT rc; GetWindowRect(dlg, &rc); st->baseW = rc.right - rc.left; st->baseH = rc.bottom - rc.top;
                LONG_PTR ex = GetWindowLongPtrW(dlg, GWL_EXSTYLE); SetWindowLongPtrW(dlg, GWL_EXSTYLE, ex | WS_EX_APPWINDOW);
            }
            apply_dialog_icon(dlg, st->cfg);
            init_tabs(dlg, st);
            // Center window manually (DS_CENTER removed). Use work area.
            {
                RECT rc; GetWindowRect(dlg, &rc); RECT wa; SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
                int w = rc.right - rc.left; int h = rc.bottom - rc.top;
                int x = wa.left + ((wa.right - wa.left) - w) / 2; int y = wa.top + ((wa.bottom - wa.top) - h) / 2;
                SetWindowPos(dlg, NULL, x, y, 0, 0, SWP_NOZORDER | SWP_NOSIZE);
            }
            return TRUE;
        case WM_SIZE:
            if (st) {
                int cw = LOWORD(lParam); int ch = HIWORD(lParam);
                // Layout: buttons anchored bottom-right, tabs fill remaining above button row
                HWND hApply = GetDlgItem(dlg, IDC_APPLY);
                HWND hSave = GetDlgItem(dlg, IDC_SAVEEXIT);
                HWND hExit = GetDlgItem(dlg, IDC_EXIT);
                HWND hCancel = GetDlgItem(dlg, IDC_CANCEL);
                RECT rb; GetWindowRect(hApply, &rb); MapWindowPoints(NULL, dlg, (POINT*)&rb, 2); int btnH = rb.bottom - rb.top; int btnW = rb.right - rb.left;
                int margin = 6; int gap = 4;
                int cancelW, cancelH; RECT rc; GetWindowRect(hCancel, &rc); MapWindowPoints(NULL, dlg, (POINT*)&rc, 2); cancelW = rc.right - rc.left; cancelH = rc.bottom - rc.top;
                int exitW, exitH; GetWindowRect(hExit, &rc); MapWindowPoints(NULL, dlg, (POINT*)&rc, 2); exitW = rc.right - rc.left; exitH = rc.bottom - rc.top;
                int saveW, saveH; GetWindowRect(hSave, &rc); MapWindowPoints(NULL, dlg, (POINT*)&rc, 2); saveW = rc.right - rc.left; saveH = rc.bottom - rc.top;
                int applyW, applyH; GetWindowRect(hApply, &rc); MapWindowPoints(NULL, dlg, (POINT*)&rc, 2); applyW = rc.right - rc.left; applyH = rc.bottom - rc.top;
                int btnY = ch - margin - btnH;
                // Total width of button cluster with equal gaps
                int totalW = applyW + saveW + exitW + cancelW + gap * 3;
                // Center cluster; keep at least margin from edges
                int clusterX = (cw - totalW) / 2; if (clusterX < margin) clusterX = margin; if (clusterX + totalW > cw - margin) clusterX = cw - margin - totalW;
                int xApply = clusterX;
                int xSave = xApply + applyW + gap;
                int xExit = xSave + saveW + gap;
                int xCancel = xExit + exitW + gap;
                SetWindowPos(hApply, NULL, xApply, btnY, 0, 0, SWP_NOZORDER | SWP_NOSIZE);
                SetWindowPos(hSave, NULL, xSave, btnY, 0, 0, SWP_NOZORDER | SWP_NOSIZE);
                SetWindowPos(hExit, NULL, xExit, btnY, 0, 0, SWP_NOZORDER | SWP_NOSIZE);
                SetWindowPos(hCancel, NULL, xCancel, btnY, 0, 0, SWP_NOZORDER | SWP_NOSIZE);
                // Resize tab control
                HWND hTabs = st->hTabs; if (hTabs) {
                    RECT rTabs; GetWindowRect(hTabs, &rTabs); MapWindowPoints(NULL, dlg, (POINT*)&rTabs, 2);
                    int tabsX = rTabs.left; int tabsY = rTabs.top; // keep original top-left
                    int tabsW = cw - tabsX - margin;
                    int tabsH = btnY - tabsY - margin;
                    if (tabsW < 100) tabsW = 100; if (tabsH < 100) tabsH = 100;
                    SetWindowPos(hTabs, NULL, tabsX, tabsY, tabsW, tabsH, SWP_NOZORDER);
                    // Adjust pages to new tab display area
                    RECT rcClient; GetClientRect(hTabs, &rcClient); RECT rcDisplay = rcClient; TabCtrl_AdjustRect(hTabs, FALSE, &rcDisplay); MapWindowPoints(hTabs, dlg, (POINT*)&rcDisplay, 2);
                    const int inset = 3;
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
            if (nh->idFrom == IDC_SETTINGS_TABS && nh->code == TCN_SELCHANGE) {
                int sel = TabCtrl_GetCurSel(st->hTabs);
                show_page(st, sel);
                if (sel == PAGE_MENU) menu_update_buttons(st, st->pages[PAGE_MENU]);
                if (sel == PAGE_ICONS) icons_update_buttons(st, st->pages[PAGE_ICONS]);
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
            break;
        }
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDC_APPLY:
                    save_all(st);
                    // Do NOT reload app on Apply for tray icon changes
                    return TRUE;
                case IDC_SAVEEXIT:
                    save_all(st);
                    // Only reload app if tray icon settings changed and Save & Close was clicked
                    if (st && st->reloadNeeded && g_settingsOwnerHwnd) {
                        PostMessageW(g_settingsOwnerHwnd, WM_COMMAND, 10010, 0);
                    }
                    EndDialog(dlg, IDOK);
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
                case IDC_MENU_ADD: menu_action_add(st, st->pages[PAGE_MENU]); return TRUE;
                case IDC_MENU_EDIT: menu_action_edit(st, st->pages[PAGE_MENU]); return TRUE;
                case IDC_MENU_DELETE: menu_action_delete(st, st->pages[PAGE_MENU]); return TRUE;
                case IDC_MENU_UP: menu_action_move(st, st->pages[PAGE_MENU], -1); return TRUE;
                case IDC_MENU_DOWN: menu_action_move(st, st->pages[PAGE_MENU], 1); return TRUE;
                case IDC_ICON_BROWSE_FILE: icons_action_browse(st, st->pages[PAGE_ICONS], 0); return TRUE;
                case IDC_ICON_BROWSE_LIGHT: icons_action_browse(st, st->pages[PAGE_ICONS], 1); return TRUE;
                case IDC_ICON_BROWSE_DARK: icons_action_browse(st, st->pages[PAGE_ICONS], 2); return TRUE;
                case IDC_ICON_CLEAR: icons_action_clear(st, st->pages[PAGE_ICONS]); return TRUE;
                case IDC_POINTERRELATIVE: {
                    // Live enable/disable of the Ignore Relative dropdown
                    if (st && st->pages[PAGE_PLACEMENT]) {
                        HWND hRelative = GetDlgItem(st->pages[PAGE_PLACEMENT], IDC_IGNORE_RELATIVE_COMBO);
                        HWND hRelativeLabel = GetDlgItem(st->pages[PAGE_PLACEMENT], IDC_IGNORE_RELATIVE_LABEL);
                        BOOL enabled = IsDlgButtonChecked(st->pages[PAGE_PLACEMENT], IDC_POINTERRELATIVE) == BST_CHECKED;
                        EnableWindow(hRelative, enabled);
                        if (hRelativeLabel) EnableWindow(hRelativeLabel, enabled);
                    }
                    return TRUE;
                }
                case IDC_ROOT_MENU_ICON_SIZE: {
                    if (st && st->pages[PAGE_APPEARANCE]) {
                        HWND hAnim = GetDlgItem(st->pages[PAGE_APPEARANCE], IDC_ANIMATION_COMBO);
                        BOOL enabled = IsDlgButtonChecked(st->pages[PAGE_APPEARANCE], IDC_ROOT_MENU_ICON_SIZE) == BST_CHECKED;
                        EnableWindow(hAnim, enabled);
                        {
                            HWND hAnimLabel = GetDlgItem(st->pages[PAGE_APPEARANCE], IDC_ANIMATION_LABEL);
                            if (hAnimLabel) EnableWindow(hAnimLabel, enabled);
                        }
                    }
                    return TRUE;
                }
            }
            break;
    }
    return FALSE;
}
