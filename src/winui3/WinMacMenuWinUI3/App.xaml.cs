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
    private MenuWindow?  _activeMenu;   // non-null while the flyout is visible
    private string       _iniPath = "";

    public App() => InitializeComponent();

    protected override void OnLaunched(LaunchActivatedEventArgs args)
    {
        _iniPath = IniParser.FindConfigFile()
                   ?? Path.Combine(AppContext.BaseDirectory, "config.ini");

        var config = IniParser.Load(_iniPath);

        if (config.RunInBackground)
        {
            _host = new HostWindow();
            _host.Activate();

            try
            {
                _tray = new TrayService(_host.Hwnd, config);
                _tray.ShowMenuRequested += ToggleMenu;
                _tray.ExitRequested     += Exit;
            }
            catch { /* tray icon unavailable — app still runs */ }

            try
            {
                var dq = DispatcherQueue.GetForCurrentThread();
                if (dq != null)
                {
                    _hooks = new HookService(config, dq);
                    _hooks.MenuRequested += ToggleMenu;
                }
            }
            catch { /* hooks unavailable — use tray icon to open menu */ }
        }

        // Respect ShowOnLaunch — when running in background the tray icon
        // is enough; only show the menu immediately if explicitly configured.
        if (!config.RunInBackground || config.ShowOnLaunch)
            ShowMenu();
    }

    // Shows the menu, or closes it if already open (toggle behaviour)
    private void ToggleMenu()
    {
        if (_activeMenu != null)
        {
            _activeMenu.Close();
            // _activeMenu is cleared by the Closed handler below
            return;
        }
        ShowMenu();
    }

    private void ShowMenu()
    {
        if (_activeMenu != null) return; // already open

        try
        {
            var config  = IniParser.Load(_iniPath); // reload INI each time
            var window  = new MenuWindow(config, _iniPath);

            window.Closed += (_, _) => _activeMenu = null;

            _activeMenu = window;
            window.Activate();
        }
        catch (Exception ex)
        {
            _activeMenu = null;
            LogException("ShowMenu", ex);
        }
    }

    internal static void LogException(string context, Exception ex)
    {
        try
        {
            var path = Path.Combine(Path.GetTempPath(), "WinMacMenuWinUI3.log");
            File.AppendAllText(path,
                $"[{DateTime.Now:yyyy-MM-dd HH:mm:ss.fff}] {context}{Environment.NewLine}{ex}{Environment.NewLine}{Environment.NewLine}");
        }
        catch { }
    }

    private void Exit()
    {
        _hooks?.Dispose();
        _tray?.Dispose();
        _activeMenu?.Close();
        _host?.Close();
        Environment.Exit(0); // ensure process actually terminates
    }
}
