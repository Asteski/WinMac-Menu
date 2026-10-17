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
    Settings,
}

public enum SettingsItemMode { Disabled, Shift, WinX, RightClick, MiddleClick, Always }
public enum MenuTriggerType { Other, Shift, WinX, RightClick, MiddleClick }
public enum IconDisplayMode { Disabled, All, FilesOnly, FoldersOnly }

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
    public int FolderSubmenuDepth { get; set; } = 2;
    public bool RunInBackground { get; set; } = true;
    public bool ShowOnLaunch { get; set; } = false;
    public bool ShowTrayIcon { get; set; } = true;
    public IconDisplayMode IconMode { get; set; } = IconDisplayMode.All;
    public bool HideIconsInRootMenu { get; set; } = false;
    public bool ShowFileExtensions { get; set; } = true;
    public bool ShowHidden { get; set; } = false;
    public bool ShowDotfiles { get; set; } = false;
    public bool KeepMenuOpenAfterContextAction { get; set; } = false;
    public int MaxItems { get; set; } = 0;
    public string SubmenuMetadataThreshold { get; set; } = "4"; // dynamic uses only item-count caching; 0 disables folder metadata reuse
    public string DefaultIconPath { get; set; } = "";
    public string TrayIconPath { get; set; } = "";
    public string TrayIconPathLight { get; set; } = "";
    public string TrayIconPathDark { get; set; } = "";
    public bool MonochromeTrayIcon { get; set; } = false;
    // [Placement]
    public bool PointerRelative { get; set; } = true;
    public string Horizontal { get; set; } = "left";
    public int HOffset { get; set; } = 13;
    public string Vertical { get; set; } = "top";
    public int VOffset { get; set; } = 13;
    public bool IgnoreHOffsetWhenRelative { get; set; } = true;
    public bool IgnoreVOffsetWhenRelative { get; set; } = true;

    public string Corners { get; set; } = "rounded";

    // [Appearance] TouchMode, with old WinUISize/[WinUI3] Size fallback
    public string WinUI3Size { get; set; } = "default";
    public bool WinUIAlwaysShowIcons { get; set; } = false; // inverse of [Appearance] ShowThumbnails

    public string DefaultIconPathLight { get; set; } = "";
    public string DefaultIconPathDark { get; set; } = "";
    // [Controls]
    public bool WindowsKey     { get; set; } = false;
    public bool ShiftWindowsKey { get; set; } = false;
    public bool WindowsKeyX    { get; set; } = false;
    public bool LeftClick      { get; set; } = false;
    public bool RightClick     { get; set; } = false;
    public bool MiddleClick    { get; set; } = true;
    public bool ShiftLeftClick  { get; set; } = false;
    public bool ShiftRightClick { get; set; } = false;
    public bool ShiftMiddleClick{ get; set; } = false;
    public SettingsItemMode ShowSettingsItem { get; set; } = SettingsItemMode.Disabled;
    public bool IgnoreTriggersWhenFullscreen { get; set; } = false;
    public string FullscreenExclusionList { get; set; } = "";

    // [Sorting]
    public string SortBy { get; set; } = "name";
    public string SortDirection { get; set; } = "ascending";
    public bool FoldersFirst { get; set; } = true;

    // [RecentItems]
    public int RecentMax { get; set; } = 12;
    public string RecentLabel { get; set; } = "fullpath";
    public bool RecentShowCleanItems { get; set; } = true;
    public bool RecentHideProtocolShortcuts { get; set; } = true;
    public bool RecentSeparateItems { get; set; } = false;

    // [TaskKill]
    public int TaskKillMax { get; set; } = 24;
    public bool TaskKillIgnoreSystem { get; set; } = true;
    public bool TaskKillListWindows { get; set; } = false;
    public bool TaskKillAllDesktops { get; set; } = true;
    public bool TaskKillShowProcessIds { get; set; } = true;
    public string TaskKillExcludes { get; set; } = "";

    // [ThisPC]
    public bool ThisPCAsSubmenu { get; set; } = true;
    public bool ThisPCItemsAsSubmenus { get; set; } = false;

    // [Home]
    public bool HomeAsSubmenu { get; set; } = true;
    public bool HomeItemsAsSubmenus { get; set; } = false;

    // [Power]
    public bool PowerSleep { get; set; } = true;
    public bool PowerHibernate { get; set; } = true;
    public bool PowerShutdown { get; set; } = true;
    public bool PowerRestart { get; set; } = true;
    public bool PowerLock { get; set; } = true;
    public bool PowerLogoff { get; set; } = true;
    public bool PowerAskForConfirmation { get; set; } = false;

    // Menu items (up to 64)
    public List<ConfigItem> Items { get; set; } = new();
}
