namespace WinMacMenu.Models;

/// <summary>
/// Item kinds, mirroring <c>ConfigItemType</c> in the Win32 app's config.h.
/// The string token (URI, FILE, POWER_SLEEP, ...) is parsed in <see cref="Config.ConfigLoader"/>.
/// </summary>
public enum ConfigItemType
{
    Separator,
    Category,
    Uri,
    File,
    Cmd,
    Folder,
    FolderSubmenu,
    PowerSleep,
    PowerShutdown,
    PowerRestart,
    PowerLock,
    PowerLogoff,
    PowerHibernate,
    RecentSubmenu,
    PowerMenu,
    TaskKill,
    ThisPc,
    Home,
}

/// <summary>One [Menu] entry: <c>ItemN=Label|TYPE|Path|Params|Icon</c>.</summary>
public sealed class ConfigItem
{
    public string Label { get; set; } = string.Empty;
    public ConfigItemType Type { get; set; } = ConfigItemType.Separator;
    public string Path { get; set; } = string.Empty;
    public string Params { get; set; } = string.Empty;

    public string IconPath { get; set; } = string.Empty;
    public string IconPathLight { get; set; } = string.Empty;
    public string IconPathDark { get; set; } = string.Empty;

    /// <summary>Render this item as a submenu (folder/recent/thispc/home in submenu mode).</summary>
    public bool Submenu { get; set; }

    /// <summary>Expand the folder's contents directly into the parent menu instead of a submenu.</summary>
    public bool InlineExpand { get; set; }

    /// <summary>When inline-expanding, suppress the header label.</summary>
    public bool InlineNoHeader { get; set; }

    /// <summary>When inline-expanding, make the header clickable to open the folder.</summary>
    public bool InlineOpen { get; set; }
}
