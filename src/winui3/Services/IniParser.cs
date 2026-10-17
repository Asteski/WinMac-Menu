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

        cfg.FolderSubmenuDepth = Math.Clamp(GetInt("General", "FolderSubmenuDepth", 2, iniPath), 0, 4);
        cfg.RunInBackground = GetBool("General", "RunInBackground", true, iniPath);
        cfg.ShowOnLaunch = GetBool("General", "ShowOnLaunch", false, iniPath);
        cfg.ShowTrayIcon = GetBool("General", "ShowTrayIcon", true, iniPath);
        var rawIconMode = GetString("General", "ShowIcons", "", iniPath).Trim();
        var hasUnifiedIconMode = rawIconMode.Equals("all", StringComparison.OrdinalIgnoreCase) ||
                                  rawIconMode.Equals("files", StringComparison.OrdinalIgnoreCase) ||
                                  rawIconMode.Equals("folders", StringComparison.OrdinalIgnoreCase) ||
                                  rawIconMode.Equals("true", StringComparison.OrdinalIgnoreCase) ||
                                  rawIconMode == "1" ||
                                  rawIconMode.Equals("false", StringComparison.OrdinalIgnoreCase) ||
                                  rawIconMode == "0" ||
                                  rawIconMode.Equals("other", StringComparison.OrdinalIgnoreCase);
        cfg.IconMode = rawIconMode.ToLowerInvariant() switch
        {
            "all" or "true" or "1" => IconDisplayMode.All,
            "files" or "filesonly" or "files-only" => IconDisplayMode.FilesOnly,
            "folders" or "foldersonly" or "folders-only" => IconDisplayMode.FoldersOnly,
            "other" => IconDisplayMode.All,
            "" => IconDisplayMode.All,
            _ => IconDisplayMode.Disabled,
        };
        var hideFolderIcons = GetString("General", "HideFolderIcons", "", iniPath);
        var legacyShowFolderIcons = !string.IsNullOrWhiteSpace(hideFolderIcons)
            ? !GetBool("General", "HideFolderIcons", false, iniPath)
            : GetBool("General", "ShowFolderIcons", true, iniPath);
        var hideFileIcons = GetString("General", "HideFileIcons", "", iniPath);
        if (!string.IsNullOrWhiteSpace(hideFileIcons))
        {
            var legacyShowFileIcons = !GetBool("General", "HideFileIcons", false, iniPath);
            if (!hasUnifiedIconMode)
                cfg.IconMode = legacyShowFolderIcons == legacyShowFileIcons ? (legacyShowFolderIcons ? IconDisplayMode.All : IconDisplayMode.Disabled) : (legacyShowFolderIcons ? IconDisplayMode.FoldersOnly : IconDisplayMode.FilesOnly);
        }
        else
        {
            var showFileIcons = GetString("General", "ShowFileIcons", "", iniPath);
            var legacyShowFileIcons = !string.IsNullOrWhiteSpace(showFileIcons)
                ? GetBool("General", "ShowFileIcons", true, iniPath)
                : GetBool("General", "ShowSubmenuFileIcons", true, iniPath);
            if (!hasUnifiedIconMode)
                cfg.IconMode = legacyShowFolderIcons == legacyShowFileIcons ? (legacyShowFolderIcons ? IconDisplayMode.All : IconDisplayMode.Disabled) : (legacyShowFolderIcons ? IconDisplayMode.FoldersOnly : IconDisplayMode.FilesOnly);
        }
        cfg.HideIconsInRootMenu = GetBool("General", "HideIconsInRootMenu", false, iniPath) || rawIconMode.Equals("other", StringComparison.OrdinalIgnoreCase);
        var rawShowFileExtensions = GetString("General", "ShowFileExtensions", "", iniPath).Trim();
        var hasGeneralExtensionSetting = !string.IsNullOrWhiteSpace(rawShowFileExtensions);
        if (hasGeneralExtensionSetting)
        {
            cfg.ShowFileExtensions = rawShowFileExtensions is "true" or "1" or "yes";
        }
        else
        {
            var rawLegacyShowExtensions = GetString("General", "ShowExtensions", "", iniPath).Trim();
            if (!string.IsNullOrWhiteSpace(rawLegacyShowExtensions))
            {
                cfg.ShowFileExtensions = rawLegacyShowExtensions is "true" or "1" or "yes";
                hasGeneralExtensionSetting = true;
            }
            else
            {
                var rawLegacyHideExtensions = GetString("General", "HideExtensions", "", iniPath).Trim();
                if (!string.IsNullOrWhiteSpace(rawLegacyHideExtensions))
                {
                    cfg.ShowFileExtensions = !(rawLegacyHideExtensions is "true" or "1" or "yes");
                    hasGeneralExtensionSetting = true;
                }
            }
        }
        if (!hasGeneralExtensionSetting)
        {
            var rawRecentExtensions = GetString("RecentItems", "RecentShowExtensions", "", iniPath).Trim();
            if (!string.IsNullOrWhiteSpace(rawRecentExtensions))
                cfg.ShowFileExtensions = rawRecentExtensions is "true" or "1" or "yes";
            else
            {
                var rawRecentHideExtensions = GetString("RecentItems", "RecentHideExtensions", "", iniPath).Trim();
                if (!string.IsNullOrWhiteSpace(rawRecentHideExtensions))
                    cfg.ShowFileExtensions = !(rawRecentHideExtensions is "true" or "1" or "yes");
            }
        }
        cfg.ShowHidden = GetBool("General", "ShowHidden", false, iniPath);
        cfg.ShowDotfiles = GetBool("General", "ShowDotfiles", false, iniPath);
        cfg.KeepMenuOpenAfterContextAction = GetBool("General", "KeepMenuOpenAfterContextAction", false, iniPath);
        cfg.MaxItems = GetInt("General", "MaxItems", 0, iniPath);
        cfg.SubmenuMetadataThreshold = GetString("General", "SubmenuMetadataThreshold", "4", iniPath).Trim();
        if (string.IsNullOrWhiteSpace(cfg.SubmenuMetadataThreshold))
            cfg.SubmenuMetadataThreshold = "4";
        cfg.DefaultIconPath = GetString("General", "DefaultIcon", GetString("Icons", "DefaultIcon", "", iniPath), iniPath);
        cfg.DefaultIconPathLight = GetString("General", "DefaultIconLight", GetString("Icons", "DefaultIconLight", "", iniPath), iniPath);
        cfg.DefaultIconPathDark = GetString("General", "DefaultIconDark", GetString("Icons", "DefaultIconDark", "", iniPath), iniPath);
        cfg.TrayIconPath    = GetString("General", "TrayIcon",   "", iniPath);
        cfg.TrayIconPathLight = GetString("General", "TrayIconLight", "", iniPath);
        cfg.TrayIconPathDark = GetString("General", "TrayIconDark", "", iniPath);
        cfg.MonochromeTrayIcon = GetBool("General", "MonochromeTrayIcon", false, iniPath);
        cfg.PointerRelative = GetBool("Placement", "PointerRelative", true, iniPath);
        cfg.Horizontal = GetString("Placement", "Horizontal", "left", iniPath);
        cfg.HOffset = GetInt("Placement", "HOffset", 13, iniPath);
        cfg.Vertical = GetString("Placement", "Vertical", "top", iniPath);
        cfg.VOffset = GetInt("Placement", "VOffset", 13, iniPath);
        var ignoreRelative = GetString("Placement", "IgnoreOffsetWhenRelative", "true", iniPath).ToLowerInvariant();
        cfg.IgnoreHOffsetWhenRelative = ignoreRelative is "true" or "1" or "yes" or "hoffset" or "h" or "horizontal";
        cfg.IgnoreVOffsetWhenRelative = ignoreRelative is "true" or "1" or "yes" or "voffset" or "v" or "vertical";

        cfg.Corners = GetString("Appearance", "Corners", "rounded", iniPath);

        var touchMode = GetString("Appearance", "TouchMode", "", iniPath);
        if (!string.IsNullOrWhiteSpace(touchMode))
        {
            cfg.WinUI3Size = GetBool("Appearance", "TouchMode", false, iniPath) ? "large" : "default";
        }
        else
        {
            cfg.WinUI3Size = GetString("Appearance", "WinUISize", "", iniPath);
            if (string.IsNullOrWhiteSpace(cfg.WinUI3Size))
                cfg.WinUI3Size = GetString("WinUI3", "Size", "default", iniPath);
        }
        if (cfg.WinUI3Size.Equals("compact", StringComparison.OrdinalIgnoreCase))
            cfg.WinUI3Size = "default";
        else if (!cfg.WinUI3Size.Equals("default", StringComparison.OrdinalIgnoreCase) &&
                 !cfg.WinUI3Size.Equals("large", StringComparison.OrdinalIgnoreCase))
            cfg.WinUI3Size = "default";
        var showThumbnails = GetString("Appearance", "ShowThumbnails", "", iniPath);
        if (!string.IsNullOrWhiteSpace(showThumbnails))
        {
            cfg.WinUIAlwaysShowIcons = !GetBool("Appearance", "ShowThumbnails", true, iniPath);
        }
        else
        {
            var alwaysShowIcons = GetString("Appearance", "AlwaysShowIcons", "", iniPath);
            cfg.WinUIAlwaysShowIcons = !string.IsNullOrWhiteSpace(alwaysShowIcons)
                ? GetBool("Appearance", "AlwaysShowIcons", false, iniPath)
                : GetBool("Appearance", "WinUIAlwaysShowIcons", false, iniPath);
        }

        cfg.WindowsKey      = GetBool("Controls", "WindowsKey",      false, iniPath);
        cfg.ShiftWindowsKey = GetBool("Controls", "ShiftWindowsKey", false, iniPath);
        cfg.WindowsKeyX     = GetBool("Controls", "WindowsKeyX",     false, iniPath);
        cfg.LeftClick       = GetBool("Controls", "LeftClick",       false, iniPath);
        cfg.RightClick      = GetBool("Controls", "RightClick",      false, iniPath);
        cfg.MiddleClick     = GetBool("Controls", "MiddleClick",     true,  iniPath);
        cfg.ShiftLeftClick  = GetBool("Controls", "ShiftLeftClick",  false, iniPath);
        cfg.ShiftRightClick = GetBool("Controls", "ShiftRightClick", false, iniPath);
        cfg.ShiftMiddleClick= GetBool("Controls", "ShiftMiddleClick",false, iniPath);
        cfg.ShowSettingsItem = GetString("Controls", "ShowSettingsItem", "disabled", iniPath).Trim().ToLowerInvariant() switch
        {
            "shift" or "with shift" => SettingsItemMode.Shift,
            "winx" or "win+x" or "win + x" => SettingsItemMode.WinX,
            "rightclick" or "right click" => SettingsItemMode.RightClick,
            "middleclick" or "middle click" => SettingsItemMode.MiddleClick,
            "always" => SettingsItemMode.Always,
            _ => SettingsItemMode.Disabled,
        };
        cfg.IgnoreTriggersWhenFullscreen = GetBool("Controls", "IgnoreTriggersWhenFullscreen", false, iniPath);
        cfg.FullscreenExclusionList      = GetString("Controls", "FullscreenExclusionList", "", iniPath);

        cfg.SortBy = GetString("Sorting", "SortBy", "name", iniPath);
        cfg.SortDirection = GetString("Sorting", "SortDirection", "ascending", iniPath);
        cfg.FoldersFirst = GetBool("Sorting", "FoldersFirst", true, iniPath);

        cfg.RecentMax = GetInt("RecentItems", "RecentMax", 12, iniPath);
        cfg.RecentLabel = GetString("RecentItems", "RecentLabel", "fullpath", iniPath);
        cfg.RecentShowCleanItems = GetBool("RecentItems", "RecentShowCleanItems", true, iniPath);
        cfg.RecentHideProtocolShortcuts = GetBool("RecentItems", "RecentHideProtocolShortcuts", true, iniPath);
        cfg.RecentSeparateItems = GetBool("RecentItems", "SeparateItems", false, iniPath);

        cfg.TaskKillMax = GetInt("TaskKill", "TaskKillMax", 24, iniPath);
        cfg.TaskKillIgnoreSystem = GetBool("TaskKill", "TaskKillIgnoreSystem", true, iniPath);
        cfg.TaskKillListWindows = GetBool("TaskKill", "TaskKillListWindows", false, iniPath);
        cfg.TaskKillAllDesktops = GetBool("TaskKill", "TaskKillAllDesktops", true, iniPath);
        cfg.TaskKillShowProcessIds = GetBool("TaskKill", "TaskKillShowProcessIds", true, iniPath);
        cfg.TaskKillExcludes = GetString("TaskKill", "TaskKillExcludes", "", iniPath);

        cfg.ThisPCAsSubmenu      = GetBool("ThisPC", "ThisPCAsSubmenu",      true,  iniPath);
        cfg.ThisPCItemsAsSubmenus = GetBool("ThisPC", "ThisPCItemsAsSubmenus", false, iniPath);

        cfg.HomeAsSubmenu      = GetBool("Home", "HomeAsSubmenu",      true,  iniPath);
        cfg.HomeItemsAsSubmenus = GetBool("Home", "HomeItemsAsSubmenus", false, iniPath);

        cfg.PowerSleep = GetInt("Power", "Sleep", 1, iniPath) != 0;
        cfg.PowerHibernate = GetInt("Power", "Hibernate", 1, iniPath) != 0;
        cfg.PowerShutdown = GetInt("Power", "Shutdown", 1, iniPath) != 0;
        cfg.PowerRestart = GetInt("Power", "Restart", 1, iniPath) != 0;
        cfg.PowerLock = GetInt("Power", "Lock", 1, iniPath) != 0;
        cfg.PowerLogoff = GetInt("Power", "Logoff", 1, iniPath) != 0;
        cfg.PowerAskForConfirmation = GetBool("Power", "AskForConfirmation", false, iniPath);

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
            "SETTINGS" or "WINMAC_SETTINGS" or "WINMACMENU_SETTINGS" => ConfigItemType.Settings,
            _                => ConfigItemType.Uri,
        };

        if (string.IsNullOrWhiteSpace(item.Label))
        {
            item.Label = item.Type switch
            {
                ConfigItemType.Recent or ConfigItemType.RecentSubmenu => "Recent Items",
                ConfigItemType.ThisPC => "This PC",
                ConfigItemType.Home => "Home",
                _ => item.Label,
            };
        }

        if (parts.Length > 2)
            item.Path = Environment.ExpandEnvironmentVariables(parts[2].Trim());

        if (parts.Length > 3)
        {
            item.Params = parts[3].Trim().ToLowerInvariant();
            // Both folder item types can use inline parameters to emit the
            // folder's objects directly into the root menu.
            if (item.Type is ConfigItemType.Folder or ConfigItemType.FolderSubmenu or ConfigItemType.ThisPC or ConfigItemType.Home)
                ParseFolderParams(item, item.Params);
        }

        if ((item.Type is ConfigItemType.ThisPC or ConfigItemType.Home) && !item.InlineExpand && !item.Submenu)
        {
            item.InlineExpand = true;
            item.InlineNoHeader = true;
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

        if ((item.Type is ConfigItemType.ThisPC or ConfigItemType.Home) && !item.Submenu && !item.InlineExpand)
            item.InlineExpand = true;
        if ((item.Type is ConfigItemType.ThisPC or ConfigItemType.Home) && item.InlineExpand &&
            !paramsLower.Contains("notitle") && !paramsLower.Contains("noheader") && paramsLower != "title")
            item.InlineNoHeader = true;
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
