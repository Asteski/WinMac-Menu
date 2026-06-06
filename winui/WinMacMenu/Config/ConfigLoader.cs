using WinMacMenu.Models;

namespace WinMacMenu.Configuration;

/// <summary>
/// Loads <see cref="Models.Config"/> from an INI file, faithfully reproducing the parsing,
/// defaults and back-compat fallbacks of the Win32 app's config.c.
/// </summary>
public static class ConfigLoader
{
    /// <summary>Resolves the INI path next to the executable (config.ini) unless overridden.</summary>
    public static string ResolveDefaultPath()
        => System.IO.Path.Combine(AppContext.BaseDirectory, "config.ini");

    public static Config Load(string? overridePath = null)
    {
        var cfg = new Config { IniPath = overridePath ?? ResolveDefaultPath() };

        if (!File.Exists(cfg.IniPath))
            WriteDefaultIni(cfg.IniPath);

        var ini = IniFile.Load(cfg.IniPath);

        // ---- [General] ----
        cfg.RunInBackground = ini.GetBool("General", "RunInBackground", true);
        cfg.ShowOnLaunch = ini.GetBool("General", "ShowOnLaunch", true);
        cfg.ShowTrayIcon = ini.GetBool("General", "ShowTrayIcon", true);
        cfg.StartOnLogin = ini.GetBool("General", "StartOnLogin", false);

        cfg.RecentMax = ini.GetInt("RecentItems", "RecentMax", 12);
        cfg.FolderMaxDepth = Math.Clamp(ini.GetInt("General", "FolderSubmenuDepth", 4), 1, 4);
        cfg.FolderSingleClickOpen =
            ini.GetString("General", "FolderSubmenuOpen", "single").Equals("single", StringComparison.OrdinalIgnoreCase);
        cfg.FolderShowOpenEntry = ini.GetBool("General", "FolderShowOpenEntry", true);
        cfg.RecentShowIcons = ini.GetBool("RecentItems", "RecentShowIcons", false);

        // ---- [Sorting] ----
        cfg.SortField = ini.GetString("Sorting", "SortBy", "name").ToLowerInvariant() switch
        {
            "date_modified" or "datemodified" or "modified" => SortField.DateModified,
            "date_created" or "datecreated" or "created" => SortField.DateCreated,
            "type" => SortField.Type,
            "size" => SortField.Size,
            _ => SortField.Name,
        };
        var dir = ini.GetString("Sorting", "SortDirection", "ascending");
        cfg.SortDescending = dir.Equals("descending", StringComparison.OrdinalIgnoreCase)
                             || dir.Equals("desc", StringComparison.OrdinalIgnoreCase);
        cfg.SortObjectPriority = ini.GetString("Sorting", "FoldersFirst", "folders").ToLowerInvariant() switch
        {
            "folders" or "true" or "1" => SortObjectPriority.FoldersFirst,
            "files" => SortObjectPriority.FilesFirst,
            _ => SortObjectPriority.Disabled,
        };

        cfg.MaxItems = ini.GetInt("General", "MaxItems", 40);
        cfg.ShowHidden = ini.GetBool("General", "ShowHidden", false);

        // ---- [TaskKill] ----
        cfg.TaskKillMax = ini.GetInt("TaskKill", "TaskKillMax", 10);
        cfg.TaskKillIgnoreSystem = ini.GetBool("TaskKill", "TaskKillIgnoreSystem", false);
        cfg.TaskKillShowIcons = ini.GetBool("TaskKill", "TaskKillShowIcons", true);
        cfg.TaskKillListWindows = ini.GetBool("TaskKill", "TaskKillListWindows", false);
        cfg.TaskKillAllDesktops = ini.GetBool("TaskKill", "TaskKillAllDesktops", false);
        cfg.TaskKillExcludes = ini.GetString("TaskKill", "TaskKillExcludes", "");

        // ---- [ThisPC] / [Home] ----
        cfg.ThisPCItemsAsSubmenus = ini.GetBool("ThisPC", "ThisPCItemsAsSubmenus", true);
        cfg.ThisPCShowIcons = ini.GetBool("ThisPC", "ThisPCShowIcons", true);
        cfg.ThisPCAsSubmenu = ini.GetBool("ThisPC", "ThisPCAsSubmenu", false);
        cfg.HomeItemsAsSubmenus = ini.GetBool("Home", "HomeItemsAsSubmenus", true);
        cfg.HomeShowIcons = ini.GetBool("Home", "HomeShowIcons", true);
        cfg.HomeAsSubmenu = ini.GetBool("Home", "HomeAsSubmenu", false);

        // ---- Dotfiles ----
        var dot = ini.GetString("General", "ShowDotfiles", "false").ToLowerInvariant();
        (cfg.DotMode, cfg.ShowDotfiles) = dot switch
        {
            "true" or "1" => (3, true),
            "filesonly" or "files-only" => (1, true),
            "foldersonly" or "folders-only" => (2, true),
            _ => (0, false),
        };

        // ---- Icons ----
        cfg.DefaultIconPath = Env.Expand(FirstNonEmpty(
            ini.GetString("General", "DefaultIcon"), ini.GetString("Icons", "DefaultIcon")));
        cfg.DefaultIconPathLight = Env.Expand(FirstNonEmpty(
            ini.GetString("General", "DefaultIconLight"), ini.GetString("Icons", "DefaultIconLight")));
        cfg.DefaultIconPathDark = Env.Expand(FirstNonEmpty(
            ini.GetString("General", "DefaultIconDark"), ini.GetString("Icons", "DefaultIconDark")));

        var showIcons = ini.GetString("General", "ShowIcons");
        if (string.IsNullOrEmpty(showIcons))
            showIcons = ini.GetString("General", "LegacyIcons", "false");
        cfg.ShowIcons = showIcons.Equals("true", StringComparison.OrdinalIgnoreCase) || showIcons == "1"
            ? 1
            : showIcons.Equals("other", StringComparison.OrdinalIgnoreCase) ? 2 : 0;

        // ---- [Placement] ----
        cfg.HPlacement = ini.GetString("Placement", "Horizontal", "").ToLowerInvariant() switch
        {
            "center" => HorizontalPlacement.Center,
            "right" => HorizontalPlacement.Right,
            _ => HorizontalPlacement.Left,
        };
        cfg.HOffset = ini.GetInt("Placement", "HOffset", 0);
        cfg.VPlacement = ini.GetString("Placement", "Vertical", "").ToLowerInvariant() switch
        {
            "center" => VerticalPlacement.Center,
            "bottom" => VerticalPlacement.Bottom,
            _ => VerticalPlacement.Top,
        };
        cfg.VOffset = ini.GetInt("Placement", "VOffset", 0);
        cfg.PointerRelative = ini.GetBool("Placement", "PointerRelative", true);

        ParseIgnoreOffset(ini.GetString("Placement", "IgnoreOffsetWhenCentered", "false"),
            out var ihc, out var ivc);
        cfg.IgnoreHOffsetWhenCentered = ihc;
        cfg.IgnoreVOffsetWhenCentered = ivc;
        ParseIgnoreOffset(ini.GetString("Placement", "IgnoreOffsetWhenRelative", "false"),
            out var ihr, out var ivr);
        cfg.IgnoreHOffsetWhenRelative = ihr;
        cfg.IgnoreVOffsetWhenRelative = ivr;

        // ---- [Appearance] animation (auto -> bottom; disabled/off/none -> Auto/no-anim) ----
        var anim = FirstNonEmpty(
            ini.GetString("Appearance", "LargeMenuAnimation"),
            ini.GetString("Advanced", "LargeMenuAnimation"),
            ini.GetString("Placement", "RootMenuLargeAnimation"),
            ini.GetString("Placement", "animation"),
            ini.GetString("Placement", "AnimationDirection", "auto")).ToLowerInvariant();
        cfg.AnimationDirection = anim switch
        {
            "" or "auto" => AnimationDirection.Bottom,
            "disabled" or "off" or "none" => AnimationDirection.Auto,
            "top" => AnimationDirection.Top,
            "bottom" => AnimationDirection.Bottom,
            "left" => AnimationDirection.Left,
            "right" => AnimationDirection.Right,
            _ => AnimationDirection.Bottom,
        };

        // ---- [RecentItems] label + extensions ----
        cfg.RecentLabelMode = ini.GetString("RecentItems", "RecentLabel", "fullpath").ToLowerInvariant() switch
        {
            "name" or "filename" or "file" or "leaf" => 1,
            _ => 0,
        };

        cfg.ShowExtensions = ResolveShowExtensions(
            ini, "General", "ShowFileExtensions", "ShowExtensions", "HideExtensions");
        cfg.ShowFolderIcons = ini.GetBool("General", "ShowFolderIcons", true);

        var showFileIcons = ini.GetString("General", "ShowFileIcons");
        if (string.IsNullOrEmpty(showFileIcons))
            showFileIcons = ini.GetString("General", "ShowSubmenuFileIcons", "true");
        cfg.ShowFileIcons = showFileIcons.Equals("true", StringComparison.OrdinalIgnoreCase) || showFileIcons == "1";

        cfg.KeepMenuOpenAfterContextAction = ini.GetBool("General", "KeepMenuOpenAfterContextAction", false);

        cfg.RecentShowExtensions = ResolveShowExtensions(
            ini, "RecentItems", "RecentShowExtensions", null, "RecentHideExtensions");
        cfg.RecentShowCleanItems = ini.GetBool("RecentItems", "RecentShowCleanItems", true);

        // ---- Tray icon ----
        cfg.TrayIconPath = Env.Expand(ini.GetString("General", "TrayIcon"));
        cfg.TrayIconPathLight = Env.Expand(ini.GetString("General", "TrayIconLight"));
        cfg.TrayIconPathDark = Env.Expand(ini.GetString("General", "TrayIconDark"));
        cfg.MonochromeTrayIcon = ini.GetBool("General", "MonochromeTrayIcon", true);

        // ---- [Control] actions ----
        cfg.LeftClickAction = ParseControlAction(ini.GetString("Control", "LeftClick", "WinMacMenu"));
        cfg.LeftClickCommand = Env.Expand(ini.GetString("Control", "LeftClickCommand"));
        cfg.WindowsKeyAction = ParseControlAction(ini.GetString("Control", "WindowsKey", "WinMacMenu"));
        cfg.WindowsKeyCommand = Env.Expand(ini.GetString("Control", "WindowsKeyCommand"));

        // ---- [Controls] triggers (with [Control] fallback) ----
        cfg.WindowsKeyTrigger = TriggerBool(ini, "WindowsKey", false);
        cfg.ShiftWindowsKeyTrigger = TriggerBool(ini, "ShiftWindowsKey", false);
        cfg.LeftClickTrigger = TriggerBool(ini, "LeftClick", false, "StartLeftClick");
        cfg.RightClickTrigger = TriggerBool(ini, "RightClick", false, "StartRightClick");
        cfg.MiddleClickTrigger = TriggerBool(ini, "MiddleClick", true);
        cfg.ShiftLeftClickTrigger = TriggerBool(ini, "ShiftLeftClick", false);
        cfg.ShiftRightClickTrigger = TriggerBool(ini, "ShiftRightClick", false);
        cfg.ShiftMiddleClickTrigger = TriggerBool(ini, "ShiftMiddleClick", false);
        cfg.IgnoreTriggersWhenFullscreen = ini.GetBool("Controls", "IgnoreTriggersWhenFullscreen", false);
        cfg.FullscreenExclusionList = ini.GetString("Controls", "FullscreenExclusionList", "");

        ParseMenu(cfg, ini);
        ParseIcons(cfg, ini);

        // ---- [Power] exclusions (legacy Exclude*, then new inclusion keys override) ----
        cfg.ExcludeSleep = ini.GetInt("Power", "ExcludeSleep", 0) != 0;
        cfg.ExcludeShutdown = ini.GetInt("Power", "ExcludeShutdown", 0) != 0;
        cfg.ExcludeRestart = ini.GetInt("Power", "ExcludeRestart", 0) != 0;
        cfg.ExcludeLock = ini.GetInt("Power", "ExcludeLock", 0) != 0;
        cfg.ExcludeLogoff = ini.GetInt("Power", "ExcludeLogoff", 0) != 0;
        cfg.ExcludeHibernate = ini.GetInt("Power", "ExcludeHibernate", 0) != 0;
        if (ini.GetInt("Power", "Sleep", 1) == 0) cfg.ExcludeSleep = true;
        if (ini.GetInt("Power", "Hibernate", 1) == 0) cfg.ExcludeHibernate = true;
        if (ini.GetInt("Power", "Shutdown", 1) == 0) cfg.ExcludeShutdown = true;
        if (ini.GetInt("Power", "Restart", 1) == 0) cfg.ExcludeRestart = true;
        if (ini.GetInt("Power", "Lock", 1) == 0) cfg.ExcludeLock = true;
        if (ini.GetInt("Power", "Logoff", 1) == 0) cfg.ExcludeLogoff = true;

        // ---- [Logging] ----
        var log = FirstNonEmpty(ini.GetString("General", "LogConfig"), ini.GetString("Debug", "LogConfig"))
            .ToLowerInvariant();
        cfg.LogLevel = log switch
        {
            "verbose" or "2" => 2,
            "basic" or "true" or "1" => 1,
            _ => 0,
        };
        var logFolder = Env.Expand(FirstNonEmpty(
            ini.GetString("General", "LogFolder"), ini.GetString("Debug", "LogFolder")));
        if (string.IsNullOrEmpty(logFolder))
            logFolder = AppContext.BaseDirectory.TrimEnd(System.IO.Path.DirectorySeparatorChar);
        cfg.LogFolderPath = logFolder;
        if (!string.IsNullOrEmpty(logFolder))
        {
            var configBase = System.IO.Path.GetFileNameWithoutExtension(cfg.IniPath);
            var now = DateTime.Now;
            var fname = $"WinMacMenu_{configBase}_{now:yyMMdd-HHmm}.log";
            cfg.LogFilePath = System.IO.Path.Combine(logFolder, fname);
        }

        return cfg;
    }

    private static void ParseMenu(Config cfg, IniFile ini)
    {
        for (int i = 1; i <= 64; i++)
        {
            var line = ini.GetString("Menu", $"Item{i}", "");
            if (string.IsNullOrEmpty(line))
                continue;

            // Label|TYPE|Path|Params|Icon — missing trailing parts default to empty/SEPARATOR.
            var parts = line.Split('|');
            string label = parts.Length > 0 ? parts[0] : "";
            string type = parts.Length > 1 ? parts[1] : "SEPARATOR";
            string path = parts.Length > 2 ? parts[2] : "";
            string ps = parts.Length > 3 ? parts[3] : "";
            string icon = parts.Length > 4 ? parts[4] : "";

            var it = new ConfigItem
            {
                Label = Env.Expand(label),
                Type = ParseType(type),
                Path = Env.Expand(path),
                Params = Env.Expand(ps),
                IconPath = Env.Expand(icon),
            };

            it.Submenu = it.Type is ConfigItemType.FolderSubmenu or ConfigItemType.RecentSubmenu;
            if (it.Type == ConfigItemType.ThisPc && cfg.ThisPCAsSubmenu) it.Submenu = true;
            if (it.Type == ConfigItemType.Home && cfg.HomeAsSubmenu) it.Submenu = true;

            bool isFolderLike = it.Type is ConfigItemType.Folder or ConfigItemType.ThisPc or ConfigItemType.Home;
            if (isFolderLike && !string.IsNullOrEmpty(it.Params))
            {
                var p = it.Params.ToLowerInvariant();
                if (p.Contains("submenu")) it.Submenu = true;
                else if (p.Contains("link")) it.Submenu = false;

                if (p.Contains("inline")) it.InlineExpand = true;
                else if (it.Type is ConfigItemType.ThisPc or ConfigItemType.Home && !it.Submenu) it.InlineExpand = true;
                else it.InlineExpand = false;

                if (it.InlineExpand)
                {
                    if (p.Contains("notitle") || p.Contains("noheader")) it.InlineNoHeader = true;
                    else if (p.Contains("title")) it.InlineNoHeader = false;
                    else it.InlineNoHeader = it.Type is ConfigItemType.ThisPc or ConfigItemType.Home;
                }
                it.InlineOpen = it.InlineExpand && p.Contains("inlineopen");
            }
            else if (it.Type == ConfigItemType.Folder)
            {
                // explicit defaults already false
            }
            else if (it.Type is ConfigItemType.ThisPc or ConfigItemType.Home)
            {
                it.InlineExpand = true;
                it.InlineNoHeader = true;
            }

            cfg.Items.Add(it);
        }
    }

    private static void ParseIcons(Config cfg, IniFile ini)
    {
        for (int i = 1; i <= cfg.Items.Count; i++)
        {
            var icon = ini.GetString("Icons", $"Icon{i}");
            if (!string.IsNullOrEmpty(icon)) cfg.Items[i - 1].IconPath = Env.Expand(icon);

            var l = ini.GetString("IconsLight", $"Icon{i}");
            if (!string.IsNullOrEmpty(l)) cfg.Items[i - 1].IconPathLight = Env.Expand(l);

            var d = ini.GetString("IconsDark", $"Icon{i}");
            if (!string.IsNullOrEmpty(d)) cfg.Items[i - 1].IconPathDark = Env.Expand(d);
        }
    }

    private static ConfigItemType ParseType(string s) => (s ?? "").ToUpperInvariant() switch
    {
        "SEPARATOR" => ConfigItemType.Separator,
        "CATEGORY" or "CAT" => ConfigItemType.Category,
        "URI" => ConfigItemType.Uri,
        "FILE" => ConfigItemType.File,
        "CMD" => ConfigItemType.Cmd,
        "FOLDER" => ConfigItemType.Folder,
        "FOLDER_SUBMENU" => ConfigItemType.FolderSubmenu,
        "POWER_SLEEP" => ConfigItemType.PowerSleep,
        "POWER_SHUTDOWN" => ConfigItemType.PowerShutdown,
        "POWER_RESTART" => ConfigItemType.PowerRestart,
        "POWER_LOCK" => ConfigItemType.PowerLock,
        "POWER_LOGOFF" => ConfigItemType.PowerLogoff,
        "POWER_HIBERNATE" => ConfigItemType.PowerHibernate,
        "RECENT_SUBMENU" or "RECENT" => ConfigItemType.RecentSubmenu,
        "POWER_MENU" => ConfigItemType.PowerMenu,
        "TASKKILL" => ConfigItemType.TaskKill,
        "THISPC" => ConfigItemType.ThisPc,
        "HOME" => ConfigItemType.Home,
        _ => ConfigItemType.Separator,
    };

    private static ControlAction ParseControlAction(string s)
    {
        var v = (s ?? "").Trim();
        if (v.Equals("Nothing", StringComparison.OrdinalIgnoreCase)) return ControlAction.Nothing;
        if (v.Equals("WinMacMenu", StringComparison.OrdinalIgnoreCase) || v.Equals("WinMac Menu", StringComparison.OrdinalIgnoreCase))
            return ControlAction.WinMacMenu;
        if (v.Equals("WindowsMenu", StringComparison.OrdinalIgnoreCase) || v.Equals("Windows Menu", StringComparison.OrdinalIgnoreCase)
            || v.Equals("WindowsStartMenu", StringComparison.OrdinalIgnoreCase) || v.Equals("Windows Start Menu", StringComparison.OrdinalIgnoreCase))
            return ControlAction.WindowsMenu;
        if (v.Equals("CustomCommand", StringComparison.OrdinalIgnoreCase) || v.Equals("Custom Command", StringComparison.OrdinalIgnoreCase)
            || v.Equals("Command", StringComparison.OrdinalIgnoreCase))
            return ControlAction.CustomCommand;
        return ControlAction.Nothing;
    }

    /// <summary>Reads [Controls].<paramref name="key"/> then falls back to [Control].<paramref name="key"/> and optional alias.</summary>
    private static bool TriggerBool(IniFile ini, string key, bool fallback, string? alias = null)
    {
        var v = ini.GetString("Controls", key);
        if (string.IsNullOrEmpty(v)) v = ini.GetString("Control", key);
        if (string.IsNullOrEmpty(v) && alias != null) v = ini.GetString("Controls", alias);
        if (string.IsNullOrEmpty(v)) return fallback;
        return v.Equals("true", StringComparison.OrdinalIgnoreCase) || v == "1";
    }

    /// <summary>
    /// Resolves a "show extensions" flag with the C app's back-compat chain:
    /// primary key, optional legacy "show" alias, then an inverse "hide" key overrides.
    /// </summary>
    private static bool ResolveShowExtensions(IniFile ini, string section, string showKey, string? legacyShowKey, string hideKey)
    {
        bool show = true;
        var v = ini.GetString(section, showKey);
        if (!string.IsNullOrEmpty(v))
            show = v.Equals("true", StringComparison.OrdinalIgnoreCase) || v == "1";
        else if (legacyShowKey != null)
        {
            var old = ini.GetString(section, legacyShowKey);
            if (!string.IsNullOrEmpty(old))
                show = old.Equals("true", StringComparison.OrdinalIgnoreCase) || old == "1";
        }
        var hide = ini.GetString(section, hideKey);
        if (!string.IsNullOrEmpty(hide))
            show = !(hide.Equals("true", StringComparison.OrdinalIgnoreCase) || hide == "1");
        return show;
    }

    private static void ParseIgnoreOffset(string value, out bool h, out bool v)
    {
        h = false; v = false;
        var s = (value ?? "").Trim();
        if (s.Equals("true", StringComparison.OrdinalIgnoreCase) || s.Equals("both", StringComparison.OrdinalIgnoreCase))
        { h = true; v = true; }
        else if (s.Equals("hoffset", StringComparison.OrdinalIgnoreCase) || s.Equals("h", StringComparison.OrdinalIgnoreCase) || s.Equals("horizontal", StringComparison.OrdinalIgnoreCase))
            h = true;
        else if (s.Equals("voffset", StringComparison.OrdinalIgnoreCase) || s.Equals("v", StringComparison.OrdinalIgnoreCase) || s.Equals("vertical", StringComparison.OrdinalIgnoreCase))
            v = true;
    }

    private static string FirstNonEmpty(params string[] values)
        => values.FirstOrDefault(x => !string.IsNullOrEmpty(x)) ?? string.Empty;

    private static void WriteDefaultIni(string path)
    {
        try
        {
            var dir = System.IO.Path.GetDirectoryName(path);
            if (!string.IsNullOrEmpty(dir)) Directory.CreateDirectory(dir);
            File.WriteAllText(path, DefaultIni.Replace("\n", "\r\n"));
        }
        catch
        {
            // Best-effort, mirroring the C app which ignores write failures.
        }
    }

    // Mirrors the DEFAULT_INI template embedded in config.c.
    private const string DefaultIni =
@"[General]
FolderSubmenuDepth=4
RunInBackground=true
ShowDotfiles=false
ShowFileExtensions=true
ShowFolderIcons=true
ShowFileIcons=true
KeepMenuOpenAfterContextAction=false
ShowHidden=false
ShowIcons=true
ShowOnLaunch=false
ShowTrayIcon=true
MonochromeTrayIcon=true
StartOnLogin=false

[Placement]
PointerRelative=true
HOffset=0
VOffset=0

[Appearance]
LargeMenuIcons=false
KeepLargeMenuHighlightTextColor=false
LargeMenuAnimation=bottom

[Sorting]
SortBy=name
SortDirection=ascending
FoldersFirst=true

[Controls]
WindowsKey=false
ShiftWindowsKey=false
LeftClick=false
RightClick=false
MiddleClick=true
ShiftLeftClick=false
ShiftRightClick=false
ShiftMiddleClick=false
IgnoreTriggersWhenFullscreen=false
FullscreenExclusionList=

[Menu]
Item1=Apps and Features|URI|ms-settings:appsfeatures
Item2=About Windows|URI|winver
Item3=---
Item4=System Settings|URI|ms-settings:
Item5=File Explorer|THISPC
Item6=User Profile|HOME
Item7=---
Item8=Recent Items|RECENT
Item9=---
Item10=End task|TASKKILL
Item11=---
Item12=Sleep|POWER_SLEEP
Item13=Restart|POWER_RESTART
Item14=Shut down|POWER_SHUTDOWN
Item15=---
Item16=Event Viewer|URI|eventvwr
Item17=Task Scheduler|URI|taskschd.msc
Item18=Task Manager|URI|taskmgr
Item19=---
Item20=Lock screen|POWER_LOCK
Item21=Sign out %USERNAME%|POWER_LOGOFF

[Icons]
Icon1=shell32.dll,-271
Icon2=shell32.dll,-1001
Icon4=shell32.dll,-16826
Icon5=imageres.dll,-5325
Icon6=imageres.dll,-88
Icon8=shell32.dll,-327
Icon10=shell32.dll,-200
Icon16=eventvwr.exe,0
Icon17=powercpl.dll,-513
Icon18=taskmgr.exe,0

[RecentItems]
RecentLabel=fullpath
RecentMax=12
RecentShowCleanItems=true
RecentShowExtensions=true
RecentShowIcons=true

[TaskKill]
TaskKillAllDesktops=true
TaskKillExcludes=
TaskKillIgnoreSystem=true
TaskKillListWindows=false
TaskKillMax=24
TaskKillShowIcons=true

[ThisPC]
ThisPCAsSubmenu=true
ThisPCItemsAsSubmenus=true
ThisPCShowIcons=true

[Home]
HomeAsSubmenu=true
HomeItemsAsSubmenus=true
HomeShowIcons=true
";
}
