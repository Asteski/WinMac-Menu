using System.Runtime.InteropServices;
using Microsoft.UI;
using Microsoft.UI.Windowing;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Windows.Foundation;
using Microsoft.UI.Xaml.Controls.Primitives;
using Windows.Graphics;
using WinMacMenuWinUI3.Models;
using WinMacMenuWinUI3.Services;

namespace WinMacMenuWinUI3;

public sealed partial class MenuWindow : Window
{
    private const int GWL_EXSTYLE      = -20;
    private const int WS_EX_TOOLWINDOW = 0x00000080;
    private const int WS_EX_TOPMOST    = 0x00000008;

    [DllImport("user32.dll")] private static extern int  GetWindowLong(IntPtr hWnd, int nIndex);
    [DllImport("user32.dll")] private static extern int  SetWindowLong(IntPtr hWnd, int nIndex, int dwNewLong);

    [StructLayout(LayoutKind.Sequential)]
    private struct POINT { public int x; public int y; }
    [DllImport("user32.dll")] private static extern bool GetCursorPos(out POINT lpPoint);
    [DllImport("user32.dll")] private static extern bool SetForegroundWindow(IntPtr hWnd);

    private readonly AppConfig _config;
    private readonly string _iniPath;

    public MenuWindow(AppConfig config, string iniPath)
    {
        _config = config;
        _iniPath = iniPath;

        InitializeComponent();

        ConfigureWindowChrome();

        // Build and show the flyout once the window content is loaded
        RootGrid.Loaded += (_, _) => ShowFlyout();
    }

    private void ShowFlyout()
    {
        var flyout = new MenuFlyout
        {
            Placement = FlyoutPlacementMode.BottomEdgeAlignedLeft
        };

        BuildItems(flyout.Items, _config.Items);

        flyout.Closed += (_, _) => this.Close();

        // Window is 1×1 px sitting at the cursor — flyout opens from (0,0)
        // which is exactly the cursor position on screen.
        flyout.ShowAt(RootGrid, new Point(0, 0));
    }

    // inSubmenu = true  →  respect ShowFolderIcons / ShowFileIcons
    // inSubmenu = false →  respect ShowIcons (root menu)
    private void BuildItems(IList<MenuFlyoutItemBase> target, IEnumerable<ConfigItem> items,
                            bool inSubmenu = false)
    {
        foreach (var item in items)
        {
            try { target.Add(CreateFlyoutItem(item, inSubmenu)); }
            catch { /* skip broken items so the rest of the menu still shows */ }
        }
    }

    private MenuFlyoutItemBase CreateFlyoutItem(ConfigItem item, bool inSubmenu = false)
    {
        if (item.IsSeparator)
            return new MenuFlyoutSeparator();

        if (item.IsCategory)
            return new MenuFlyoutItem { Text = item.Label, IsEnabled = false };

        switch (item.Type)
        {
            // ── Power menu ───────────────────────────────────────────────
            case ConfigItemType.PowerMenu:
            {
                var sub = new MenuFlyoutSubItem { Text = item.Label };
                ApplyIcon(sub, item, inSubmenu);
                foreach (var pi in BuildPowerItems()) sub.Items.Add(pi);
                return sub;
            }

            // ── Task kill ────────────────────────────────────────────────
            case ConfigItemType.TaskKill:
            {
                var sub = new MenuFlyoutSubItem { Text = item.Label };
                ApplyIcon(sub, item, inSubmenu);
                foreach (var p in CommandExecutor.GetRunningProcesses(_config))
                {
                    var pi  = new MenuFlyoutItem { Text = $"{p.Name}  (PID {p.Pid})" };
                    var pid = p.Pid;
                    pi.Click += (_, _) => CommandExecutor.KillProcess(pid);
                    if (_config.TaskKillShowIcons)
                    {
                        try
                        {
                            // MainModule throws for system/elevated processes — catch per-item
                            using var proc = System.Diagnostics.Process.GetProcessById(pid);
                            var exePath = proc.MainModule?.FileName;
                            if (exePath != null) pi.Icon = IconLoader.Load(exePath);
                        }
                        catch { }
                    }
                    sub.Items.Add(pi);
                }
                return sub;
            }

            // ── Folder submenu ───────────────────────────────────────────
            case ConfigItemType.FolderSubmenu:
            case ConfigItemType.Folder when item.Submenu:
            {
                var sub = new MenuFlyoutSubItem { Text = item.Label };
                ApplyIcon(sub, item, inSubmenu);
                AddFolderEntries(sub.Items, item.Path);
                return sub;
            }

            // ── Recent items ─────────────────────────────────────────────
            case ConfigItemType.Recent:
            case ConfigItemType.RecentSubmenu:
            {
                var sub = new MenuFlyoutSubItem { Text = item.Label };
                ApplyIcon(sub, item, inSubmenu);
                AddRecentItems(sub.Items);
                return sub;
            }

            // ── This PC (drives) ─────────────────────────────────────────
            case ConfigItemType.ThisPC:
            {
                if (!_config.ThisPCAsSubmenu)
                {
                    // Plain clickable item — opens File Explorer at This PC
                    var fi = new MenuFlyoutItem { Text = item.Label };
                    ApplyIcon(fi, item, inSubmenu);
                    fi.Click += (_, _) => { this.Close(); CommandExecutor.ShellOpen("shell:MyComputerFolder"); };
                    return fi;
                }
                var sub = new MenuFlyoutSubItem { Text = item.Label };
                ApplyIcon(sub, item, inSubmenu);
                AddThisPCItems(sub.Items);
                return sub;
            }

            // ── Home folder ──────────────────────────────────────────────
            case ConfigItemType.Home:
            {
                var homePath = Environment.GetFolderPath(Environment.SpecialFolder.UserProfile);
                if (!_config.HomeAsSubmenu)
                {
                    var fi = new MenuFlyoutItem { Text = item.Label };
                    ApplyIcon(fi, item, inSubmenu);
                    fi.Click += (_, _) => { this.Close(); CommandExecutor.ShellOpen(homePath); };
                    return fi;
                }
                var sub = new MenuFlyoutSubItem { Text = item.Label };
                ApplyIcon(sub, item, inSubmenu);
                AddHomeFolderItems(sub.Items, homePath);
                return sub;
            }

            // ── Regular item ─────────────────────────────────────────────
            default:
            {
                var fi = new MenuFlyoutItem { Text = item.Label };
                ApplyIcon(fi, item, inSubmenu);
                var captured = item;
                fi.Click += (_, _) => { this.Close(); CommandExecutor.Execute(captured); };
                return fi;
            }
        }
    }

    private void AddFolderEntries(IList<MenuFlyoutItemBase> target, string folderPath)
    {
        foreach (var entry in CommandExecutor.GetFolderContents(folderPath, _config))
        {
            var e  = entry;
            var fi = new MenuFlyoutItem { Text = e.Name };
            fi.Click += (_, _) => CommandExecutor.ShellOpen(e.FullPath);
            if (e.IsDirectory ? _config.ShowFolderIcons : _config.ShowFileIcons)
                fi.Icon = IconLoader.Load(e.FullPath);
            target.Add(fi);
        }
    }

    private static string? ResolveLnkTarget(string lnkPath)
    {
        try
        {
            var shellType = Type.GetTypeFromProgID("WScript.Shell");
            if (shellType == null) return null;
            dynamic shell    = Activator.CreateInstance(shellType)!;
            dynamic shortcut = shell.CreateShortcut(lnkPath);
            string  target   = shortcut.TargetPath;
            return string.IsNullOrEmpty(target) ? null : target;
        }
        catch { return null; }
    }

    private void AddRecentItems(IList<MenuFlyoutItemBase> target)
    {
        try
        {
            var recentDir = Environment.GetFolderPath(Environment.SpecialFolder.Recent);
            var lnkFiles  = Directory.GetFiles(recentDir, "*.lnk")
                                     .OrderByDescending(File.GetLastWriteTime)
                                     .Take(_config.RecentMax);

            foreach (var lnk in lnkFiles)
            {
                var targetPath = ResolveLnkTarget(lnk);

                string displayName;
                if (_config.RecentLabel == "fullpath")
                {
                    // Show resolved target path; fall back to .lnk name if unresolvable
                    displayName = targetPath ?? Path.GetFileNameWithoutExtension(lnk);
                }
                else
                {
                    // filename only from resolved target, or from .lnk name as fallback
                    var baseName = targetPath != null
                        ? Path.GetFileName(targetPath)
                        : Path.GetFileNameWithoutExtension(lnk);

                    displayName = _config.RecentShowExtensions
                        ? baseName
                        : Path.GetFileNameWithoutExtension(baseName);
                }

                var fi   = new MenuFlyoutItem { Text = displayName };
                var path = targetPath ?? lnk;  // open the real file if resolved
                fi.Click += (_, _) => CommandExecutor.ShellOpen(path);
                if (_config.RecentShowIcons) fi.Icon = IconLoader.Load(targetPath ?? lnk);
                target.Add(fi);
            }

            if (_config.RecentShowCleanItems && target.Count > 0)
            {
                target.Add(new MenuFlyoutSeparator());
                var clear = new MenuFlyoutItem { Text = "Clear recent items" };
                clear.Click += (_, _) =>
                {
                    foreach (var f in Directory.GetFiles(recentDir))
                        try { File.Delete(f); } catch { }
                };
                target.Add(clear);
            }
        }
        catch { }
    }

    private void AddThisPCItems(IList<MenuFlyoutItemBase> target)
    {
        foreach (var drive in DriveInfo.GetDrives().Where(d => d.IsReady))
        {
            var label = string.IsNullOrEmpty(drive.VolumeLabel)
                ? drive.Name
                : $"{drive.VolumeLabel} ({drive.Name.TrimEnd('\\')})";

            var root = drive.RootDirectory.FullName;

            if (_config.ThisPCItemsAsSubmenus)
            {
                var sub = new MenuFlyoutSubItem { Text = label };
                if (_config.ThisPCShowIcons) sub.Icon = IconLoader.Load(root);
                AddFolderEntries(sub.Items, root);
                target.Add(sub);
            }
            else
            {
                var fi = new MenuFlyoutItem { Text = label };
                if (_config.ThisPCShowIcons) fi.Icon = IconLoader.Load(root);
                fi.Click += (_, _) => CommandExecutor.ShellOpen(root);
                target.Add(fi);
            }
        }
    }

    private void AddHomeFolderItems(IList<MenuFlyoutItemBase> target, string homePath)
    {
        foreach (var entry in CommandExecutor.GetFolderContents(homePath, _config))
        {
            var e = entry;

            if (_config.HomeItemsAsSubmenus && e.IsDirectory)
            {
                var sub = new MenuFlyoutSubItem { Text = e.Name };
                if (_config.HomeShowIcons) sub.Icon = IconLoader.Load(e.FullPath);
                AddFolderEntries(sub.Items, e.FullPath);
                target.Add(sub);
            }
            else
            {
                var fi = new MenuFlyoutItem { Text = e.Name };
                if (_config.HomeShowIcons) fi.Icon = IconLoader.Load(e.FullPath);
                fi.Click += (_, _) => CommandExecutor.ShellOpen(e.FullPath);
                target.Add(fi);
            }
        }
    }

    /// <summary>
    /// Resolves the best icon for this item and applies it, honouring the
    /// ShowIcons (root) / ShowFolderIcons+ShowFileIcons (submenu) settings.
    /// Priority: per-item path (theme-aware) → item generic → config default.
    /// </summary>
    private void ApplyIcon(MenuFlyoutItemBase target, ConfigItem item, bool inSubmenu)
    {
        bool showIcons = inSubmenu
            ? (_config.ShowFolderIcons || _config.ShowFileIcons)
            : _config.ShowIcons;

        if (!showIcons) return;

        // Pick theme-aware path if available, fall back to generic then default
        var isDark   = Application.Current.RequestedTheme == ApplicationTheme.Dark;
        var iconPath = isDark && !string.IsNullOrEmpty(item.IconPathDark)  ? item.IconPathDark
                     : !isDark && !string.IsNullOrEmpty(item.IconPathLight) ? item.IconPathLight
                     : !string.IsNullOrEmpty(item.IconPath)                 ? item.IconPath
                     : _config.DefaultIconPath;

        if (string.IsNullOrEmpty(iconPath)) return;

        var icon = IconLoader.Load(iconPath);
        if (icon == null) return;

        if (target is MenuFlyoutItem fi)        fi.Icon  = icon;
        else if (target is MenuFlyoutSubItem si) si.Icon  = icon;
    }

    private List<MenuFlyoutItemBase> BuildPowerItems()
    {
        var list = new List<MenuFlyoutItemBase>();
        void Add(string label, ConfigItemType type)
        {
            var ci = new ConfigItem { Label = label, Type = type };
            var fi = new MenuFlyoutItem { Text = label };
            fi.Click += (_, _) => { this.Close(); CommandExecutor.Execute(ci); };
            list.Add(fi);
        }
        if (_config.PowerSleep)     Add("Sleep",     ConfigItemType.PowerSleep);
        if (_config.PowerHibernate) Add("Hibernate", ConfigItemType.PowerHibernate);
        if (_config.PowerShutdown)  Add("Shut Down", ConfigItemType.PowerShutdown);
        if (_config.PowerRestart)   Add("Restart",   ConfigItemType.PowerRestart);
        if (_config.PowerLock)      Add("Lock",      ConfigItemType.PowerLock);
        if (_config.PowerLogoff)    Add("Sign Out",  ConfigItemType.PowerLogoff);
        return list;
    }

    private void ConfigureWindowChrome()
    {
        ExtendsContentIntoTitleBar = true;
        SetTitleBar(null);

        var hwnd      = WinRT.Interop.WindowNative.GetWindowHandle(this);
        var appWindow = GetAppWindowForCurrentWindow();
        appWindow.IsShownInSwitchers = false;

        var exStyle = GetWindowLong(hwnd, GWL_EXSTYLE);
        exStyle |= WS_EX_TOOLWINDOW | WS_EX_TOPMOST;
        SetWindowLong(hwnd, GWL_EXSTYLE, exStyle);

        var presenter = OverlappedPresenter.CreateForToolWindow();
        presenter.IsResizable    = false;
        presenter.IsMaximizable  = false;
        presenter.IsMinimizable  = false;
        presenter.SetBorderAndTitleBar(false, false);
        appWindow.SetPresenter(presenter);

        // Position the 1×1 anchor window — at cursor or at configured fixed position
        var anchorPt = _config.PointerRelative
            ? GetCursorAnchor()
            : GetFixedAnchor();

        appWindow.MoveAndResize(new RectInt32(anchorPt.X, anchorPt.Y, 1, 1));

        // Bring to front so the flyout receives input
        SetForegroundWindow(hwnd);
    }

    [DllImport("user32.dll")] private static extern uint GetDpiForWindow(IntPtr hWnd);

    // Converts physical screen pixels → logical pixels WinUI3 uses for window placement.
    // At 100% scaling these are identical; at 150% a physical 1500px becomes 1000 logical px.
    private PointInt32 ToLogical(int physX, int physY)
    {
        var hwnd = WinRT.Interop.WindowNative.GetWindowHandle(this);
        var dpi  = (int)GetDpiForWindow(hwnd);
        if (dpi <= 0) dpi = 96;
        return new PointInt32(physX * 96 / dpi, physY * 96 / dpi);
    }

    // Anchor = cursor position (pointer-relative mode)
    private PointInt32 GetCursorAnchor()
    {
        GetCursorPos(out var pt);
        // Snap to nearest integer logical pixel to avoid sub-pixel layout jitter
        return ToLogical(pt.x, pt.y);
    }

    // Anchor = fixed point derived from Horizontal/Vertical alignment + offsets
    private PointInt32 GetFixedAnchor()
    {
        // Use the display that contains the primary taskbar
        var display = DisplayArea.Primary;
        var work    = display.WorkArea;

        // Horizontal: left/center/right edge of work area + HOffset
        int x = _config.Horizontal.ToLowerInvariant() switch
        {
            "right"  => work.X + work.Width  + _config.HOffset,
            "center" => work.X + work.Width  / 2 + _config.HOffset,
            _        => work.X + _config.HOffset,           // "left" (default)
        };

        // Vertical: top/center/bottom edge of work area + VOffset
        int y = _config.Vertical.ToLowerInvariant() switch
        {
            "bottom" => work.Y + work.Height + _config.VOffset,
            "center" => work.Y + work.Height / 2 + _config.VOffset,
            _        => work.Y + _config.VOffset,           // "top" (default)
        };

        // Clamp to work area so the anchor is always on-screen
        x = Math.Clamp(x, work.X, work.X + work.Width  - 1);
        y = Math.Clamp(y, work.Y, work.Y + work.Height - 1);

        return new PointInt32(x, y);
    }

    private AppWindow GetAppWindowForCurrentWindow()
    {
        var hwnd  = WinRT.Interop.WindowNative.GetWindowHandle(this);
        var wndId = Win32Interop.GetWindowIdFromWindow(hwnd);
        return AppWindow.GetFromWindowId(wndId);
    }
}
