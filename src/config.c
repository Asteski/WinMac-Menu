#include "config.h"
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
        WCHAR c = s[len-1];
        if (c == L' ' || c == L'\t' || c == L'\r' || c == L'\n') { s[len-1] = 0; --len; }
        else break;
    }
}

static void write_default_ini(const WCHAR* path) {
    // Create parent folder
    WCHAR parent[MAX_PATH]; lstrcpynW(parent, path, MAX_PATH); PathRemoveFileSpecW(parent); SHCreateDirectoryExW(NULL, parent, NULL);
    // Populate defaults using profile API (handles formatting)
    WritePrivateProfileStringW(L"General", L"RecentMax", L"12", path);
    WritePrivateProfileStringW(L"Placement", L"Horizontal", L"right", path);
    WritePrivateProfileStringW(L"Placement", L"HOffset", L"16", path);
    WritePrivateProfileStringW(L"Placement", L"Vertical", L"bottom", path);
    WritePrivateProfileStringW(L"Placement", L"VOffset", L"48", path);
    WritePrivateProfileStringW(L"General", L"FolderSubmenuDepth", L"1", path);
    // Default to single-click open for folder submenus (changed from double)
    WritePrivateProfileStringW(L"General", L"FolderSubmenuOpen", L"single", path);
    // Default to legacy style (changed from modern)
    WritePrivateProfileStringW(L"General", L"MenuStyle", L"legacy", path);
    WritePrivateProfileStringW(L"General", L"DefaultIcon", L"", path);
    WritePrivateProfileStringW(L"General", L"ShowIcons", L"false", path); // renamed from LegacyIcons
    WritePrivateProfileStringW(L"General", L"ShowExtensions", L"true", path); // new (inverse of deprecated HideExtensions)
    WritePrivateProfileStringW(L"General", L"ShowFolderIcons", L"false", path);
    WritePrivateProfileStringW(L"General", L"RecentShowExtensions", L"true", path); // inverse of deprecated RecentHideExtensions
    WritePrivateProfileStringW(L"General", L"RecentShowCleanItems", L"true", path); // default now enabled
    WritePrivateProfileStringW(L"Modern", L"Corners", L"rounded", path);
    // Sorting options omitted (reverted)
    WritePrivateProfileStringW(L"General", L"ShowHidden", L"false", path);
    WritePrivateProfileStringW(L"General", L"ShowDotfiles", L"false", path);
    WritePrivateProfileStringW(L"Placement", L"PointerRelative", L"false", path);
    // Menu
    WritePrivateProfileStringW(L"Menu", L"Item1",  L"Apps and Features|URI|ms-settings:appsfeatures", path);
    WritePrivateProfileStringW(L"Menu", L"Item2",  L"About Windows|FILE|winver.exe", path);
    WritePrivateProfileStringW(L"Menu", L"Item3",  L"Settings|URI|ms-settings:", path);
    WritePrivateProfileStringW(L"Menu", L"Item4",  L"File Explorer|FILE|explorer.exe", path);
    WritePrivateProfileStringW(L"Menu", L"Item5",  L"---|SEPARATOR|", path);
    WritePrivateProfileStringW(L"Menu", L"Item6",  L"Sleep|POWER_SLEEP|", path);
    WritePrivateProfileStringW(L"Menu", L"Item7",  L"Shut down|POWER_SHUTDOWN|", path);
    WritePrivateProfileStringW(L"Menu", L"Item8",  L"Restart|POWER_RESTART|", path);
    WritePrivateProfileStringW(L"Menu", L"Item9",  L"Lock|POWER_LOCK|", path);
    WritePrivateProfileStringW(L"Menu", L"Item10",  L"Log off|POWER_LOGOFF|", path);
    WritePrivateProfileStringW(L"Menu", L"Item11",  L"---|SEPARATOR|", path);
    WritePrivateProfileStringW(L"Menu", L"Item12", L"Event Viewer|FILE|eventvwr.msc", path);
    WritePrivateProfileStringW(L"Menu", L"Item13", L"Task Scheduler|FILE|taskschd.msc", path);
    WritePrivateProfileStringW(L"Menu", L"Item14", L"Recent Items|RECENT_SUBMENU|", path);
    WritePrivateProfileStringW(L"Menu", L"Item15", L"Task Manager|FILE|taskmgr.exe", path);
    WritePrivateProfileStringW(L"Menu", L"Item16", L"Properties|URI|ms-settings:about", path);
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
    if (!lstrcmpiW(s, L"RECENT_SUBMENU")) return CI_RECENT_SUBMENU;
    if (!lstrcmpiW(s, L"POWER_MENU")) return CI_POWER_MENU;
    return CI_SEPARATOR;
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
        it->submenu = (it->type == CI_FOLDER_SUBMENU || it->type == CI_RECENT_SUBMENU);
        // Allow FOLDER items to set mode via 4th field: "submenu" or "link"
        if (it->type == CI_FOLDER && it->params[0]) {
            WCHAR pLower[256]; lstrcpynW(pLower, it->params, ARRAYSIZE(pLower));
            for (WCHAR* q=pLower; *q; ++q) *q = (WCHAR)towlower(*q);
            if (wcsstr(pLower, L"submenu")) it->submenu = TRUE;
            else if (wcsstr(pLower, L"link")) it->submenu = FALSE;
            // Experimental inline expansion: include token "inline" to inject folder contents at root
            if (wcsstr(pLower, L"inline")) it->inlineExpand = TRUE; else it->inlineExpand = FALSE;
            if (it->inlineExpand && (wcsstr(pLower, L"notitle") || wcsstr(pLower, L"noheader"))) it->inlineNoHeader = TRUE; else it->inlineNoHeader = FALSE;
            if (it->inlineExpand && wcsstr(pLower, L"inlineopen")) it->inlineOpen = TRUE; else it->inlineOpen = FALSE;
        } else if (it->type == CI_FOLDER) {
            it->inlineExpand = FALSE;
            it->inlineNoHeader = FALSE;
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
            lstrcpynW(cfg->items[i-1].iconPath, path, ARRAYSIZE(cfg->items[i-1].iconPath));
        }
    }
}

BOOL config_load(Config* out) {
    if (!out) return FALSE;
    config_ensure(out);
    // Explicit defaults for control flags so missing keys don't inherit prior memory/state
    // Removed resident/trigger controls
    out->recentMax = GetPrivateProfileIntW(L"General", L"RecentMax", 12, out->iniPath);
    out->folderMaxDepth = GetPrivateProfileIntW(L"General", L"FolderSubmenuDepth", 1, out->iniPath);
    if (out->folderMaxDepth < 1) out->folderMaxDepth = 1; if (out->folderMaxDepth > 4) out->folderMaxDepth = 4;
    WCHAR buf[32];
    // Fallback default now single
    GetPrivateProfileStringW(L"General", L"FolderSubmenuOpen", L"single", buf, ARRAYSIZE(buf), out->iniPath);
    trim_inplace(buf);
    out->folderSingleClickOpen = (!lstrcmpiW(buf, L"single")) ? TRUE : FALSE;
    // Global toggle for showing "Open <folder>" entry in submenus (default true)
    GetPrivateProfileStringW(L"General", L"FolderShowOpenEntry", L"true", buf, ARRAYSIZE(buf), out->iniPath);
    trim_inplace(buf);
    out->folderShowOpenEntry = (!lstrcmpiW(buf, L"true") || !lstrcmpiW(buf, L"1"));
    // Sorting options omitted (reverted)
    GetPrivateProfileStringW(L"General", L"ShowHidden", L"false", buf, ARRAYSIZE(buf), out->iniPath);
    trim_inplace(buf);
    out->showHidden = (!lstrcmpiW(buf, L"true") || !lstrcmpiW(buf, L"1"));
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
    // Expand any environment variables in default icon path
    if (out->defaultIconPath[0]) {
        WCHAR expanded[MAX_PATH];
        expand_env(out->defaultIconPath, expanded, ARRAYSIZE(expanded));
        lstrcpynW(out->defaultIconPath, expanded, ARRAYSIZE(out->defaultIconPath));
    }
    GetPrivateProfileStringW(L"General", L"ShowIcons", L"", buf, ARRAYSIZE(buf), out->iniPath);
    if (!buf[0]) GetPrivateProfileStringW(L"General", L"LegacyIcons", L"false", buf, ARRAYSIZE(buf), out->iniPath); // backward compatibility
    trim_inplace(buf);
    out->showIcons = (!lstrcmpiW(buf, L"true") || !lstrcmpiW(buf, L"1"));
    // Modern-only options (with [General] fallback) - ignored if modern disabled
#ifdef ENABLE_MODERN_STYLE
    GetPrivateProfileStringW(L"Modern", L"Corners", L"", buf, ARRAYSIZE(buf), out->iniPath);
    if (!buf[0]) GetPrivateProfileStringW(L"General", L"Corners", L"rounded", buf, ARRAYSIZE(buf), out->iniPath);
    trim_inplace(buf);
    out->roundedCorners = (lstrcmpiW(buf, L"square") != 0); // default rounded
#else
    out->roundedCorners = FALSE;
#endif
    GetPrivateProfileStringW(L"Placement", L"Horizontal", L"right", buf, ARRAYSIZE(buf), out->iniPath);
    trim_inplace(buf);
    if (!lstrcmpiW(buf, L"left")) out->hPlacement = 0; else if (!lstrcmpiW(buf, L"center")) out->hPlacement = 1; else out->hPlacement = 2;
    out->hOffset = GetPrivateProfileIntW(L"Placement", L"HOffset", 16, out->iniPath);
    GetPrivateProfileStringW(L"Placement", L"Vertical", L"bottom", buf, ARRAYSIZE(buf), out->iniPath);
    trim_inplace(buf);
    if (!lstrcmpiW(buf, L"top")) out->vPlacement = 0; else if (!lstrcmpiW(buf, L"center")) out->vPlacement = 1; else out->vPlacement = 2;
    out->vOffset = GetPrivateProfileIntW(L"Placement", L"VOffset", 48, out->iniPath);
    GetPrivateProfileStringW(L"Placement", L"PointerRelative", L"false", buf, ARRAYSIZE(buf), out->iniPath);
    trim_inplace(buf);
    out->pointerRelative = (!lstrcmpiW(buf, L"true") || !lstrcmpiW(buf, L"1"));
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
    GetPrivateProfileStringW(L"General", L"RecentLabel", L"fullpath", buf, ARRAYSIZE(buf), out->iniPath);
    trim_inplace(buf);
    if (!lstrcmpiW(buf, L"name") || !lstrcmpiW(buf, L"filename") || !lstrcmpiW(buf, L"file") || !lstrcmpiW(buf, L"leaf")) out->recentLabelMode = 1; else out->recentLabelMode = 0;
    // ShowExtensions (default true). Backward compatibility: if HideExtensions present, it overrides ShowExtensions logic.
    GetPrivateProfileStringW(L"General", L"ShowExtensions", L"", buf, ARRAYSIZE(buf), out->iniPath);
    trim_inplace(buf);
    BOOL haveShow = buf[0] != 0;
    BOOL showExt = TRUE; // default
    if (haveShow) {
        showExt = (!lstrcmpiW(buf, L"true") || !lstrcmpiW(buf, L"1"));
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
    GetPrivateProfileStringW(L"General", L"ShowFolderIcons", L"false", buf, ARRAYSIZE(buf), out->iniPath);
    trim_inplace(buf);
    out->showFolderIcons = (!lstrcmpiW(buf, L"true") || !lstrcmpiW(buf, L"1"));
    // RecentShowExtensions (default true). Back compat: RecentHideExtensions overrides if present.
    GetPrivateProfileStringW(L"General", L"RecentShowExtensions", L"", buf, ARRAYSIZE(buf), out->iniPath);
    trim_inplace(buf);
    BOOL haveRecentShow = buf[0] != 0;
    BOOL recentShow = TRUE;
    if (haveRecentShow) {
        recentShow = (!lstrcmpiW(buf, L"true") || !lstrcmpiW(buf, L"1"));
    }
    WCHAR tmpOldR[32]; tmpOldR[0]=0;
    GetPrivateProfileStringW(L"General", L"RecentHideExtensions", L"", tmpOldR, ARRAYSIZE(tmpOldR), out->iniPath);
    trim_inplace(tmpOldR);
    if (tmpOldR[0]) {
        BOOL hideOldR = (!lstrcmpiW(tmpOldR, L"true") || !lstrcmpiW(tmpOldR, L"1"));
        recentShow = !hideOldR;
    }
    out->recentShowExtensions = recentShow;
    // RecentShowCleanItems flag (default true)
    GetPrivateProfileStringW(L"General", L"RecentShowCleanItems", L"true", buf, ARRAYSIZE(buf), out->iniPath);
    trim_inplace(buf);
    out->recentShowCleanItems = (!lstrcmpiW(buf, L"true") || !lstrcmpiW(buf, L"1"));
    parse_menu(out);
    parse_icons(out);
    if (out->logLevel > 0) {
        WCHAR msg[4096];
        wsprintfW(msg,
            L"[WinMacMenu Config]\n Level=%d Style=%s ShowIcons=%d MenuWidth=%d Rounded=%d\n Hidden=%d DotMode=%d (showDot=%d) RecentLabel=%s ShowExt=%d RecentShowExt=%d ShowFolderIcons=%d\n FolderDepth=%d SingleClickOpen=%d ShowOpenEntry=%d RecentShowCleanItems=%d\n RecentMax=%d Items=%d PointerRel=%d HPlacement=%d VPlacement=%d HOffset=%d VOffset=%d\n IniPath=%s\n LogFolder=%s\n LogFile=%s\n",
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
            out->folderMaxDepth,
            out->folderSingleClickOpen,
            out->folderShowOpenEntry,
            out->recentShowCleanItems,
            out->recentMax,
            out->count,
            out->pointerRelative,
            out->hPlacement,
            out->vPlacement,
            out->hOffset,
            out->vOffset,
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

void config_set_default_path(const WCHAR* path) {
    if (!path) { g_defaultIniPath[0] = 0; return; }
    lstrcpynW(g_defaultIniPath, path, ARRAYSIZE(g_defaultIniPath));
}
