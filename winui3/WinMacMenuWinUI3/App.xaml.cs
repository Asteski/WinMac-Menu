using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml;
using WinMacMenuWinUI3.Models;
using WinMacMenuWinUI3.Services;

namespace WinMacMenuWinUI3;

public partial class App : Application
{
    private HostWindow?  _host;
    private TrayService? _tray;
    private HookService? _hooks;
    private string       _iniPath = "";

    public App() => InitializeComponent();

    protected override void OnLaunched(LaunchActivatedEventArgs args)
    {
        _iniPath = IniParser.FindConfigFile()
                   ?? Path.Combine(AppContext.BaseDirectory, "config.ini");

        var config = IniParser.Load(_iniPath);

        if (config.RunInBackground)
        {
            // Keep a hidden window alive so WinUI3 doesn't auto-exit
            _host = new HostWindow();
            _host.Activate();

            _tray = new TrayService(_host.Hwnd, config);
            _tray.ShowMenuRequested += ShowMenu;
            _tray.ExitRequested     += Exit;

            _hooks = new HookService(config, DispatcherQueue.GetForCurrentThread());
            _hooks.MenuRequested += ShowMenu;
        }

        // Always show the menu on first launch (matches original app behaviour)
        ShowMenu();
    }

    private void ShowMenu()
    {
        // Reload config on every open so INI changes are picked up live
        var config = IniParser.Load(_iniPath);
        var window = new MenuWindow(config, _iniPath);
        window.Activate();
    }

    private void Exit()
    {
        _hooks?.Dispose();
        _tray?.Dispose();
        _host?.Close();
    }
}
