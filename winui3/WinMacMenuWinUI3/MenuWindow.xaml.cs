using System.Collections.ObjectModel;
using System.Runtime.InteropServices;
using Microsoft.UI;
using Microsoft.UI.Windowing;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Windows.Graphics;
using WinMacMenuWinUI3.Models;
using WinMacMenuWinUI3.Services;

namespace WinMacMenuWinUI3;

public sealed partial class MenuWindow : Window
{
    private const int GWL_EXSTYLE     = -20;
    private const int WS_EX_TOOLWINDOW = 0x00000080;
    private const int WS_EX_TOPMOST    = 0x00000008;

    [DllImport("user32.dll")] private static extern int  GetWindowLong(IntPtr hWnd, int nIndex);
    [DllImport("user32.dll")] private static extern int  SetWindowLong(IntPtr hWnd, int nIndex, int dwNewLong);

    [StructLayout(LayoutKind.Sequential)]
    private struct POINT { public int x; public int y; }
    [DllImport("user32.dll")] private static extern bool GetCursorPos(out POINT lpPoint);

    private readonly AppConfig _config;
    private readonly string _iniPath;
    private bool _closeOnDeactivate = false;

    public ObservableCollection<ConfigItem> MenuItems { get; } = new();

    public MenuWindow(AppConfig config, string iniPath)
    {
        _config = config;
        _iniPath = iniPath;

        InitializeComponent();

        foreach (var item in config.Items)
            MenuItems.Add(item);

        ConfigureWindowChrome();
        PositionAtCursor();

        // Wait until the window has fully appeared before allowing deactivation to close it.
        // Without the delay, the Deactivated event fires before the window is even visible.
        this.Activated += OnActivated;
    }

    private async void OnActivated(object sender, WindowActivatedEventArgs e)
    {
        if (e.WindowActivationState != WindowActivationState.Deactivated)
        {
            // Window just became active — arm the close-on-deactivate after a short delay
            if (!_closeOnDeactivate)
            {
                await Task.Delay(300);
                _closeOnDeactivate = true;
            }
        }
        else if (_closeOnDeactivate)
        {
            this.Close();
        }
    }

    private void MenuItemsRepeater_ElementPrepared(ItemsRepeater sender, ItemsRepeaterElementPreparedEventArgs args)
    {
        if (args.Element is MenuItemControl ctrl)
        {
            ctrl.ItemClicked -= OnMenuItemClicked;
            ctrl.ItemClicked += OnMenuItemClicked;
        }
    }

    private void OnMenuItemClicked(object? sender, ConfigItem item)
        => OnItemClicked(item);

    private void ConfigureWindowChrome()
    {
        ExtendsContentIntoTitleBar = true;
        SetTitleBar(null);

        var hwnd = WinRT.Interop.WindowNative.GetWindowHandle(this);
        var appWindow = GetAppWindowForCurrentWindow();
        appWindow.IsShownInSwitchers = false;

        // Tool window (no taskbar/alt-tab entry) + always on top
        var exStyle = GetWindowLong(hwnd, GWL_EXSTYLE);
        exStyle |= WS_EX_TOOLWINDOW | WS_EX_TOPMOST;
        SetWindowLong(hwnd, GWL_EXSTYLE, exStyle);

        var presenter = OverlappedPresenter.CreateForToolWindow();
        presenter.IsResizable = false;
        presenter.IsMaximizable = false;
        presenter.IsMinimizable = false;
        presenter.SetBorderAndTitleBar(false, false);
        appWindow.SetPresenter(presenter);

        try { SystemBackdrop = new MicaBackdrop(); }
        catch { SystemBackdrop = new DesktopAcrylicBackdrop(); }
    }

    private void PositionAtCursor()
    {
        var appWindow = GetAppWindowForCurrentWindow();

        int itemCount = _config.Items.Count(i => !i.IsSeparator);
        int sepCount  = _config.Items.Count(i => i.IsSeparator);
        int height = Math.Max(itemCount * 36 + sepCount * 9 + 8, 50);
        int width  = 240;

        PointInt32 pos;
        if (_config.PointerRelative || true) // always use cursor position for now
        {
            GetCursorPos(out var cursor);
            int x = cursor.x + _config.HOffset;
            int y = cursor.y + _config.VOffset;

            var display = DisplayArea.GetFromPoint(new PointInt32(cursor.x, cursor.y), DisplayAreaFallback.Nearest);
            var work = display.WorkArea;

            if (x + width  > work.X + work.Width)  x = cursor.x - width;
            if (y + height > work.Y + work.Height)  y = cursor.y - height;
            if (x < work.X) x = work.X;
            if (y < work.Y) y = work.Y;

            pos = new PointInt32(x, y);
        }
        else
        {
            var display = DisplayArea.Primary;
            var work = display.WorkArea;

            int x = _config.Horizontal switch
            {
                "center" => work.X + (work.Width  - width)  / 2,
                "right"  => work.X +  work.Width  - width  + _config.HOffset,
                _        => work.X + _config.HOffset,
            };
            int y = _config.Vertical switch
            {
                "center" => work.Y + (work.Height - height) / 2,
                "bottom" => work.Y +  work.Height - height + _config.VOffset,
                _        => work.Y + _config.VOffset,
            };

            pos = new PointInt32(x, y);
        }

        appWindow.MoveAndResize(new RectInt32(pos.X, pos.Y, width, height));
    }

    private AppWindow GetAppWindowForCurrentWindow()
    {
        var hwnd = WinRT.Interop.WindowNative.GetWindowHandle(this);
        var wndId = Win32Interop.GetWindowIdFromWindow(hwnd);
        return AppWindow.GetFromWindowId(wndId);
    }

    internal void OnItemClicked(ConfigItem item)
    {
        if (item.Type == ConfigItemType.TaskKill)
        {
            ShowTaskKillSubmenu();
            return;
        }
        if (item.Type == ConfigItemType.PowerMenu)
        {
            ShowPowerMenuSubmenu();
            return;
        }

        this.Close();
        CommandExecutor.Execute(item);
    }

    private void ShowTaskKillSubmenu()
    {
        var processes = CommandExecutor.GetRunningProcesses(_config);
        MenuItems.Clear();
        MenuItems.Add(new ConfigItem { Label = "← Back", Type = ConfigItemType.Category });
        MenuItems.Add(new ConfigItem { Type = ConfigItemType.Separator });
        foreach (var p in processes)
            MenuItems.Add(new ConfigItem { Label = $"{p.Name} (PID {p.Pid})", Type = ConfigItemType.Cmd, Path = p.Pid.ToString() });
    }

    private void ShowPowerMenuSubmenu()
    {
        MenuItems.Clear();
        MenuItems.Add(new ConfigItem { Label = "← Back", Type = ConfigItemType.Category });
        MenuItems.Add(new ConfigItem { Type = ConfigItemType.Separator });
        if (_config.PowerSleep)     MenuItems.Add(new ConfigItem { Label = "Sleep",     Type = ConfigItemType.PowerSleep });
        if (_config.PowerHibernate) MenuItems.Add(new ConfigItem { Label = "Hibernate", Type = ConfigItemType.PowerHibernate });
        if (_config.PowerShutdown)  MenuItems.Add(new ConfigItem { Label = "Shut Down", Type = ConfigItemType.PowerShutdown });
        if (_config.PowerRestart)   MenuItems.Add(new ConfigItem { Label = "Restart",   Type = ConfigItemType.PowerRestart });
        if (_config.PowerLock)      MenuItems.Add(new ConfigItem { Label = "Lock",      Type = ConfigItemType.PowerLock });
        if (_config.PowerLogoff)    MenuItems.Add(new ConfigItem { Label = "Sign Out",  Type = ConfigItemType.PowerLogoff });
    }
}
