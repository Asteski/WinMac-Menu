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
            target.Add(CreateFlyoutItem(item, inSubmenu));
    }

    private MenuFlyoutItemBase CreateFlyoutItem(ConfigItem item, bool inSubmenu = false)
    {
        if (item.IsSeparator)
            return new MenuFlyoutSeparator();

        if (item.IsCategory)
        {
            return new MenuFlyoutItem { Text = item.Label, IsEnabled = false };
        }

        // ── Submenus ──────────────────────────────────────────────────────

        if (item.Type is ConfigItemType.PowerMenu)
        {
            var sub = new MenuFlyoutSubItem { Text = item.Label };
            ApplyIcon(sub, item, inSubmenu);
            foreach (var pi in BuildPowerItems()) sub.Items.Add(pi);
            return sub;
        }

        if (item.Type is ConfigItemType.TaskKill)
        {
            var sub = new MenuFlyoutSubItem { Text = item.Label };
            ApplyIcon(sub, item, inSubmenu);
            foreach (var p in CommandExecutor.GetRunningProcesses(_config))
            {
                var pi  = new MenuFlyoutItem { Text = $"{p.Name}  (PID {p.Pid})" };
                var pid = p.Pid;
                pi.Click += (_, _) => CommandExecutor.KillProcess(pid);
                // icons for processes: respect ShowFileIcons
                if (_config.ShowFileIcons)
                {
                    try
                    {
                        var exePath = System.Diagnostics.Process.GetProcessById(pid).MainModule?.FileName;
                        if (exePath != null)
                            pi.Icon = IconLoader.Load(exePath);
                    }
                    catch { }
                }
                sub.Items.Add(pi);
            }
            return sub;
        }

        if (item.Type is ConfigItemType.FolderSubmenu ||
            (item.Type is ConfigItemType.Folder && item.Submenu))
        {
            var sub = new MenuFlyoutSubItem { Text = item.Label };
            ApplyIcon(sub, item, inSubmenu);
            foreach (var entry in CommandExecutor.GetFolderContents(item.Path, _config))
            {
                var e  = entry;
                var fi = new MenuFlyoutItem { Text = e.Name };
                fi.Click += (_, _) => CommandExecutor.ShellOpen(e.FullPath);
                if (e.IsDirectory ? _config.ShowFolderIcons : _config.ShowFileIcons)
                    fi.Icon = IconLoader.Load(e.FullPath);
                sub.Items.Add(fi);
            }
            return sub;
        }

        // ── Regular item ─────────────────────────────────────────────────

        var flyoutItem = new MenuFlyoutItem { Text = item.Label };
        ApplyIcon(flyoutItem, item, inSubmenu);
        var captured = item;
        flyoutItem.Click += (_, _) => { this.Close(); CommandExecutor.Execute(captured); };
        return flyoutItem;
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

        // 1×1 px window at the cursor — effectively invisible, just an anchor for the flyout
        GetCursorPos(out var cursor);
        appWindow.MoveAndResize(new RectInt32(cursor.x, cursor.y, 1, 1));

        // Bring to front so the flyout receives input
        SetForegroundWindow(hwnd);
    }

    private AppWindow GetAppWindowForCurrentWindow()
    {
        var hwnd  = WinRT.Interop.WindowNative.GetWindowHandle(this);
        var wndId = Win32Interop.GetWindowIdFromWindow(hwnd);
        return AppWindow.GetFromWindowId(wndId);
    }
}
