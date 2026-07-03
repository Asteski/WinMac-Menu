using System.Collections.ObjectModel;
using System.Runtime.InteropServices;
using Microsoft.UI;
using Microsoft.UI.Windowing;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Windows.Foundation;
using Windows.Graphics;
using WinMacMenuWinUI3.Models;
using WinMacMenuWinUI3.Services;

namespace WinMacMenuWinUI3;

public sealed partial class MenuWindow : Window
{
    // Win32 window styles for a popup that doesn't appear in the taskbar or alt-tab
    private const int GWL_STYLE   = -16;
    private const int GWL_EXSTYLE = -20;
    private const int WS_POPUP           = unchecked((int)0x80000000);
    private const int WS_EX_TOOLWINDOW   = 0x00000080;
    private const int WS_EX_TOPMOST      = 0x00000008;
    private const int WS_EX_NOACTIVATE   = 0x08000000;

    [DllImport("user32.dll")] private static extern int  GetWindowLong(IntPtr hWnd, int nIndex);
    [DllImport("user32.dll")] private static extern int  SetWindowLong(IntPtr hWnd, int nIndex, int dwNewLong);
    [DllImport("user32.dll")] private static extern bool SetWindowPos(IntPtr hWnd, IntPtr hWndInsertAfter,
        int X, int Y, int cx, int cy, uint uFlags);
    [StructLayout(LayoutKind.Sequential)]
    private struct POINT { public int x; public int y; }

    [DllImport("user32.dll")] private static extern bool GetCursorPos(out POINT lpPoint);

    private readonly AppConfig _config;
    private readonly string _iniPath;
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

        // Close when focus is lost
        this.Activated += (_, e) =>
        {
            if (e.WindowActivationState == WindowActivationState.Deactivated)
                this.Close();
        };
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
        // Remove title bar, make it a floating tool window
        ExtendsContentIntoTitleBar = true;
        SetTitleBar(null);

        var hwnd = WinRT.Interop.WindowNative.GetWindowHandle(this);
        var appWindow = GetAppWindowForCurrentWindow();
        appWindow.IsShownInSwitchers = false;

        // Apply popup + toolwindow + topmost + noactivate styles
        var exStyle = GetWindowLong(hwnd, GWL_EXSTYLE);
        exStyle |= WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE;
        SetWindowLong(hwnd, GWL_EXSTYLE, exStyle);

        // Use overlapped presenter without decorations
        var presenter = OverlappedPresenter.CreateForToolWindow();
        presenter.IsResizable = false;
        presenter.IsMaximizable = false;
        presenter.IsMinimizable = false;
        presenter.SetBorderAndTitleBar(false, false);
        appWindow.SetPresenter(presenter);

        // Apply Mica backdrop for Windows 11 look; falls back to acrylic on older builds
        try { SystemBackdrop = new MicaBackdrop(); }
        catch { SystemBackdrop = new DesktopAcrylicBackdrop(); }
    }

    private void PositionAtCursor()
    {
        var hwnd = WinRT.Interop.WindowNative.GetWindowHandle(this);
        var appWindow = GetAppWindowForCurrentWindow();

        // Measure desired size (approximation: 220px wide, 40px per item + 8px padding)
        int itemCount = _config.Items.Count(i => !i.IsSeparator);
        int sepCount  = _config.Items.Count(i => i.IsSeparator);
        int height = itemCount * 36 + sepCount * 9 + 8;
        int width  = 240;

        PointInt32 pos;
        if (_config.PointerRelative)
        {
            GetCursorPos(out var cursor);
            int x = cursor.x + _config.HOffset;
            int y = cursor.y + _config.VOffset;

            // Keep on screen
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

    // Called by MenuItemControl when an item is clicked
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
        var submenuItems = processes.Select(p => new ConfigItem
        {
            Label = $"{p.Name} (PID {p.Pid})",
            Type  = ConfigItemType.Cmd,
            Path  = p.Pid.ToString(),
        }).ToList();

        // Replace menu contents with process list + back button
        MenuItems.Clear();
        MenuItems.Add(new ConfigItem { Label = "← Back", Type = ConfigItemType.Category });
        MenuItems.Add(new ConfigItem { Type = ConfigItemType.Separator });
        foreach (var pi in submenuItems)
            MenuItems.Add(pi);
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
