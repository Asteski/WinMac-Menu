namespace WinMacMenu.Models;

public enum SortField { Name, DateModified, DateCreated, Type, Size }

/// <summary>0 = disabled, 1 = folders first, 2 = files first.</summary>
public enum SortObjectPriority { Disabled = 0, FoldersFirst = 1, FilesFirst = 2 }

public enum HorizontalPlacement { Left = 0, Center = 1, Right = 2 }

public enum VerticalPlacement { Top = 0, Center = 1, Bottom = 2 }

/// <summary>auto/disabled/top/bottom/left/right entrance animation hint.</summary>
public enum AnimationDirection { Auto = 0, Top = 1, Bottom = 2, Left = 3, Right = 4 }

public enum ControlAction { Nothing = 0, WinMacMenu = 1, WindowsMenu = 2, CustomCommand = 3 }

/// <summary>
/// Full configuration model, mirroring the Win32 <c>Config</c> struct in config.h.
/// Defaults match the C parser so behaviour is identical when keys are absent.
/// </summary>
public sealed class Config
{
    public string IniPath { get; set; } = string.Empty;

    // [General]
    public bool RunInBackground { get; set; } = true;
    public bool ShowOnLaunch { get; set; } = true;
    public bool ShowTrayIcon { get; set; } = true;
    public bool StartOnLogin { get; set; }
    public bool MonochromeTrayIcon { get; set; } = true;
    public int FolderMaxDepth { get; set; } = 4;
    public bool FolderSingleClickOpen { get; set; } = true;
    public bool FolderShowOpenEntry { get; set; } = true;
    public bool ShowHidden { get; set; }
    public bool ShowDotfiles { get; set; }
    /// <summary>0 = none, 1 = files only, 2 = folders only, 3 = both.</summary>
    public int DotMode { get; set; }
    public int MaxItems { get; set; } = 40;
    public bool ShowExtensions { get; set; } = true;
    public bool ShowFolderIcons { get; set; } = true;
    public bool ShowFileIcons { get; set; } = true;
    public bool KeepMenuOpenAfterContextAction { get; set; }
    /// <summary>0 = off, 1 = show icons, 2 = other/mixed.</summary>
    public int ShowIcons { get; set; }

    public string DefaultIconPath { get; set; } = string.Empty;
    public string DefaultIconPathLight { get; set; } = string.Empty;
    public string DefaultIconPathDark { get; set; } = string.Empty;

    public string TrayIconPath { get; set; } = string.Empty;
    public string TrayIconPathLight { get; set; } = string.Empty;
    public string TrayIconPathDark { get; set; } = string.Empty;

    // [Sorting]
    public SortField SortField { get; set; } = SortField.Name;
    public bool SortDescending { get; set; }
    public SortObjectPriority SortObjectPriority { get; set; } = SortObjectPriority.FoldersFirst;

    // [Placement]
    public HorizontalPlacement HPlacement { get; set; } = HorizontalPlacement.Left;
    public int HOffset { get; set; }
    public VerticalPlacement VPlacement { get; set; } = VerticalPlacement.Top;
    public int VOffset { get; set; }
    public bool PointerRelative { get; set; } = true;
    public bool IgnoreHOffsetWhenCentered { get; set; }
    public bool IgnoreVOffsetWhenCentered { get; set; }
    public bool IgnoreHOffsetWhenRelative { get; set; }
    public bool IgnoreVOffsetWhenRelative { get; set; }

    // [Appearance]
    public AnimationDirection AnimationDirection { get; set; } = AnimationDirection.Bottom;

    // [Power] exclusions
    public bool ExcludeSleep { get; set; }
    public bool ExcludeShutdown { get; set; }
    public bool ExcludeRestart { get; set; }
    public bool ExcludeLock { get; set; }
    public bool ExcludeLogoff { get; set; }
    public bool ExcludeHibernate { get; set; }

    // [RecentItems]
    public int RecentMax { get; set; } = 12;
    /// <summary>0 = full path, 1 = file name.</summary>
    public int RecentLabelMode { get; set; }
    public bool RecentShowExtensions { get; set; } = true;
    public bool RecentShowCleanItems { get; set; } = true;
    public bool RecentShowIcons { get; set; }

    // [TaskKill]
    public int TaskKillMax { get; set; } = 10;
    public bool TaskKillIgnoreSystem { get; set; }
    public bool TaskKillShowIcons { get; set; } = true;
    public bool TaskKillListWindows { get; set; }
    public bool TaskKillAllDesktops { get; set; }
    public string TaskKillExcludes { get; set; } = string.Empty;

    // [ThisPC]
    public bool ThisPCItemsAsSubmenus { get; set; } = true;
    public bool ThisPCShowIcons { get; set; } = true;
    public bool ThisPCAsSubmenu { get; set; }

    // [Home]
    public bool HomeItemsAsSubmenus { get; set; } = true;
    public bool HomeShowIcons { get; set; } = true;
    public bool HomeAsSubmenu { get; set; }

    // [Controls] / [Control]
    public ControlAction LeftClickAction { get; set; } = ControlAction.WinMacMenu;
    public string LeftClickCommand { get; set; } = string.Empty;
    public ControlAction WindowsKeyAction { get; set; } = ControlAction.WinMacMenu;
    public string WindowsKeyCommand { get; set; } = string.Empty;
    public bool WindowsKeyTrigger { get; set; }
    public bool ShiftWindowsKeyTrigger { get; set; }
    public bool LeftClickTrigger { get; set; }
    public bool RightClickTrigger { get; set; }
    public bool MiddleClickTrigger { get; set; } = true;
    public bool ShiftLeftClickTrigger { get; set; }
    public bool ShiftRightClickTrigger { get; set; }
    public bool ShiftMiddleClickTrigger { get; set; }
    public bool IgnoreTriggersWhenFullscreen { get; set; }
    public string FullscreenExclusionList { get; set; } = string.Empty;

    // [Logging]
    /// <summary>0 = off, 1 = basic, 2 = verbose.</summary>
    public int LogLevel { get; set; }
    public string LogFolderPath { get; set; } = string.Empty;
    public string LogFilePath { get; set; } = string.Empty;

    public List<ConfigItem> Items { get; } = new();
}
