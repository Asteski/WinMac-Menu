namespace WinMacMenuWinUI3.Models;

public enum ConfigItemType
{
    Separator,
    Category,
    Uri,
    File,
    Cmd,
    Folder,
    FolderSubmenu,
    Recent,
    RecentSubmenu,
    ThisPC,
    Home,
    PowerSleep,
    PowerShutdown,
    PowerRestart,
    PowerLock,
    PowerLogoff,
    PowerHibernate,
    PowerMenu,
    TaskKill,
}

public class ConfigItem
{
    public string Label { get; set; } = "";
    public ConfigItemType Type { get; set; }
    public string Path { get; set; } = "";
    public string Params { get; set; } = "";
    public string IconPath { get; set; } = "";
    public string IconPathLight { get; set; } = "";
    public string IconPathDark { get; set; } = "";

    // Folder display modes
    public bool Submenu { get; set; }
    public bool InlineExpand { get; set; }
    public bool InlineOpen { get; set; }
    public bool InlineNoHeader { get; set; }

    public bool IsSeparator => Type == ConfigItemType.Separator;
    public bool IsCategory => Type == ConfigItemType.Category;
}

public class AppConfig
{
    // [General]
    public int FolderSubmenuDepth { get; set; } = 4;
    public bool RunInBackground { get; set; } = true;
    public bool ShowOnLaunch { get; set; } = false;
    public bool ShowTrayIcon { get; set; } = true;
    public bool ShowIcons { get; set; } = true;        // icons on root menu items
    public bool ShowFolderIcons { get; set; } = true;  // icons inside folder submenus
    public bool ShowFileIcons { get; set; } = true;    // file icons inside folder submenus
    public bool ShowFileExtensions { get; set; } = true;
    public bool ShowHidden { get; set; } = false;
    public int MaxItems { get; set; } = 40;
    public string DefaultIconPath { get; set; } = "";
    public string MenuStyle { get; set; } = "modern";

    // [Placement]
    public bool PointerRelative { get; set; } = true;
    public string Horizontal { get; set; } = "left";
    public int HOffset { get; set; } = 0;
    public string Vertical { get; set; } = "top";
    public int VOffset { get; set; } = 0;

    public string Corners { get; set; } = "rounded";

    // [Sorting]
    public string SortBy { get; set; } = "name";
    public string SortDirection { get; set; } = "ascending";
    public bool FoldersFirst { get; set; } = true;

    // [RecentItems]
    public int RecentMax { get; set; } = 12;
    public string RecentLabel { get; set; } = "fullpath";
    public bool RecentShowExtensions { get; set; } = true;
    public bool RecentShowIcons { get; set; } = true;
    public bool RecentShowCleanItems { get; set; } = true;

    // [TaskKill]
    public int TaskKillMax { get; set; } = 24;
    public bool TaskKillIgnoreSystem { get; set; } = true;
    public bool TaskKillShowIcons { get; set; } = true;
    public bool TaskKillListWindows { get; set; } = false;
    public bool TaskKillAllDesktops { get; set; } = true;
    public string TaskKillExcludes { get; set; } = "";

    // [ThisPC]
    public bool ThisPCAsSubmenu { get; set; } = true;
    public bool ThisPCItemsAsSubmenus { get; set; } = false;
    public bool ThisPCShowIcons { get; set; } = true;

    // [Home]
    public bool HomeAsSubmenu { get; set; } = true;
    public bool HomeItemsAsSubmenus { get; set; } = false;
    public bool HomeShowIcons { get; set; } = true;

    // [Power]
    public bool PowerSleep { get; set; } = true;
    public bool PowerHibernate { get; set; } = true;
    public bool PowerShutdown { get; set; } = true;
    public bool PowerRestart { get; set; } = true;
    public bool PowerLock { get; set; } = true;
    public bool PowerLogoff { get; set; } = true;

    // Menu items (up to 64)
    public List<ConfigItem> Items { get; set; } = new();
}
