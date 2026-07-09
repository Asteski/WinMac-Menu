#include "config.h"
#include "util.h"
#include <shlwapi.h>
#include <shlobj.h>
#include <stdio.h>

// Expand %ENVVAR% sequences in-place (destination buffer provided).
// If no '%' is present we skip calling the API for performance.
static void expand_env(const WCHAR* in, WCHAR* out, size_t cchOut) {
    if (!in || !out || cchOut == 0) return;
    // Quick scan for '%'
    const WCHAR* p = in; BOOL hasPct = FALSE;
    while (*p) { if (*p == L'%') { hasPct = TRUE; break; } ++p; }
    if (!hasPct) { lstrcpynW(out, in, (int)cchOut); return; }
    WCHAR tmp[4096];
    DWORD n = ExpandEnvironmentStringsW(in, tmp, ARRAYSIZE(tmp));
    if (n == 0 || n > ARRAYSIZE(tmp)) { // failure or truncated; fallback copy
        lstrcpynW(out, in, (int)cchOut);
        return;
    }
    lstrcpynW(out, tmp, (int)cchOut);
}

// Trim leading/trailing whitespace in-place for small config strings.
static void trim_inplace(WCHAR* s) {
    if (!s || !*s) return;
    // Leading
    WCHAR* p = s;
    while (*p == L' ' || *p == L'\t' || *p == L'\r' || *p == L'\n') ++p;
    if (p != s) {
        WCHAR* d = s; while (*p) *d++ = *p++; *d = 0;
    }
    // Trailing
    size_t len = lstrlenW(s);
    while (len > 0) {
        WCHAR ch = s[len - 1];
        if (ch == L' ' || ch == L'\t' || ch == L'\r' || ch == L'\n') {
            s[len - 1] = 0;
            --len;
        } else {
            break;
        }
    }
}

static BOOL parse_bool_default(const WCHAR* value, BOOL fallback) {
    WCHAR buf[32];
    if (!value || !value[0]) return fallback;
    lstrcpynW(buf, value, ARRAYSIZE(buf));
    WCHAR* comment = wcspbrk(buf, L";#");
    if (comment) *comment = 0;
    trim_inplace(buf);
    if (!buf[0]) return fallback;
    if (!lstrcmpiW(buf, L"true") || !lstrcmpiW(buf, L"1") || !lstrcmpiW(buf, L"yes") || !lstrcmpiW(buf, L"on")) return TRUE;
    if (!lstrcmpiW(buf, L"false") || !lstrcmpiW(buf, L"0") || !lstrcmpiW(buf, L"no") || !lstrcmpiW(buf, L"off")) return FALSE;
    return fallback;
}


// Write a default config.ini without comments
static void write_default_ini(const WCHAR* path) {
    const char* ini =
        "[General]\r\n"\
        "FolderSubmenuDepth=4\r\n"\
        "RunInBackground=true\r\n"\
        "ShowDotfiles=false\r\n"\
        "ShowFileExtensions=true\r\n"\
        "ShowFolderIcons=true\r\n"\
        "ShowFileIcons=true\r\n"\
        "KeepMenuOpenAfterContextAction=false\r\n"\
        "ShowHidden=false\r\n"\
        "ShowIcons=true\r\n"\
        "ShowOnLaunch=false\r\n"\
        "ShowTrayIcon=true\r\n"\
        "MonochromeTrayIcon=false\r\n"\
        "StartOnLogin=false\r\n"\
        "\r\n"\
        "[Placement]\r\n"\
        "PointerRelative=true\r\n"\
        "HOffset=0\r\n"\
        "VOffset=0\r\n"\
        "\r\n"\
        "[Appearance]\r\n"\
        "Renderer=Win32\r\n"\
        "UseWinUI3Menu=false\r\n"\
        "WinUISize=compact\r\n"\
        "KeepLargeMenuHighlightTextColor=false\r\n"\
        "LargeMenuHighlightFrame=background\r\n"\
        "LargeMenuAnimation=bottom\r\n"\
        "\r\n"\
        "[Sorting]\r\n"\
        "SortBy=name\r\n"\
        "SortDirection=ascending\r\n"\
        "FoldersFirst=true\r\n"\
        "\r\n"\
        "[Controls]\r\n"\
        "WindowsKey=false\r\n"\
        "ShiftWindowsKey=false\r\n"\
        "WindowsKeyX=false\r\n"\
        "LeftClick=false\r\n"\
        "RightClick=false\r\n"\
        "MiddleClick=true\r\n"\
        "ShiftLeftClick=false\r\n"\
        "ShiftRightClick=false\r\n"\
        "ShiftMiddleClick=false\r\n"\
        "IgnoreTriggersWhenFullscreen=false\r\n"\
        "FullscreenExclusionList=\r\n"\
        "\r\n"\
        "[Menu]\r\n"\
        "Item1=Apps and Features|URI|ms-settings:appsfeatures\r\n"\
        "Item2=About Windows|URI|winver\r\n"\
        "Item3=---\r\n"\
        "Item4=System Settings|URI|ms-settings:\r\n"\
        "Item5=File Explorer|THISPC\r\n"\
        "Item6=User Profile|HOME\r\n"\
        "Item7=---\r\n"\
        "Item8=Recent Items|RECENT\r\n"\
        "Item9=---\r\n"\
        "Item10=End task|TASKKILL\r\n"\
        "Item11=---\r\n"\
        "Item12=Sleep|POWER_SLEEP\r\n"\
        "Item13=Restart|POWER_RESTART\r\n"\
        "Item14=Shut down|POWER_SHUTDOWN\r\n"\
        "Item15=---\r\n"\
        "Item16=Event Viewer|URI|eventvwr\r\n"\
        "Item17=Task Scheduler|URI|taskschd.msc\r\n"\
        "Item18=Task Manager|URI|taskmgr\r\n"\
        "Item19=---\r\n"\
        "Item20=Lock screen|POWER_LOCK\r\n"\
        "Item21=Sign out %USERNAME%|POWER_LOGOFF\r\n"\
        "\r\n"\
        "[Icons]\r\n"\
        "Icon1=shell32.dll,-271\r\n"\
        "Icon2=shell32.dll,-1001\r\n"\
        "Icon4=shell32.dll,-16826\r\n"\
        "Icon5=imageres.dll,-5325\r\n"\
        "Icon6=imageres.dll,-88\r\n"\
        "Icon8=shell32.dll,-327\r\n"\
        "Icon10=shell32.dll,-200\r\n"\
        "Icon16=eventvwr.exe,0\r\n"\
        "Icon17=powercpl.dll,-513\r\n"\
        "Icon18=taskmgr.exe,0\r\n"\
        "\r\n"\
        "[RecentItems]\r\n"\
        "RecentLabel=fullpath\r\n"\
        "RecentMax=12\r\n"\
        "RecentShowCleanItems=true\r\n"\
        "RecentShowExtensions=true\r\n"\
        "SeparateItems=true\r\n"\
        "RecentShowIcons=true\r\n"\
        "\r\n"\
        "[TaskKill]\r\n"\
        "TaskKillAllDesktops=true\r\n"\
        "TaskKillExcludes=\r\n"\
        "TaskKillIgnoreSystem=true\r\n"\
        "TaskKillListWindows=false\r\n"\
        "TaskKillMax=24\r\n"\
        "TaskKillShowIcons=true\r\n"\
        "\r\n"\
        "[ThisPC]\r\n"\
        "ThisPCAsSubmenu=true\r\n"\
        "ThisPCItemsAsSubmenus=true\r\n"\
        "ThisPCShowIcons=true\r\n"\
        "\r\n"\
        "[Home]\r\n"\
        "HomeAsSubmenu=true\r\n"\
        "HomeItemsAsSubmenus=true\r\n"\
        "HomeShowIcons=true\r\n";

    HANDLE hf = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hf != INVALID_HANDLE_VALUE) {
        DWORD toWrite = (DWORD)lstrlenA(ini);
        DWORD written = 0;
        WriteFile(hf, ini, toWrite, &written, NULL);
        CloseHandle(hf);
    }
}

static WCHAR g_defaultIniPath[MAX_PATH];

static void exe_config_path(WCHAR* buf, size_t cch) {
    GetModuleFileNameW(NULL, buf, (DWORD)cch);
    PathRemoveFileSpecW(buf);
    PathAppendW(buf, L"config.ini");
}

BOOL config_ensure(Config* out) {
    if (!out) return FALSE;
    if (g_defaultIniPath[0]) {
        lstrcpynW(out->iniPath, g_defaultIniPath, ARRAYSIZE(out->iniPath));
    } else {
        exe_config_path(out->iniPath, MAX_PATH);
    }
    if (!PathFileExistsW(out->iniPath)) {
        write_default_ini(out->iniPath);
    }
    return TRUE;
}

static ConfigItemType parse_type(const WCHAR* s) {
    if (!s) return CI_SEPARATOR;
    if (!lstrcmpiW(s, L"SEPARATOR")) return CI_SEPARATOR;
    if (!lstrcmpiW(s, L"CATEGORY")) return CI_CATEGORY;
    if (!lstrcmpiW(s, L"CAT")) return CI_CATEGORY;
    if (!lstrcmpiW(s, L"URI")) return CI_URI;
    if (!lstrcmpiW(s, L"FILE")) return CI_FILE;
    if (!lstrcmpiW(s, L"CMD")) return CI_CMD;
    if (!lstrcmpiW(s, L"FOLDER")) return CI_FOLDER;
    if (!lstrcmpiW(s, L"FOLDER_SUBMENU")) return CI_FOLDER_SUBMENU;
    if (!lstrcmpiW(s, L"POWER_SLEEP")) return CI_POWER_SLEEP;
    if (!lstrcmpiW(s, L"POWER_SHUTDOWN")) return CI_POWER_SHUTDOWN;
    if (!lstrcmpiW(s, L"POWER_RESTART")) return CI_POWER_RESTART;
    if (!lstrcmpiW(s, L"POWER_LOCK")) return CI_POWER_LOCK;
    if (!lstrcmpiW(s, L"POWER_LOGOFF")) return CI_POWER_LOGOFF;
    if (!lstrcmpiW(s, L"POWER_HIBERNATE")) return CI_POWER_HIBERNATE;
    if (!lstrcmpiW(s, L"RECENT_SUBMENU")) return CI_RECENT_SUBMENU;
    if (!lstrcmpiW(s, L"RECENT")) return CI_RECENT_SUBMENU;
    if (!lstrcmpiW(s, L"POWER_MENU")) return CI_POWER_MENU;
    if (!lstrcmpiW(s, L"TASKKILL")) return CI_TASKKILL;
    if (!lstrcmpiW(s, L"THISPC")) return CI_THISPC;
    if (!lstrcmpiW(s, L"HOME")) return CI_HOME;
    return CI_SEPARATOR;
}

static ControlActionType parse_control_action(const WCHAR* s) {
    if (!s) return CA_NOTHING;
    if (!lstrcmpiW(s, L"Nothing")) return CA_NOTHING;
    if (!lstrcmpiW(s, L"WinMacMenu") || !lstrcmpiW(s, L"WinMac Menu")) return CA_WINMAC_MENU;
    if (!lstrcmpiW(s, L"WindowsMenu") || !lstrcmpiW(s, L"Windows Menu") || !lstrcmpiW(s, L"WindowsStartMenu") || !lstrcmpiW(s, L"Windows Start Menu")) return CA_WINDOWS_MENU;
    if (!lstrcmpiW(s, L"CustomCommand") || !lstrcmpiW(s, L"Custom Command") || !lstrcmpiW(s, L"Command")) return CA_CUSTOM_COMMAND;
    return CA_NOTHING;
}

static int parse_menu(Config* cfg) {
    cfg->count = 0;
    WCHAR section[] = L"Menu";
    for (int i = 1; i <= 64; ++i) {
        WCHAR key[32]; wsprintfW(key, L"Item%d", i);
        WCHAR line[1024] = {0};
        GetPrivateProfileStringW(section, key, L"", line, ARRAYSIZE(line), cfg->iniPath);
        if (!line[0]) continue;

        // Expected: Label|TYPE|Path|Params(optional)|Icon(optional)
        WCHAR* p = line;
        WCHAR* label = p;
        WCHAR* type = NULL;
        WCHAR* path = NULL;
        WCHAR* params = NULL;
        WCHAR* icon = NULL;
        for (int part=0; part<5; ++part) {
            WCHAR* bar = wcschr(p, L'|');
            if (!bar) {
                if (part==0) { type = L"SEPARATOR"; p = L""; }
                else if (part==1) { type = p; p = L""; }
                else if (part==2) { path = p; p = L""; }
                else if (part==3) { params = p; p = L""; }
                else if (part==4) { icon = p; p = L""; }
                break;
            }
            *bar = 0;
            if (part==0) type = bar+1;
            else if (part==1) path = bar+1;
            else if (part==2) params = bar+1;
            else if (part==3) icon = bar+1;
            p = bar+1;
        }
        ConfigItem* it = &cfg->items[cfg->count++];
        // Expand environment variables in label
        expand_env(label, it->label, ARRAYSIZE(it->label));
        it->type = parse_type(type);
        if (path) expand_env(path, it->path, ARRAYSIZE(it->path)); else it->path[0]=0;
        if (params) expand_env(params, it->params, ARRAYSIZE(it->params)); else it->params[0]=0;
    if (icon) expand_env(icon, it->iconPath, ARRAYSIZE(it->iconPath)); else it->iconPath[0]=0;
    // Initialize theme-specific per-item icons empty; filled via [IconsLight]/[IconsDark] sections
    it->iconPathLight[0] = 0;
    it->iconPathDark[0] = 0;
        it->submenu = (it->type == CI_FOLDER_SUBMENU || it->type == CI_RECENT_SUBMENU);
        if (it->type == CI_THISPC && cfg->thisPCAsSubmenu) it->submenu = TRUE;
        if (it->type == CI_HOME && cfg->homeAsSubmenu) it->submenu = TRUE;

        // Allow FOLDER items to set mode via 4th field: "submenu" or "link"
        if ((it->type == CI_FOLDER || it->type == CI_THISPC || it->type == CI_HOME) && it->params[0]) {
            WCHAR pLower[256]; lstrcpynW(pLower, it->params, ARRAYSIZE(pLower));
            for (WCHAR* q=pLower; *q; ++q) *q = (WCHAR)towlower(*q);
            if (wcsstr(pLower, L"submenu")) it->submenu = TRUE;
            else if (wcsstr(pLower, L"link")) it->submenu = FALSE;
            
            // Experimental inline expansion: include token "inline" to inject folder contents at root
            if (wcsstr(pLower, L"inline")) it->inlineExpand = TRUE; 
            else if ((it->type == CI_THISPC || it->type == CI_HOME) && !it->submenu) it->inlineExpand = TRUE; // THISPC/HOME defaults to inline
            else it->inlineExpand = FALSE;

            if (it->inlineExpand) {
                BOOL explicitNoTitle = (wcsstr(pLower, L"notitle") || wcsstr(pLower, L"noheader"));
                if (explicitNoTitle) {
                    it->inlineNoHeader = TRUE;
                } else if (wcsstr(pLower, L"title")) {
                    it->inlineNoHeader = FALSE;
                } else {
                    // Default behavior if neither specified
                    if (it->type == CI_THISPC || it->type == CI_HOME) it->inlineNoHeader = TRUE; // Default hidden for THISPC/HOME
                    else it->inlineNoHeader = FALSE; // Default shown for FOLDER
                }
            } else {
                it->inlineNoHeader = FALSE;
            }

            if (it->inlineExpand && wcsstr(pLower, L"inlineopen")) it->inlineOpen = TRUE; else it->inlineOpen = FALSE;
        } else if (it->type == CI_FOLDER) {
            it->inlineExpand = FALSE;
            it->inlineNoHeader = FALSE;
            it->inlineOpen = FALSE;
        } else if (it->type == CI_THISPC || it->type == CI_HOME) {
            // Default to inline/notitle for THISPC/HOME if no params provided
            it->inlineExpand = TRUE;
            it->inlineNoHeader = TRUE;
            it->inlineOpen = FALSE;
        } else {
            it->inlineExpand = FALSE; // non-folder
            it->inlineNoHeader = FALSE;
            it->inlineOpen = FALSE;
        }
        if (cfg->count >= 64) break;
    }
    return cfg->count;
}

static void parse_icons(Config* cfg) {
    // Optional [Icons] section: Icon1..IconN map to Item1..ItemN
    WCHAR section[] = L"Icons";
    for (int i = 1; i <= cfg->count; ++i) {
        WCHAR key[32]; wsprintfW(key, L"Icon%d", i);
        WCHAR path[MAX_PATH] = {0};
        GetPrivateProfileStringW(section, key, L"", path, ARRAYSIZE(path), cfg->iniPath);
        if (path[0]) {
            // Expand environment variables in icon path
            WCHAR expanded[MAX_PATH];
            expand_env(path, expanded, ARRAYSIZE(expanded));
            lstrcpynW(cfg->items[i-1].iconPath, expanded, ARRAYSIZE(cfg->items[i-1].iconPath));
        }
    }
    // Optional [IconsLight] and [IconsDark]
    WCHAR sectionL[] = L"IconsLight";
    WCHAR sectionD[] = L"IconsDark";
    for (int i = 1; i <= cfg->count; ++i) {
        WCHAR key[32]; wsprintfW(key, L"Icon%d", i);
        WCHAR pathL[MAX_PATH] = {0};
        WCHAR pathD[MAX_PATH] = {0};
        GetPrivateProfileStringW(sectionL, key, L"", pathL, ARRAYSIZE(pathL), cfg->iniPath);
        GetPrivateProfileStringW(sectionD, key, L"", pathD, ARRAYSIZE(pathD), cfg->iniPath);
        if (pathL[0]) {
            WCHAR expandedL[MAX_PATH];
            expand_env(pathL, expandedL, ARRAYSIZE(expandedL));
            lstrcpynW(cfg->items[i-1].iconPathLight, expandedL, ARRAYSIZE(cfg->items[i-1].iconPathLight));
        }
        if (pathD[0]) {
            WCHAR expandedD[MAX_PATH];
            expand_env(pathD, expandedD, ARRAYSIZE(expandedD));
            lstrcpynW(cfg->items[i-1].iconPathDark,  expandedD, ARRAYSIZE(cfg->items[i-1].iconPathDark));
        }
    }
}

BOOL config_load(Config* out) {
    if (!out) return FALSE;
    config_ensure(out);
    // Explicit defaults for control flags so missing keys don't inherit prior memory/state
    // Removed resident/trigger controls
    // RunInBackground and tray icon
    WCHAR buf[32];
    GetPrivateProfileStringW(L"General", L"RunInBackground", L"true", buf, ARRAYSIZE(buf), out->iniPath);
    trim_inplace(buf);
    out->runInBackground = (!lstrcmpiW(buf, L"true") || !lstrcmpiW(buf, L"1"));
    // ShowOnLaunch (default true) controls whether we show the initial menu when starting in background
    GetPrivateProfileStringW(L"General", L"ShowOnLaunch", L"true", buf, ARRAYSIZE(buf), out->iniPath);
    trim_inplace(buf);
    out->showOnLaunch = (!lstrcmpiW(buf, L"true") || !lstrcmpiW(buf, L"1"));
    GetPrivateProfileStringW(L"General", L"ShowTrayIcon", L"true", buf, ARRAYSIZE(buf), out->iniPath);
    trim_inplace(buf);
    out->showTrayIcon = (!lstrcmpiW(buf, L"true") || !lstrcmpiW(buf, L"1"));
    // Start on login (registry Run entry)
    GetPrivateProfileStringW(L"General", L"StartOnLogin", L"false", buf, ARRAYSIZE(buf), out->iniPath);
    trim_inplace(buf);
    out->startOnLogin = (!lstrcmpiW(buf, L"true") || !lstrcmpiW(buf, L"1"));

    out->recentMax = GetPrivateProfileIntW(L"RecentItems", L"RecentMax", 12, out->iniPath);
    out->folderMaxDepth = GetPrivateProfileIntW(L"General", L"FolderSubmenuDepth", 4, out->iniPath);
    if (out->folderMaxDepth < 1) out->folderMaxDepth = 1; if (out->folderMaxDepth > 4) out->folderMaxDepth = 4;
    
    // Fallback default now single
    GetPrivateProfileStringW(L"General", L"FolderSubmenuOpen", L"single", buf, ARRAYSIZE(buf), out->iniPath);
    trim_inplace(buf);
    out->folderSingleClickOpen = (!lstrcmpiW(buf, L"single")) ? TRUE : FALSE;
    // Global toggle for showing "Open <folder>" entry in submenus (default true)
    GetPrivateProfileStringW(L"General", L"FolderShowOpenEntry", L"true", buf, ARRAYSIZE(buf), out->iniPath);
    trim_inplace(buf);
    out->folderShowOpenEntry = (!lstrcmpiW(buf, L"true") || !lstrcmpiW(buf, L"1"));

    // Global toggle for showing icons in Recent submenu (default false)
    GetPrivateProfileStringW(L"RecentItems", L"RecentShowIcons", L"false", buf, ARRAYSIZE(buf), out->iniPath);
    trim_inplace(buf);
    out->recentShowIcons = (!lstrcmpiW(buf, L"true") || !lstrcmpiW(buf, L"1"));
    GetPrivateProfileStringW(L"RecentItems", L"SeparateItems", L"", buf, ARRAYSIZE(buf), out->iniPath);
    trim_inplace(buf);
    if (!buf[0]) {
        GetPrivateProfileStringW(L"General", L"SeparateItems", L"", buf, ARRAYSIZE(buf), out->iniPath);
        trim_inplace(buf);
    }
    if (!buf[0]) {
        GetPrivateProfileStringW(L"RecentItems", L"RecentGroupFoldersFirst", L"true", buf, ARRAYSIZE(buf), out->iniPath);
        trim_inplace(buf);
    }
    out->separateItems = parse_bool_default(buf, TRUE);
    
    // Sorting options
    GetPrivateProfileStringW(L"Sorting", L"SortBy", L"name", buf, ARRAYSIZE(buf), out->iniPath);
    trim_inplace(buf);
    if (!lstrcmpiW(buf, L"date_modified") || !lstrcmpiW(buf, L"datemodified") || !lstrcmpiW(buf, L"modified")) out->sortField = SORT_DATE_MODIFIED;
    else if (!lstrcmpiW(buf, L"date_created") || !lstrcmpiW(buf, L"datecreated") || !lstrcmpiW(buf, L"created")) out->sortField = SORT_DATE_CREATED;
    else if (!lstrcmpiW(buf, L"type")) out->sortField = SORT_TYPE;
    else if (!lstrcmpiW(buf, L"size")) out->sortField = SORT_SIZE;
    else out->sortField = SORT_NAME;

    GetPrivateProfileStringW(L"Sorting", L"SortDirection", L"ascending", buf, ARRAYSIZE(buf), out->iniPath);
    trim_inplace(buf);
    out->sortDescending = (!lstrcmpiW(buf, L"descending") || !lstrcmpiW(buf, L"desc"));

    GetPrivateProfileStringW(L"Sorting", L"FoldersFirst", L"folders", buf, ARRAYSIZE(buf), out->iniPath);
    trim_inplace(buf);
    if (!lstrcmpiW(buf, L"folders") || !lstrcmpiW(buf, L"true") || !lstrcmpiW(buf, L"1"))
        out->sortObjectTypePriority = 1;
    else if (!lstrcmpiW(buf, L"files"))
        out->sortObjectTypePriority = 2;
    else
        out->sortObjectTypePriority = 0;

    out->maxItems = GetPrivateProfileIntW(L"General", L"MaxItems", 40, out->iniPath);

    GetPrivateProfileStringW(L"General", L"ShowHidden", L"false", buf, ARRAYSIZE(buf), out->iniPath);
    trim_inplace(buf);
    out->showHidden = (!lstrcmpiW(buf, L"true") || !lstrcmpiW(buf, L"1"));

    // TaskKill defaults
    out->taskKillMax = GetPrivateProfileIntW(L"TaskKill", L"TaskKillMax", 10, out->iniPath);

    GetPrivateProfileStringW(L"TaskKill", L"TaskKillIgnoreSystem", L"false", buf, ARRAYSIZE(buf), out->iniPath);
    trim_inplace(buf);
    out->taskKillIgnoreSystem = (!lstrcmpiW(buf, L"true") || !lstrcmpiW(buf, L"1"));

    GetPrivateProfileStringW(L"TaskKill", L"TaskKillShowIcons", L"true", buf, ARRAYSIZE(buf), out->iniPath);
    trim_inplace(buf);
    out->taskKillShowIcons = (!lstrcmpiW(buf, L"true") || !lstrcmpiW(buf, L"1"));

    GetPrivateProfileStringW(L"TaskKill", L"TaskKillListWindows", L"false", buf, ARRAYSIZE(buf), out->iniPath);
    trim_inplace(buf);
    out->taskKillListWindows = (!lstrcmpiW(buf, L"true") || !lstrcmpiW(buf, L"1"));

    GetPrivateProfileStringW(L"TaskKill", L"TaskKillAllDesktops", L"false", buf, ARRAYSIZE(buf), out->iniPath);
    trim_inplace(buf);
    out->taskKillAllDesktops = (!lstrcmpiW(buf, L"true") || !lstrcmpiW(buf, L"1"));

    GetPrivateProfileStringW(L"TaskKill", L"TaskKillExcludes", L"", out->taskKillExcludes, ARRAYSIZE(out->taskKillExcludes), out->iniPath);

    // Power option defaults: all included unless explicitly excluded
    out->excludeSleep = GetPrivateProfileIntW(L"Power", L"ExcludeSleep", 0, out->iniPath) ? TRUE : FALSE;
    out->excludeHibernate = GetPrivateProfileIntW(L"Power", L"ExcludeHibernate", 0, out->iniPath) ? TRUE : FALSE;
    out->excludeShutdown = GetPrivateProfileIntW(L"Power", L"ExcludeShutdown", 0, out->iniPath) ? TRUE : FALSE;
    out->excludeRestart = GetPrivateProfileIntW(L"Power", L"ExcludeRestart", 0, out->iniPath) ? TRUE : FALSE;
    out->excludeLock = GetPrivateProfileIntW(L"Power", L"ExcludeLock", 0, out->iniPath) ? TRUE : FALSE;
    out->excludeLogoff = GetPrivateProfileIntW(L"Power", L"ExcludeLogoff", 0, out->iniPath) ? TRUE : FALSE;

    GetPrivateProfileStringW(L"ThisPC", L"ThisPCItemsAsSubmenus", L"true", buf, ARRAYSIZE(buf), out->iniPath);
    trim_inplace(buf);
    out->thisPCItemsAsSubmenus = (!lstrcmpiW(buf, L"true") || !lstrcmpiW(buf, L"1"));

    GetPrivateProfileStringW(L"ThisPC", L"ThisPCShowIcons", L"true", buf, ARRAYSIZE(buf), out->iniPath);
    trim_inplace(buf);
    out->thisPCShowIcons = (!lstrcmpiW(buf, L"true") || !lstrcmpiW(buf, L"1"));

    GetPrivateProfileStringW(L"ThisPC", L"ThisPCAsSubmenu", L"false", buf, ARRAYSIZE(buf), out->iniPath);
    trim_inplace(buf);
    out->thisPCAsSubmenu = (!lstrcmpiW(buf, L"true") || !lstrcmpiW(buf, L"1"));

    GetPrivateProfileStringW(L"Home", L"HomeItemsAsSubmenus", L"true", buf, ARRAYSIZE(buf), out->iniPath);
    trim_inplace(buf);
    out->homeItemsAsSubmenus = (!lstrcmpiW(buf, L"true") || !lstrcmpiW(buf, L"1"));

    GetPrivateProfileStringW(L"Home", L"HomeShowIcons", L"true", buf, ARRAYSIZE(buf), out->iniPath);
    trim_inplace(buf);
    out->homeShowIcons = (!lstrcmpiW(buf, L"true") || !lstrcmpiW(buf, L"1"));

    GetPrivateProfileStringW(L"Home", L"HomeAsSubmenu", L"false", buf, ARRAYSIZE(buf), out->iniPath);
    trim_inplace(buf);
    out->homeAsSubmenu = (!lstrcmpiW(buf, L"true") || !lstrcmpiW(buf, L"1"));

    GetPrivateProfileStringW(L"General", L"ShowDotfiles", L"false", buf, ARRAYSIZE(buf), out->iniPath);
    trim_inplace(buf);
    // Accept both dashed and concatenated forms (files-only, folders-only)
    if (!lstrcmpiW(buf, L"true") || !lstrcmpiW(buf, L"1")) { out->dotMode = 3; out->showDotfiles = TRUE; }
    else if (!lstrcmpiW(buf, L"filesonly") || !lstrcmpiW(buf, L"files-only")) { out->dotMode = 1; out->showDotfiles = TRUE; }
    else if (!lstrcmpiW(buf, L"foldersonly") || !lstrcmpiW(buf, L"folders-only")) { out->dotMode = 2; out->showDotfiles = TRUE; }
    else { out->dotMode = 0; out->showDotfiles = FALSE; }
    // Default to legacy if missing
    GetPrivateProfileStringW(L"General", L"MenuStyle", L"legacy", buf, ARRAYSIZE(buf), out->iniPath);
    trim_inplace(buf);
#ifdef ENABLE_MODERN_STYLE
    if (!lstrcmpiW(buf, L"legacy")) out->menuStyle = 0; else out->menuStyle = 1; // only other value treated as modern
#else
    out->menuStyle = 0; // force legacy when modern disabled at build time
#endif
    GetPrivateProfileStringW(L"General", L"DefaultIcon", L"", out->defaultIconPath, ARRAYSIZE(out->defaultIconPath), out->iniPath);
    if (!out->defaultIconPath[0]) GetPrivateProfileStringW(L"Icons", L"DefaultIcon", L"", out->defaultIconPath, ARRAYSIZE(out->defaultIconPath), out->iniPath);
    GetPrivateProfileStringW(L"General", L"DefaultIconLight", L"", out->defaultIconPathLight, ARRAYSIZE(out->defaultIconPathLight), out->iniPath);
    if (!out->defaultIconPathLight[0]) GetPrivateProfileStringW(L"Icons", L"DefaultIconLight", L"", out->defaultIconPathLight, ARRAYSIZE(out->defaultIconPathLight), out->iniPath);
    GetPrivateProfileStringW(L"General", L"DefaultIconDark", L"", out->defaultIconPathDark, ARRAYSIZE(out->defaultIconPathDark), out->iniPath);
    if (!out->defaultIconPathDark[0]) GetPrivateProfileStringW(L"Icons", L"DefaultIconDark", L"", out->defaultIconPathDark, ARRAYSIZE(out->defaultIconPathDark), out->iniPath);
    // Expand any environment variables in default icon path
    if (out->defaultIconPath[0]) {
        WCHAR expanded[MAX_PATH];
        expand_env(out->defaultIconPath, expanded, ARRAYSIZE(expanded));
        lstrcpynW(out->defaultIconPath, expanded, ARRAYSIZE(out->defaultIconPath));
    }
    if (out->defaultIconPathLight[0]) {
        WCHAR expanded[MAX_PATH];
        expand_env(out->defaultIconPathLight, expanded, ARRAYSIZE(expanded));
        lstrcpynW(out->defaultIconPathLight, expanded, ARRAYSIZE(out->defaultIconPathLight));
    }
    if (out->defaultIconPathDark[0]) {
        WCHAR expanded[MAX_PATH];
        expand_env(out->defaultIconPathDark, expanded, ARRAYSIZE(expanded));
        lstrcpynW(out->defaultIconPathDark, expanded, ARRAYSIZE(out->defaultIconPathDark));
    }
    GetPrivateProfileStringW(L"General", L"ShowIcons", L"", buf, ARRAYSIZE(buf), out->iniPath);
    if (!buf[0]) GetPrivateProfileStringW(L"General", L"LegacyIcons", L"false", buf, ARRAYSIZE(buf), out->iniPath); // backward compatibility
    trim_inplace(buf);
    if (!lstrcmpiW(buf, L"true") || !lstrcmpiW(buf, L"1")) out->showIcons = 1;
    else if (!lstrcmpiW(buf, L"other")) out->showIcons = 2;
    else out->showIcons = 0;
    // Modern-only options (with [General] fallback) - ignored if modern disabled
#ifdef ENABLE_MODERN_STYLE
    GetPrivateProfileStringW(L"Modern", L"Corners", L"", buf, ARRAYSIZE(buf), out->iniPath);
    if (!buf[0]) GetPrivateProfileStringW(L"General", L"Corners", L"rounded", buf, ARRAYSIZE(buf), out->iniPath);
    trim_inplace(buf);
    out->roundedCorners = (lstrcmpiW(buf, L"square") != 0); // default rounded
#else
    out->roundedCorners = FALSE;
#endif
    GetPrivateProfileStringW(L"Placement", L"Horizontal", L"", buf, ARRAYSIZE(buf), out->iniPath);
    trim_inplace(buf);
    if (!buf[0] || !lstrcmpiW(buf, L"left")) out->hPlacement = 0; else if (!lstrcmpiW(buf, L"center")) out->hPlacement = 1; else out->hPlacement = 2;
    out->hOffset = GetPrivateProfileIntW(L"Placement", L"HOffset", 0, out->iniPath);
    GetPrivateProfileStringW(L"Placement", L"Vertical", L"", buf, ARRAYSIZE(buf), out->iniPath);
    trim_inplace(buf);
    if (!buf[0] || !lstrcmpiW(buf, L"top")) out->vPlacement = 0; else if (!lstrcmpiW(buf, L"center")) out->vPlacement = 1; else out->vPlacement = 2;
    out->vOffset = GetPrivateProfileIntW(L"Placement", L"VOffset", 0, out->iniPath);
    GetPrivateProfileStringW(L"Placement", L"PointerRelative", L"true", buf, ARRAYSIZE(buf), out->iniPath);
    trim_inplace(buf);
    out->pointerRelative = (!lstrcmpiW(buf, L"true") || !lstrcmpiW(buf, L"1"));
    GetPrivateProfileStringW(L"Appearance", L"LargeMenuAnimation", L"", buf, ARRAYSIZE(buf), out->iniPath);
    trim_inplace(buf);
    if (!buf[0]) {
        GetPrivateProfileStringW(L"Advanced", L"LargeMenuAnimation", L"", buf, ARRAYSIZE(buf), out->iniPath);
        trim_inplace(buf);
    }
    if (!buf[0]) {
        GetPrivateProfileStringW(L"Placement", L"RootMenuLargeAnimation", L"", buf, ARRAYSIZE(buf), out->iniPath);
        trim_inplace(buf);
    }
    if (!buf[0]) {
        GetPrivateProfileStringW(L"Placement", L"animation", L"", buf, ARRAYSIZE(buf), out->iniPath);
        trim_inplace(buf);
    }
    if (!buf[0]) {
        GetPrivateProfileStringW(L"Placement", L"AnimationDirection", L"auto", buf, ARRAYSIZE(buf), out->iniPath);
        trim_inplace(buf);
    }
    if (!buf[0] || !lstrcmpiW(buf, L"auto")) out->animationDirection = ANIM_BOTTOM;
    else if (!lstrcmpiW(buf, L"disabled") || !lstrcmpiW(buf, L"off") || !lstrcmpiW(buf, L"none")) out->animationDirection = ANIM_AUTO;
    else if (!lstrcmpiW(buf, L"top")) out->animationDirection = ANIM_TOP;
    else if (!lstrcmpiW(buf, L"bottom")) out->animationDirection = ANIM_BOTTOM;
    else if (!lstrcmpiW(buf, L"left")) out->animationDirection = ANIM_LEFT;
    else if (!lstrcmpiW(buf, L"right")) out->animationDirection = ANIM_RIGHT;
    else out->animationDirection = ANIM_BOTTOM;
    // Optional: IgnoreOffsetWhenCentered = false|true|hoffset|voffset (controls whether HOffset/VOffset are ignored when centered)
    out->ignoreHOffsetWhenCentered = FALSE;
    out->ignoreVOffsetWhenCentered = FALSE;
    GetPrivateProfileStringW(L"Placement", L"IgnoreOffsetWhenCentered", L"false", buf, ARRAYSIZE(buf), out->iniPath);
    trim_inplace(buf);
    if (!lstrcmpiW(buf, L"true") || !lstrcmpiW(buf, L"both")) {
        out->ignoreHOffsetWhenCentered = TRUE;
        out->ignoreVOffsetWhenCentered = TRUE;
    } else if (!lstrcmpiW(buf, L"hoffset") || !lstrcmpiW(buf, L"h") || !lstrcmpiW(buf, L"horizontal")) {
        out->ignoreHOffsetWhenCentered = TRUE;
    } else if (!lstrcmpiW(buf, L"voffset") || !lstrcmpiW(buf, L"v") || !lstrcmpiW(buf, L"vertical")) {
        out->ignoreVOffsetWhenCentered = TRUE;
    }
    // Optional: IgnoreOffsetWhenRelative = false|true|hoffset|voffset
    out->ignoreHOffsetWhenRelative = FALSE;
    out->ignoreVOffsetWhenRelative = FALSE;
    GetPrivateProfileStringW(L"Placement", L"IgnoreOffsetWhenRelative", L"false", buf, ARRAYSIZE(buf), out->iniPath);
    trim_inplace(buf);
    if (!lstrcmpiW(buf, L"true") || !lstrcmpiW(buf, L"both")) {
        out->ignoreHOffsetWhenRelative = TRUE;
        out->ignoreVOffsetWhenRelative = TRUE;
    } else if (!lstrcmpiW(buf, L"hoffset") || !lstrcmpiW(buf, L"h") || !lstrcmpiW(buf, L"horizontal")) {
        out->ignoreHOffsetWhenRelative = TRUE;
    } else if (!lstrcmpiW(buf, L"voffset") || !lstrcmpiW(buf, L"v") || !lstrcmpiW(buf, L"vertical")) {
        out->ignoreVOffsetWhenRelative = TRUE;
    }
    // Modern-only width override (ignored when disabled)
#ifdef ENABLE_MODERN_STYLE
    out->menuWidth = GetPrivateProfileIntW(L"General", L"MenuWidth", 0, out->iniPath);
#else
    out->menuWidth = 0;
#endif
    // Logging: LogConfig=off|false|0, basic|true|1, verbose|2. Fallback: [Debug] section if not in [General].
    out->logLevel = 0; out->logFolderPath[0] = 0; out->logFilePath[0] = 0;
    GetPrivateProfileStringW(L"General", L"LogConfig", L"", buf, ARRAYSIZE(buf), out->iniPath);
    trim_inplace(buf);
    if (!buf[0]) { // fallback to [Debug]
        GetPrivateProfileStringW(L"Debug", L"LogConfig", L"", buf, ARRAYSIZE(buf), out->iniPath);
        trim_inplace(buf);
    }
    if (buf[0]) {
        if (!lstrcmpiW(buf, L"verbose") || !lstrcmpiW(buf, L"2")) out->logLevel = 2;
        else if (!lstrcmpiW(buf, L"basic") || !lstrcmpiW(buf, L"true") || !lstrcmpiW(buf, L"1")) out->logLevel = 1;
        else out->logLevel = 0; // off / false / 0 / unknown
    }
    // New: LogFolder replaces LogFile. We create a dynamic log filename:
    // WinMacMenu_<configBase>_<yyMMdd-HHmm>.log inside specified folder (default = EXE directory when blank)
    WCHAR lf[512]; lf[0]=0;
    GetPrivateProfileStringW(L"General", L"LogFolder", L"", lf, ARRAYSIZE(lf), out->iniPath);
    trim_inplace(lf);
    if (!lf[0]) {
        GetPrivateProfileStringW(L"Debug", L"LogFolder", L"", lf, ARRAYSIZE(lf), out->iniPath);
        trim_inplace(lf);
    }
    if (lf[0]) {
        expand_env(lf, out->logFolderPath, ARRAYSIZE(out->logFolderPath));
    } else {
        // Default to executable directory
        GetModuleFileNameW(NULL, out->logFolderPath, ARRAYSIZE(out->logFolderPath));
        PathRemoveFileSpecW(out->logFolderPath);
    }
    // Ensure folder exists (best-effort)
    if (out->logFolderPath[0]) {
        SHCreateDirectoryExW(NULL, out->logFolderPath, NULL);
        // Derive config base name (file name without extension of iniPath)
        WCHAR base[MAX_PATH]; lstrcpynW(base, out->iniPath, ARRAYSIZE(base));
        WCHAR *slash = wcsrchr(base, L'\\'); WCHAR *name = slash ? slash+1 : base;
        WCHAR configBase[128]; lstrcpynW(configBase, name, ARRAYSIZE(configBase));
        WCHAR *dot = wcsrchr(configBase, L'.'); if (dot) *dot = 0;
        // Timestamp yyMMdd-HHmm
        SYSTEMTIME st; GetLocalTime(&st);
        WCHAR fname[256]; wsprintfW(fname, L"WinMacMenu_%s_%02d%02d%02d-%02d%02d.log", configBase, st.wYear%100, st.wMonth, st.wDay, st.wHour, st.wMinute);
        lstrcpynW(out->logFilePath, out->logFolderPath, ARRAYSIZE(out->logFilePath));
        PathAppendW(out->logFilePath, fname);
    }
    // RecentLabel (default fullpath). Accept synonyms: full, fullpath, path, name, filename, file, leaf
    GetPrivateProfileStringW(L"RecentItems", L"RecentLabel", L"fullpath", buf, ARRAYSIZE(buf), out->iniPath);
    trim_inplace(buf);
    if (!lstrcmpiW(buf, L"name") || !lstrcmpiW(buf, L"filename") || !lstrcmpiW(buf, L"file") || !lstrcmpiW(buf, L"leaf")) out->recentLabelMode = 1; else out->recentLabelMode = 0;
    // ShowFileExtensions (default true). Backward compatibility: ShowExtensions (old) and HideExtensions (legacy inverse).
    GetPrivateProfileStringW(L"General", L"ShowFileExtensions", L"", buf, ARRAYSIZE(buf), out->iniPath);
    trim_inplace(buf);
    BOOL haveShow = buf[0] != 0;
    BOOL showExt = TRUE; // default
    if (haveShow) {
        showExt = (!lstrcmpiW(buf, L"true") || !lstrcmpiW(buf, L"1"));
    } else {
        WCHAR bufOld[32]; bufOld[0]=0;
        GetPrivateProfileStringW(L"General", L"ShowExtensions", L"", bufOld, ARRAYSIZE(bufOld), out->iniPath);
        trim_inplace(bufOld);
        if (bufOld[0]) {
            showExt = (!lstrcmpiW(bufOld, L"true") || !lstrcmpiW(bufOld, L"1"));
            haveShow = TRUE;
        }
    }
    WCHAR tmpOld[32]; tmpOld[0]=0;
    GetPrivateProfileStringW(L"General", L"HideExtensions", L"", tmpOld, ARRAYSIZE(tmpOld), out->iniPath);
    trim_inplace(tmpOld);
    if (tmpOld[0]) {
        // Old semantics: HideExtensions=true means do NOT show extensions
        BOOL hideOld = (!lstrcmpiW(tmpOld, L"true") || !lstrcmpiW(tmpOld, L"1"));
        showExt = !hideOld; // override
    }
    out->showExtensions = showExt;
    // ShowFolderIcons
    GetPrivateProfileStringW(L"General", L"ShowFolderIcons", L"true", buf, ARRAYSIZE(buf), out->iniPath);
    trim_inplace(buf);
    out->showFolderIcons = (!lstrcmpiW(buf, L"true") || !lstrcmpiW(buf, L"1"));
    // ShowFileIcons (new). Back-compat: ShowSubmenuFileIcons.
    GetPrivateProfileStringW(L"General", L"ShowFileIcons", L"", buf, ARRAYSIZE(buf), out->iniPath);
    trim_inplace(buf);
    if (!buf[0]) {
        GetPrivateProfileStringW(L"General", L"ShowSubmenuFileIcons", L"true", buf, ARRAYSIZE(buf), out->iniPath);
        trim_inplace(buf);
    }
    out->showFileIcons = (!lstrcmpiW(buf, L"true") || !lstrcmpiW(buf, L"1"));
    GetPrivateProfileStringW(L"Appearance", L"LargeMenuIcons", L"", buf, ARRAYSIZE(buf), out->iniPath);
    trim_inplace(buf);
    if (!buf[0]) {
        GetPrivateProfileStringW(L"Advanced", L"LargeMenuIcons", L"", buf, ARRAYSIZE(buf), out->iniPath);
        trim_inplace(buf);
    }
    if (!buf[0]) {
        GetPrivateProfileStringW(L"General", L"RootMenuLargeIcons", L"", buf, ARRAYSIZE(buf), out->iniPath);
        trim_inplace(buf);
    }
    if (!buf[0]) {
        GetPrivateProfileStringW(L"General", L"RootMenuIconSize", L"small", buf, ARRAYSIZE(buf), out->iniPath);
        trim_inplace(buf);
    }
    if (!buf[0]) out->rootMenuLargeIcons = FALSE;
    else if (!lstrcmpiW(buf, L"large") || !lstrcmpiW(buf, L"l") || !lstrcmpiW(buf, L"32") || !lstrcmpiW(buf, L"true") || !lstrcmpiW(buf, L"1")) {
        out->rootMenuLargeIcons = TRUE;
    } else if (!lstrcmpiW(buf, L"small") || !lstrcmpiW(buf, L"s") || !lstrcmpiW(buf, L"16") || !lstrcmpiW(buf, L"false") || !lstrcmpiW(buf, L"0")) {
        out->rootMenuLargeIcons = FALSE;
    } else {
        out->rootMenuLargeIcons = FALSE;
    }
    GetPrivateProfileStringW(L"Appearance", L"UseWinUI3Menu", L"false", buf, ARRAYSIZE(buf), out->iniPath);
    trim_inplace(buf);
    out->useWinUI3Menu = (!lstrcmpiW(buf, L"true") || !lstrcmpiW(buf, L"1"));
    GetPrivateProfileStringW(L"Appearance", L"Renderer", L"", buf, ARRAYSIZE(buf), out->iniPath);
    trim_inplace(buf);
    if (buf[0]) {
        if (!lstrcmpiW(buf, L"winui") || !lstrcmpiW(buf, L"winui3")) {
            out->useWinUI3Menu = TRUE;
            out->rootMenuLargeIcons = FALSE;
            out->menuStyle = 0;
        } else if (!lstrcmpiW(buf, L"large") || !lstrcmpiW(buf, L"custom-drawn") || !lstrcmpiW(buf, L"other") || !lstrcmpiW(buf, L"bigmenu") || !lstrcmpiW(buf, L"big-menu")) {
            out->useWinUI3Menu = FALSE;
            out->rootMenuLargeIcons = TRUE;
            out->menuStyle = 1;
        } else {
            out->useWinUI3Menu = FALSE;
            out->rootMenuLargeIcons = FALSE;
            out->menuStyle = 0;
        }
    }
    GetPrivateProfileStringW(L"Appearance", L"WinUISize", L"", buf, ARRAYSIZE(buf), out->iniPath);
    trim_inplace(buf);
    if (!buf[0]) {
        GetPrivateProfileStringW(L"WinUI3", L"Size", L"compact", buf, ARRAYSIZE(buf), out->iniPath);
        trim_inplace(buf);
    }
    if (lstrcmpiW(buf, L"default") && lstrcmpiW(buf, L"compact")) {
        lstrcpynW(buf, L"compact", ARRAYSIZE(buf));
    }
    lstrcpynW(out->winuiSize, buf, ARRAYSIZE(out->winuiSize));
    GetPrivateProfileStringW(L"Appearance", L"KeepLargeMenuHighlightTextColor", L"", buf, ARRAYSIZE(buf), out->iniPath);
    trim_inplace(buf);
    if (!buf[0]) {
        GetPrivateProfileStringW(L"Advanced", L"KeepLargeMenuHighlightTextColor", L"", buf, ARRAYSIZE(buf), out->iniPath);
        trim_inplace(buf);
    }
    if (!buf[0]) {
        GetPrivateProfileStringW(L"Advanced", L"RevertBigMenuHighlightTextArrowColor", L"false", buf, ARRAYSIZE(buf), out->iniPath);
        trim_inplace(buf);
    }
    out->keepLargeMenuHighlightTextColor = (!lstrcmpiW(buf, L"true") || !lstrcmpiW(buf, L"1"));
    GetPrivateProfileStringW(L"Appearance", L"LargeMenuHighlightFrame", L"background", buf, ARRAYSIZE(buf), out->iniPath);
    trim_inplace(buf);
    if (!lstrcmpiW(buf, L"border")) out->largeMenuHighlightFrame = HIGHLIGHT_BORDER;
    else if (!lstrcmpiW(buf, L"background-border") || !lstrcmpiW(buf, L"background+border") || !lstrcmpiW(buf, L"both")) out->largeMenuHighlightFrame = HIGHLIGHT_BACKGROUND_BORDER;
    else out->largeMenuHighlightFrame = HIGHLIGHT_BACKGROUND;
    // KeepMenuOpenAfterContextAction (default false)
    GetPrivateProfileStringW(L"General", L"KeepMenuOpenAfterContextAction", L"false", buf, ARRAYSIZE(buf), out->iniPath);
    trim_inplace(buf);
    out->keepMenuOpenAfterContextAction = (!lstrcmpiW(buf, L"true") || !lstrcmpiW(buf, L"1"));
    // RecentShowExtensions (default true). Back compat: RecentHideExtensions overrides if present.
    GetPrivateProfileStringW(L"RecentItems", L"RecentShowExtensions", L"", buf, ARRAYSIZE(buf), out->iniPath);
    trim_inplace(buf);
    BOOL haveRecentShow = buf[0] != 0;
    BOOL recentShow = TRUE;
    if (haveRecentShow) {
        recentShow = (!lstrcmpiW(buf, L"true") || !lstrcmpiW(buf, L"1"));
    }
    WCHAR tmpOldR[32]; tmpOldR[0]=0;
    GetPrivateProfileStringW(L"RecentItems", L"RecentHideExtensions", L"", tmpOldR, ARRAYSIZE(tmpOldR), out->iniPath);
    trim_inplace(tmpOldR);
    if (tmpOldR[0]) {
        BOOL hideOldR = (!lstrcmpiW(tmpOldR, L"true") || !lstrcmpiW(tmpOldR, L"1"));
        recentShow = !hideOldR;
    }
    out->recentShowExtensions = recentShow;
    // RecentShowCleanItems flag (default true)
    GetPrivateProfileStringW(L"RecentItems", L"RecentShowCleanItems", L"true", buf, ARRAYSIZE(buf), out->iniPath);
    trim_inplace(buf);
    out->recentShowCleanItems = (!lstrcmpiW(buf, L"true") || !lstrcmpiW(buf, L"1"));
    // Themed tray icon optional paths
    out->trayIconPath[0]=0; out->trayIconPathLight[0]=0; out->trayIconPathDark[0]=0;
    GetPrivateProfileStringW(L"General", L"TrayIcon", L"", out->trayIconPath, ARRAYSIZE(out->trayIconPath), out->iniPath);
    GetPrivateProfileStringW(L"General", L"TrayIconLight", L"", out->trayIconPathLight, ARRAYSIZE(out->trayIconPathLight), out->iniPath);
    GetPrivateProfileStringW(L"General", L"TrayIconDark", L"", out->trayIconPathDark, ARRAYSIZE(out->trayIconPathDark), out->iniPath);
    if (out->trayIconPath[0]) { WCHAR ex[MAX_PATH]; expand_env(out->trayIconPath, ex, ARRAYSIZE(ex)); lstrcpynW(out->trayIconPath, ex, ARRAYSIZE(out->trayIconPath)); }
    if (out->trayIconPathLight[0]) { WCHAR ex[MAX_PATH]; expand_env(out->trayIconPathLight, ex, ARRAYSIZE(ex)); lstrcpynW(out->trayIconPathLight, ex, ARRAYSIZE(out->trayIconPathLight)); }
    if (out->trayIconPathDark[0]) { WCHAR ex[MAX_PATH]; expand_env(out->trayIconPathDark, ex, ARRAYSIZE(ex)); lstrcpynW(out->trayIconPathDark, ex, ARRAYSIZE(out->trayIconPathDark)); }
    // MonochromeTrayIcon (default false) - when true, use the monochrome theme icon paths; false falls back to app.ico
    GetPrivateProfileStringW(L"General", L"MonochromeTrayIcon", L"false", buf, ARRAYSIZE(buf), out->iniPath);
    trim_inplace(buf);
    out->monochromeTrayIcon = (!lstrcmpiW(buf, L"true") || !lstrcmpiW(buf, L"1"));
    
    // Parse Control section
    WCHAR controlBuf[256];
    GetPrivateProfileStringW(L"Control", L"LeftClick", L"WinMacMenu", controlBuf, ARRAYSIZE(controlBuf), out->iniPath);
    trim_inplace(controlBuf);
    out->leftClickAction = parse_control_action(controlBuf);
    
    GetPrivateProfileStringW(L"Control", L"LeftClickCommand", L"", out->leftClickCommand, ARRAYSIZE(out->leftClickCommand), out->iniPath);
    trim_inplace(out->leftClickCommand);
    if (out->leftClickCommand[0]) {
        WCHAR expanded[MAX_PATH];
        expand_env(out->leftClickCommand, expanded, ARRAYSIZE(expanded));
        lstrcpynW(out->leftClickCommand, expanded, ARRAYSIZE(out->leftClickCommand));
    }
    
    GetPrivateProfileStringW(L"Control", L"WindowsKey", L"WinMacMenu", controlBuf, ARRAYSIZE(controlBuf), out->iniPath);
    trim_inplace(controlBuf);
    out->windowsKeyAction = parse_control_action(controlBuf);
    
    GetPrivateProfileStringW(L"Control", L"WindowsKeyCommand", L"", out->windowsKeyCommand, ARRAYSIZE(out->windowsKeyCommand), out->iniPath);
    trim_inplace(out->windowsKeyCommand);
    if (out->windowsKeyCommand[0]) {
        WCHAR expanded[MAX_PATH];
        expand_env(out->windowsKeyCommand, expanded, ARRAYSIZE(expanded));
        lstrcpynW(out->windowsKeyCommand, expanded, ARRAYSIZE(out->windowsKeyCommand));
    }

    // Parse Controls section (Start button triggers; disabled by default)
    GetPrivateProfileStringW(L"Controls", L"WindowsKey", L"false", buf, ARRAYSIZE(buf), out->iniPath);
    trim_inplace(buf);
    if (!buf[0]) {
        GetPrivateProfileStringW(L"Control", L"WindowsKey", L"false", buf, ARRAYSIZE(buf), out->iniPath);
        trim_inplace(buf);
    }
    out->windowsKeyTrigger = (!lstrcmpiW(buf, L"true") || !lstrcmpiW(buf, L"1"));

    GetPrivateProfileStringW(L"Controls", L"ShiftWindowsKey", L"false", buf, ARRAYSIZE(buf), out->iniPath);
    trim_inplace(buf);
    if (!buf[0]) {
        GetPrivateProfileStringW(L"Control", L"ShiftWindowsKey", L"false", buf, ARRAYSIZE(buf), out->iniPath);
        trim_inplace(buf);
    }
    out->shiftWindowsKeyTrigger = (!lstrcmpiW(buf, L"true") || !lstrcmpiW(buf, L"1"));

    GetPrivateProfileStringW(L"Controls", L"WindowsKeyX", L"false", buf, ARRAYSIZE(buf), out->iniPath);
    trim_inplace(buf);
    out->windowsKeyXTrigger = (!lstrcmpiW(buf, L"true") || !lstrcmpiW(buf, L"1"));

    GetPrivateProfileStringW(L"Controls", L"LeftClick", L"", buf, ARRAYSIZE(buf), out->iniPath);
    trim_inplace(buf);
    if (!buf[0]) {
        GetPrivateProfileStringW(L"Control", L"LeftClick", L"", buf, ARRAYSIZE(buf), out->iniPath);
        trim_inplace(buf);
    }
    if (!buf[0]) {
        GetPrivateProfileStringW(L"Controls", L"StartLeftClick", L"false", buf, ARRAYSIZE(buf), out->iniPath);
        trim_inplace(buf);
    }
    out->leftClickTrigger = (!lstrcmpiW(buf, L"true") || !lstrcmpiW(buf, L"1"));

    GetPrivateProfileStringW(L"Controls", L"RightClick", L"", buf, ARRAYSIZE(buf), out->iniPath);
    trim_inplace(buf);
    if (!buf[0]) {
        GetPrivateProfileStringW(L"Control", L"RightClick", L"", buf, ARRAYSIZE(buf), out->iniPath);
        trim_inplace(buf);
    }
    if (!buf[0]) {
        GetPrivateProfileStringW(L"Controls", L"StartRightClick", L"false", buf, ARRAYSIZE(buf), out->iniPath);
        trim_inplace(buf);
    }
    out->rightClickTrigger = (!lstrcmpiW(buf, L"true") || !lstrcmpiW(buf, L"1"));

    GetPrivateProfileStringW(L"Controls", L"MiddleClick", L"true", buf, ARRAYSIZE(buf), out->iniPath);
    trim_inplace(buf);
    if (!buf[0]) {
        GetPrivateProfileStringW(L"Control", L"MiddleClick", L"true", buf, ARRAYSIZE(buf), out->iniPath);
        trim_inplace(buf);
    }
    out->middleClickTrigger = (!lstrcmpiW(buf, L"true") || !lstrcmpiW(buf, L"1"));

    GetPrivateProfileStringW(L"Controls", L"ShiftLeftClick", L"false", buf, ARRAYSIZE(buf), out->iniPath);
    trim_inplace(buf);
    if (!buf[0]) {
        GetPrivateProfileStringW(L"Control", L"ShiftLeftClick", L"false", buf, ARRAYSIZE(buf), out->iniPath);
        trim_inplace(buf);
    }
    out->shiftLeftClickTrigger = (!lstrcmpiW(buf, L"true") || !lstrcmpiW(buf, L"1"));

    GetPrivateProfileStringW(L"Controls", L"ShiftRightClick", L"false", buf, ARRAYSIZE(buf), out->iniPath);
    trim_inplace(buf);
    if (!buf[0]) {
        GetPrivateProfileStringW(L"Control", L"ShiftRightClick", L"false", buf, ARRAYSIZE(buf), out->iniPath);
        trim_inplace(buf);
    }
    out->shiftRightClickTrigger = (!lstrcmpiW(buf, L"true") || !lstrcmpiW(buf, L"1"));

    GetPrivateProfileStringW(L"Controls", L"ShiftMiddleClick", L"false", buf, ARRAYSIZE(buf), out->iniPath);
    trim_inplace(buf);
    if (!buf[0]) {
        GetPrivateProfileStringW(L"Control", L"ShiftMiddleClick", L"false", buf, ARRAYSIZE(buf), out->iniPath);
        trim_inplace(buf);
    }
    out->shiftMiddleClickTrigger = (!lstrcmpiW(buf, L"true") || !lstrcmpiW(buf, L"1"));
    
    // Fullscreen app detection
    GetPrivateProfileStringW(L"Controls", L"IgnoreTriggersWhenFullscreen", L"false", buf, ARRAYSIZE(buf), out->iniPath);
    trim_inplace(buf);
    out->ignoreTriggersWhenFullscreen = (!lstrcmpiW(buf, L"true") || !lstrcmpiW(buf, L"1"));
    
    GetPrivateProfileStringW(L"Controls", L"FullscreenExclusionList", L"", out->fullscreenExclusionList, ARRAYSIZE(out->fullscreenExclusionList), out->iniPath);
    trim_inplace(out->fullscreenExclusionList);
    
    parse_menu(out);
    parse_icons(out);
    // Power menu exclusions (default all FALSE). Support legacy Exclude* keys and new inclusion model.
    out->excludeSleep = GetPrivateProfileIntW(L"Power", L"ExcludeSleep", 0, out->iniPath) ? TRUE : FALSE;
    out->excludeShutdown = GetPrivateProfileIntW(L"Power", L"ExcludeShutdown", 0, out->iniPath) ? TRUE : FALSE;
    out->excludeRestart = GetPrivateProfileIntW(L"Power", L"ExcludeRestart", 0, out->iniPath) ? TRUE : FALSE;
    out->excludeLock = GetPrivateProfileIntW(L"Power", L"ExcludeLock", 0, out->iniPath) ? TRUE : FALSE;
    out->excludeLogoff = GetPrivateProfileIntW(L"Power", L"ExcludeLogoff", 0, out->iniPath) ? TRUE : FALSE;
    out->excludeHibernate = GetPrivateProfileIntW(L"Power", L"ExcludeHibernate", 0, out->iniPath) ? TRUE : FALSE;
    // New style: only write option when excluded as Name=0; if present with value 0, mark excluded.
    // If both legacy and new keys exist, new key overrides.
    if (GetPrivateProfileIntW(L"Power", L"Sleep", 1, out->iniPath) == 0) out->excludeSleep = TRUE;
    if (GetPrivateProfileIntW(L"Power", L"Hibernate", 1, out->iniPath) == 0) out->excludeHibernate = TRUE;
    if (GetPrivateProfileIntW(L"Power", L"Shutdown", 1, out->iniPath) == 0) out->excludeShutdown = TRUE;
    if (GetPrivateProfileIntW(L"Power", L"Restart", 1, out->iniPath) == 0) out->excludeRestart = TRUE;
    if (GetPrivateProfileIntW(L"Power", L"Lock", 1, out->iniPath) == 0) out->excludeLock = TRUE;
    if (GetPrivateProfileIntW(L"Power", L"Logoff", 1, out->iniPath) == 0) out->excludeLogoff = TRUE;
    if (out->logLevel > 0) {
        WCHAR msg[4096];
        wsprintfW(msg,
            L"[WinMacMenu Config]\n Level=%d Style=%s ShowIcons=%d MenuWidth=%d Rounded=%d\n Hidden=%d DotMode=%d (showDot=%d) RecentLabel=%s ShowExt=%d RecentShowExt=%d ShowFolderIcons=%d ShowFileIcons=%d KeepMenuOpenAfterContextAction=%d\n FolderDepth=%d SingleClickOpen=%d ShowOpenEntry=%d RecentShowCleanItems=%d SeparateItems=%d\n RecentMax=%d Items=%d PointerRel=%d HPlacement=%d VPlacement=%d HOffset=%d VOffset=%d\n ThisPCSubmenus=%d ThisPCAsSubmenu=%d HomeAsSubmenu=%d TaskKillAllDesktops=%d\n IniPath=%s\n LogFolder=%s\n LogFile=%s\n",
            out->logLevel,
            out->menuStyle==0?L"legacy":L"modern",
            out->showIcons,
#ifdef ENABLE_MODERN_STYLE
            out->menuWidth,
            out->roundedCorners,
#else
            0,
            0,
#endif
            out->showHidden,
            out->dotMode,
            out->showDotfiles,
            out->recentLabelMode==1?L"name":L"fullpath",
            out->showExtensions,
            out->recentShowExtensions,
            out->showFolderIcons,
            out->showFileIcons,
            out->keepMenuOpenAfterContextAction,
            out->folderMaxDepth,
            out->folderSingleClickOpen,
            out->folderShowOpenEntry,
            out->recentShowCleanItems,
            out->separateItems,
            out->recentMax,
            out->count,
            out->pointerRelative,
            out->hPlacement,
            out->vPlacement,
            out->hOffset,
            out->vOffset,
            out->thisPCItemsAsSubmenus,
            out->thisPCAsSubmenu,
            out->homeAsSubmenu,
            out->taskKillAllDesktops,
            out->iniPath,
            out->logFolderPath,
            out->logFilePath);
        OutputDebugStringW(msg);
        if (out->logFilePath[0]) {
            HANDLE hf = CreateFileW(out->logFilePath, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
            if (hf != INVALID_HANDLE_VALUE) {
                SetFilePointer(hf, 0, NULL, FILE_END);
                int len = lstrlenW(msg);
                // Convert to UTF-8
                int needed = WideCharToMultiByte(CP_UTF8, 0, msg, len, NULL, 0, NULL, NULL);
                if (needed > 0) {
                    char* buf8 = (char*)LocalAlloc(LMEM_FIXED, needed+2);
                    if (buf8) {
                        WideCharToMultiByte(CP_UTF8, 0, msg, len, buf8, needed, NULL, NULL);
                        buf8[needed] = '\n'; buf8[needed+1] = 0;
                        DWORD written; WriteFile(hf, buf8, (DWORD)(needed+1), &written, NULL);
                        LocalFree(buf8);
                    }
                }
                // Verbose item dump
                if (out->logLevel > 1 && out->count > 0) {
                    for (int i=0;i<out->count;i++) {
                        WCHAR line[1024];
                        wsprintfW(line, L"Item%02d Type=%d Label='%s' Path='%s' Icon='%s' Params='%s'\n", i+1, out->items[i].type, out->items[i].label, out->items[i].path, out->items[i].iconPath, out->items[i].params);
                        int llen = lstrlenW(line);
                        int need2 = WideCharToMultiByte(CP_UTF8, 0, line, llen, NULL, 0, NULL, NULL);
                        if (need2 > 0) {
                            char* b2 = (char*)LocalAlloc(LMEM_FIXED, need2+1);
                            if (b2) {
                                WideCharToMultiByte(CP_UTF8, 0, line, llen, b2, need2, NULL, NULL);
                                DWORD wr; WriteFile(hf, b2, (DWORD)need2, &wr, NULL);
                                LocalFree(b2);
                            }
                        }
                    }
                }
                CloseHandle(hf);
            }
        }
    }
    return TRUE;
}

void config_set_path(Config* out, const WCHAR* path) {
    if (!out || !path) return;
    lstrcpynW(out->iniPath, path, ARRAYSIZE(out->iniPath));
    if (!PathFileExistsW(out->iniPath)) {
        write_default_ini(out->iniPath);
    }
}

// Synchronizes StartOnLogin with actual registry state
// This ensures the config.ini file reflects whether the startup entry actually exists in the registry
void config_sync_startup(Config* out, const WCHAR* runValName) {
    if (!out || !runValName) return;
    
    // Check if the registry entry actually exists
    BOOL inRegistry = check_run_at_login(runValName);
    BOOL inConfig = out->startOnLogin;
    
    // If they're out of sync, update the config and INI file to match registry state
    if (inRegistry != inConfig) {
        out->startOnLogin = inRegistry;
        WritePrivateProfileStringW(L"General", L"StartOnLogin", inRegistry ? L"true" : L"false", out->iniPath);
    }
}

void config_set_default_path(const WCHAR* path) {
    if (!path) { g_defaultIniPath[0] = 0; return; }
    lstrcpynW(g_defaultIniPath, path, ARRAYSIZE(g_defaultIniPath));
}
