using System.Runtime.InteropServices;
using System.Text;
using WinMacMenuWinUI3.Models;

namespace WinMacMenuWinUI3.Services;

public static class IniParser
{
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode)]
    private static extern uint GetPrivateProfileString(
        string lpAppName, string? lpKeyName, string lpDefault,
        StringBuilder lpReturnedString, uint nSize, string lpFileName);

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode)]
    private static extern int GetPrivateProfileInt(
        string lpAppName, string lpKeyName, int nDefault, string lpFileName);

    private static string GetString(string section, string key, string def, string path)
    {
        var sb = new StringBuilder(1024);
        GetPrivateProfileString(section, key, def, sb, (uint)sb.Capacity, path);
        return Environment.ExpandEnvironmentVariables(sb.ToString());
    }

    private static bool GetBool(string section, string key, bool def, string path)
    {
        var val = GetString(section, key, def ? "true" : "false", path).ToLowerInvariant();
        return val is "true" or "1" or "yes";
    }

    private static int GetInt(string section, string key, int def, string path)
        => GetPrivateProfileInt(section, key, def, path);

    public static AppConfig Load(string iniPath)
    {
        var cfg = new AppConfig();

        cfg.FolderSubmenuDepth = GetInt("General", "FolderSubmenuDepth", 4, iniPath);
        cfg.RunInBackground = GetBool("General", "RunInBackground", true, iniPath);
        cfg.ShowOnLaunch = GetBool("General", "ShowOnLaunch", false, iniPath);
        cfg.ShowTrayIcon = GetBool("General", "ShowTrayIcon", true, iniPath);
        cfg.ShowIcons = GetBool("General", "ShowIcons", true, iniPath);
        cfg.ShowFolderIcons = GetBool("General", "ShowFolderIcons", true, iniPath);
        cfg.ShowFileIcons = GetBool("General", "ShowFileIcons", true, iniPath);
        cfg.ShowFileExtensions = GetBool("General", "ShowFileExtensions", true, iniPath);
        cfg.ShowHidden = GetBool("General", "ShowHidden", false, iniPath);
        cfg.MaxItems = GetInt("General", "MaxItems", 0, iniPath);
        cfg.DefaultIconPath = GetString("General", "DefaultIcon", "", iniPath);
        cfg.TrayIconPath    = GetString("General", "TrayIcon",   "", iniPath);
        cfg.MenuStyle = GetString("General", "MenuStyle", "modern", iniPath);

        cfg.PointerRelative = GetBool("Placement", "PointerRelative", true, iniPath);
        cfg.Horizontal = GetString("Placement", "Horizontal", "left", iniPath);
        cfg.HOffset = GetInt("Placement", "HOffset", 0, iniPath);
        cfg.Vertical = GetString("Placement", "Vertical", "top", iniPath);
        cfg.VOffset = GetInt("Placement", "VOffset", 0, iniPath);

        cfg.Corners = GetString("Appearance", "Corners", "rounded", iniPath);

        cfg.WinUI3Size = GetString("Appearance", "WinUISize", "", iniPath);
        if (string.IsNullOrWhiteSpace(cfg.WinUI3Size))
            cfg.WinUI3Size = GetString("WinUI3", "Size", "compact", iniPath);
        if (!cfg.WinUI3Size.Equals("default", StringComparison.OrdinalIgnoreCase) &&
            !cfg.WinUI3Size.Equals("compact", StringComparison.OrdinalIgnoreCase))
            cfg.WinUI3Size = "compact";

        cfg.WindowsKey      = GetBool("Controls", "WindowsKey",      false, iniPath);
        cfg.ShiftWindowsKey = GetBool("Controls", "ShiftWindowsKey", false, iniPath);
        cfg.LeftClick       = GetBool("Controls", "LeftClick",       false, iniPath);
        cfg.RightClick      = GetBool("Controls", "RightClick",      false, iniPath);
        cfg.MiddleClick     = GetBool("Controls", "MiddleClick",     true,  iniPath);
        cfg.ShiftLeftClick  = GetBool("Controls", "ShiftLeftClick",  false, iniPath);
        cfg.ShiftRightClick = GetBool("Controls", "ShiftRightClick", false, iniPath);
        cfg.ShiftMiddleClick= GetBool("Controls", "ShiftMiddleClick",false, iniPath);
        cfg.IgnoreTriggersWhenFullscreen = GetBool("Controls", "IgnoreTriggersWhenFullscreen", false, iniPath);
        cfg.FullscreenExclusionList      = GetString("Controls", "FullscreenExclusionList", "", iniPath);

        cfg.SortBy = GetString("Sorting", "SortBy", "name", iniPath);
        cfg.SortDirection = GetString("Sorting", "SortDirection", "ascending", iniPath);
        cfg.FoldersFirst = GetBool("Sorting", "FoldersFirst", true, iniPath);

        cfg.RecentMax = GetInt("RecentItems", "RecentMax", 12, iniPath);
        cfg.RecentLabel = GetString("RecentItems", "RecentLabel", "fullpath", iniPath);
        cfg.RecentShowExtensions = GetBool("RecentItems", "RecentShowExtensions", true, iniPath);
        cfg.RecentShowIcons = GetBool("RecentItems", "RecentShowIcons", true, iniPath);
        cfg.RecentShowCleanItems = GetBool("RecentItems", "RecentShowCleanItems", true, iniPath);
        cfg.RecentSeparateItems = GetBool("RecentItems", "SeparateItems", false, iniPath);

        cfg.TaskKillMax = GetInt("TaskKill", "TaskKillMax", 24, iniPath);
        cfg.TaskKillIgnoreSystem = GetBool("TaskKill", "TaskKillIgnoreSystem", true, iniPath);
        cfg.TaskKillShowIcons = GetBool("TaskKill", "TaskKillShowIcons", true, iniPath);
        cfg.TaskKillListWindows = GetBool("TaskKill", "TaskKillListWindows", false, iniPath);
        cfg.TaskKillAllDesktops = GetBool("TaskKill", "TaskKillAllDesktops", true, iniPath);
        cfg.TaskKillExcludes = GetString("TaskKill", "TaskKillExcludes", "", iniPath);

        cfg.ThisPCAsSubmenu      = GetBool("ThisPC", "ThisPCAsSubmenu",      true,  iniPath);
        cfg.ThisPCItemsAsSubmenus = GetBool("ThisPC", "ThisPCItemsAsSubmenus", false, iniPath);
        cfg.ThisPCShowIcons       = GetBool("ThisPC", "ThisPCShowIcons",       true,  iniPath);

        cfg.HomeAsSubmenu      = GetBool("Home", "HomeAsSubmenu",      true,  iniPath);
        cfg.HomeItemsAsSubmenus = GetBool("Home", "HomeItemsAsSubmenus", false, iniPath);
        cfg.HomeShowIcons       = GetBool("Home", "HomeShowIcons",       true,  iniPath);

        cfg.PowerSleep = GetInt("Power", "Sleep", 1, iniPath) != 0;
        cfg.PowerHibernate = GetInt("Power", "Hibernate", 1, iniPath) != 0;
        cfg.PowerShutdown = GetInt("Power", "Shutdown", 1, iniPath) != 0;
        cfg.PowerRestart = GetInt("Power", "Restart", 1, iniPath) != 0;
        cfg.PowerLock = GetInt("Power", "Lock", 1, iniPath) != 0;
        cfg.PowerLogoff = GetInt("Power", "Logoff", 1, iniPath) != 0;

        // Read icons
        var icons = new Dictionary<int, string>();
        var iconsLight = new Dictionary<int, string>();
        var iconsDark = new Dictionary<int, string>();

        for (int i = 1; i <= 64; i++)
        {
            var icon = GetString("Icons", $"Icon{i}", "", iniPath);
            if (!string.IsNullOrEmpty(icon)) icons[i] = icon;
            var iconLight = GetString("IconsLight", $"Icon{i}", "", iniPath);
            if (!string.IsNullOrEmpty(iconLight)) iconsLight[i] = iconLight;
            var iconDark = GetString("IconsDark", $"Icon{i}", "", iniPath);
            if (!string.IsNullOrEmpty(iconDark)) iconsDark[i] = iconDark;
        }

        // Read menu items
        for (int i = 1; i <= 64; i++)
        {
            var raw = GetString("Menu", $"Item{i}", "", iniPath);
            if (string.IsNullOrEmpty(raw)) continue;

            var item = ParseMenuLine(raw);
            if (item == null) continue;

            if (icons.TryGetValue(i, out var ic)) item.IconPath = ic;
            if (iconsLight.TryGetValue(i, out var icL)) item.IconPathLight = icL;
            if (iconsDark.TryGetValue(i, out var icD)) item.IconPathDark = icD;

            cfg.Items.Add(item);
        }

        return cfg;
    }

    private static ConfigItem? ParseMenuLine(string line)
    {
        var parts = line.Split('|');
        if (parts.Length == 0) return null;

        // Separator: bare "---" or "---|anything"
        if (parts[0].Trim() == "---")
            return new ConfigItem { Type = ConfigItemType.Separator };

        var item = new ConfigItem();
        item.Label = Environment.ExpandEnvironmentVariables(parts[0].Trim());

        if (parts.Length == 1)
            return item;

        var typeStr = parts[1].Trim().ToUpperInvariant();
        item.Type = typeStr switch
        {
            "SEPARATOR"      => ConfigItemType.Separator,
            "CATEGORY"       => ConfigItemType.Category,
            "URI"            => ConfigItemType.Uri,
            "FILE"           => ConfigItemType.File,
            "CMD"            => ConfigItemType.Cmd,
            "FOLDER"         => ConfigItemType.Folder,
            "FOLDER_SUBMENU" => ConfigItemType.FolderSubmenu,
            "RECENT"         => ConfigItemType.Recent,
            "RECENT_SUBMENU" => ConfigItemType.RecentSubmenu,
            "THISPC"         => ConfigItemType.ThisPC,
            "HOME"           => ConfigItemType.Home,
            "POWER_SLEEP"    => ConfigItemType.PowerSleep,
            "POWER_SHUTDOWN" => ConfigItemType.PowerShutdown,
            "POWER_RESTART"  => ConfigItemType.PowerRestart,
            "POWER_LOCK"     => ConfigItemType.PowerLock,
            "POWER_LOGOFF"   => ConfigItemType.PowerLogoff,
            "POWER_HIBERNATE"=> ConfigItemType.PowerHibernate,
            "POWER_MENU"     => ConfigItemType.PowerMenu,
            "TASKKILL"       => ConfigItemType.TaskKill,
            _                => ConfigItemType.Uri,
        };

        if (parts.Length > 2)
            item.Path = Environment.ExpandEnvironmentVariables(parts[2].Trim());

        if (parts.Length > 3)
        {
            item.Params = parts[3].Trim().ToLowerInvariant();
            ParseFolderParams(item, item.Params);
        }

        if (parts.Length > 4)
            item.IconPath = Environment.ExpandEnvironmentVariables(parts[4].Trim());

        return item;
    }

    private static void ParseFolderParams(ConfigItem item, string paramsLower)
    {
        if (paramsLower.Contains("submenu"))   item.Submenu = true;
        if (paramsLower.Contains("link"))      item.Submenu = false;
        if (paramsLower.Contains("inline"))    item.InlineExpand = true;
        if (paramsLower.Contains("inlineopen"))item.InlineOpen = true;
        if (paramsLower.Contains("notitle") || paramsLower.Contains("noheader"))
            item.InlineNoHeader = true;
        if (paramsLower == "title")            item.InlineNoHeader = false;
    }

    public static string? FindConfigFile(string? exeDir = null)
    {
        var dir = exeDir ?? AppContext.BaseDirectory;
        var candidates = new[] { "config.ini", "WinMacMenu.ini" };
        foreach (var name in candidates)
        {
            var path = Path.Combine(dir, name);
            if (File.Exists(path)) return path;
        }
        return null;
    }
}
