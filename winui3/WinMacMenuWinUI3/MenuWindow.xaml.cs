using System.Collections.ObjectModel;
using System.Runtime.InteropServices;
using Microsoft.UI;
using Microsoft.UI.Windowing;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Windows.Graphics;
using WinMacMenuWinUI3.Models;
using WinMacMenuWinUI3.Services;
using WinMacMenuWinUI3.ViewModels;

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

    private readonly AppConfig _config;
    private readonly string _iniPath;
    private bool _closeOnDeactivate = false;

    public ObservableCollection<MenuItemViewModel> MenuItems { get; } = new();

    public MenuWindow(AppConfig config, string iniPath)
    {
        _config = config;
        _iniPath = iniPath;

        InitializeComponent();

        PopulateItems(config.Items);

        ConfigureWindowChrome();
        PositionAtCursor();

        Activated += OnActivated;
    }

    private void PopulateItems(IEnumerable<ConfigItem> items)
    {
        MenuItems.Clear();
        foreach (var item in items)
            MenuItems.Add(new MenuItemViewModel(item, OnItemClicked));
    }

    private async void OnActivated(object sender, WindowActivatedEventArgs e)
    {
        if (e.WindowActivationState != WindowActivationState.Deactivated)
        {
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

    private void ConfigureWindowChrome()
    {
        ExtendsContentIntoTitleBar = true;
        SetTitleBar(null);

        var hwnd = WinRT.Interop.WindowNative.GetWindowHandle(this);
        var appWindow = GetAppWindowForCurrentWindow();
        appWindow.IsShownInSwitchers = false;

        var exStyle = GetWindowLong(hwnd, GWL_EXSTYLE);
        exStyle |= WS_EX_TOOLWINDOW | WS_EX_TOPMOST;
        SetWindowLong(hwnd, GWL_EXSTYLE, exStyle);

        var presenter = OverlappedPresenter.CreateForToolWindow();
        presenter.IsResizable = false;
        presenter.IsMaximizable = false;
        presenter.IsMinimizable = false;
        presenter.SetBorderAndTitleBar(false, false);
        appWindow.SetPresenter(presenter);

        appWindow.TitleBar.BackgroundColor = Windows.UI.Color.FromArgb(0, 0, 0, 0);
        appWindow.TitleBar.InactiveBackgroundColor = Windows.UI.Color.FromArgb(0, 0, 0, 0);
    }

    private void PositionAtCursor()
    {
        var appWindow = GetAppWindowForCurrentWindow();

        int itemCount = _config.Items.Count(i => !i.IsSeparator && !i.IsCategory);
        int sepCount  = _config.Items.Count(i => i.IsSeparator);
        int catCount  = _config.Items.Count(i => i.IsCategory);
        int height    = Math.Max(itemCount * 34 + sepCount * 9 + catCount * 26 + 8, 50);
        int width     = 240;

        GetCursorPos(out var cursor);
        int x = cursor.x;
        int y = cursor.y;

        var display = DisplayArea.GetFromPoint(new PointInt32(cursor.x, cursor.y), DisplayAreaFallback.Nearest);
        var work    = display.WorkArea;

        if (x + width  > work.X + work.Width)  x = cursor.x - width;
        if (y + height > work.Y + work.Height)  y = cursor.y - height;
        if (x < work.X) x = work.X;
        if (y < work.Y) y = work.Y;

        appWindow.MoveAndResize(new RectInt32(x, y, width, height));
    }

    private AppWindow GetAppWindowForCurrentWindow()
    {
        var hwnd  = WinRT.Interop.WindowNative.GetWindowHandle(this);
        var wndId = Win32Interop.GetWindowIdFromWindow(hwnd);
        return AppWindow.GetFromWindowId(wndId);
    }

    private void OnItemClicked(ConfigItem item)
    {
        switch (item.Type)
        {
            case ConfigItemType.TaskKill:
                ShowTaskKillSubmenu();
                return;
            case ConfigItemType.PowerMenu:
                ShowPowerMenuSubmenu();
                return;
        }

        this.Close();
        CommandExecutor.Execute(item);
    }

    private void ShowTaskKillSubmenu()
    {
        var processes = CommandExecutor.GetRunningProcesses(_config);
        var items = processes.Select(p => new ConfigItem
        {
            Label = $"{p.Name}  (PID {p.Pid})",
            Type  = ConfigItemType.Cmd,
            Path  = p.Pid.ToString(),
        });
        PopulateItems(items);
    }

    private void ShowPowerMenuSubmenu()
    {
        var items = new List<ConfigItem>();
        if (_config.PowerSleep)     items.Add(new ConfigItem { Label = "Sleep",     Type = ConfigItemType.PowerSleep });
        if (_config.PowerHibernate) items.Add(new ConfigItem { Label = "Hibernate", Type = ConfigItemType.PowerHibernate });
        if (_config.PowerShutdown)  items.Add(new ConfigItem { Label = "Shut Down", Type = ConfigItemType.PowerShutdown });
        if (_config.PowerRestart)   items.Add(new ConfigItem { Label = "Restart",   Type = ConfigItemType.PowerRestart });
        if (_config.PowerLock)      items.Add(new ConfigItem { Label = "Lock",      Type = ConfigItemType.PowerLock });
        if (_config.PowerLogoff)    items.Add(new ConfigItem { Label = "Sign Out",  Type = ConfigItemType.PowerLogoff });
        PopulateItems(items);
    }
}
