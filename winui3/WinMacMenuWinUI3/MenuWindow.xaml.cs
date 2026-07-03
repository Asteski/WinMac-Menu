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

    private void BuildItems(IList<MenuFlyoutItemBase> target, IEnumerable<ConfigItem> items)
    {
        foreach (var item in items)
        {
            target.Add(CreateFlyoutItem(item));
        }
    }

    private MenuFlyoutItemBase CreateFlyoutItem(ConfigItem item)
    {
        if (item.IsSeparator)
            return new MenuFlyoutSeparator();

        if (item.IsCategory)
        {
            // WinUI3 doesn't have a native flyout group header, so use a
            // disabled item styled as a label
            return new MenuFlyoutItem
            {
                Text      = item.Label,
                IsEnabled = false,
            };
        }

        // Items that expand into a submenu
        if (item.Type is ConfigItemType.PowerMenu)
        {
            var sub = new MenuFlyoutSubItem { Text = item.Label };
            var powerItems = BuildPowerItems();
            foreach (var pi in powerItems) sub.Items.Add(pi);
            return sub;
        }

        if (item.Type is ConfigItemType.TaskKill)
        {
            var sub = new MenuFlyoutSubItem { Text = item.Label };
            foreach (var p in CommandExecutor.GetRunningProcesses(_config))
            {
                var pi = new MenuFlyoutItem { Text = $"{p.Name}  (PID {p.Pid})" };
                var pid = p.Pid;
                pi.Click += (_, _) => CommandExecutor.KillProcess(pid);
                sub.Items.Add(pi);
            }
            return sub;
        }

        if (item.Type is ConfigItemType.FolderSubmenu ||
            (item.Type is ConfigItemType.Folder && item.Submenu))
        {
            var sub = new MenuFlyoutSubItem { Text = item.Label };
            foreach (var entry in CommandExecutor.GetFolderContents(item.Path, _config))
            {
                var e = entry;
                var fi = new MenuFlyoutItem { Text = e.Name };
                fi.Click += (_, _) => CommandExecutor.ShellOpen(e.FullPath);
                sub.Items.Add(fi);
            }
            return sub;
        }

        // Regular clickable item
        var flyoutItem = new MenuFlyoutItem { Text = item.Label };
        var captured   = item;
        flyoutItem.Click += (_, _) =>
        {
            this.Close();
            CommandExecutor.Execute(captured);
        };
        return flyoutItem;
    }

    private List<MenuFlyoutItemBase> BuildPowerItems()
    {
        var list = new List<MenuFlyoutItemBase>();
        void Add(string label, ConfigItem item)
        {
            var fi = new MenuFlyoutItem { Text = label };
            fi.Click += (_, _) => { this.Close(); CommandExecutor.Execute(item); };
            list.Add(fi);
        }

        if (_config.PowerSleep)     Add("Sleep",     new ConfigItem { Type = ConfigItemType.PowerSleep });
        if (_config.PowerHibernate) Add("Hibernate", new ConfigItem { Type = ConfigItemType.PowerHibernate });
        if (_config.PowerShutdown)  Add("Shut Down", new ConfigItem { Type = ConfigItemType.PowerShutdown });
        if (_config.PowerRestart)   Add("Restart",   new ConfigItem { Type = ConfigItemType.PowerRestart });
        if (_config.PowerLock)      Add("Lock",      new ConfigItem { Type = ConfigItemType.PowerLock });
        if (_config.PowerLogoff)    Add("Sign Out",  new ConfigItem { Type = ConfigItemType.PowerLogoff });
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
