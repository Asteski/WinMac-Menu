using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml;
using System.Diagnostics;
using WinMacMenuWinUI3.Models;
using WinMacMenuWinUI3.Services;

namespace WinMacMenuWinUI3;

public partial class App : Application
{
    private const uint BridgeShowMessage = 0x8057;

    private HostWindow?  _host;
    private TrayService? _tray;
    private HookService? _hooks;
    private DispatcherTimer? _parentMonitor;
    private MenuWindow?  _activeMenu;   // non-null while the flyout is visible
    private string       _iniPath = "";
    private bool         _oneShot;
    private int?         _parentPid;

    public App() => InitializeComponent();

    protected override void OnLaunched(LaunchActivatedEventArgs args)
    {
        var launchOptions = LaunchOptions.Parse(Environment.GetCommandLineArgs());
        _oneShot = launchOptions.OneShot;
        _parentPid = launchOptions.ParentPid;

        _iniPath = ResolveConfigPath(launchOptions.ConfigPath)
                   ?? IniParser.FindConfigFile()
                   ?? Path.Combine(AppContext.BaseDirectory, "config.ini");

        var config = IniParser.Load(_iniPath);
        var runInBackground = launchOptions.Bridge || (!_oneShot && config.RunInBackground);
        var showOnLaunch = _oneShot || launchOptions.ShowOnLaunch ||
                           (!launchOptions.Bridge && config.ShowOnLaunch);

        if (runInBackground)
        {
            _host = launchOptions.Bridge
                ? new HostWindow(BuildBridgeWindowTitle(_iniPath), BridgeShowMessage)
                : new HostWindow();
            _host.ShowMenuRequested += ToggleMenu;
            _host.Activate();
            if (launchOptions.Bridge && launchOptions.ParentPid is { } parentPid)
                StartParentMonitor(parentPid);

            if (!launchOptions.Bridge)
            {
                try
                {
                    _tray = new TrayService(_host.Hwnd, config);
                    _tray.ShowMenuRequested += () => ToggleMenu(MenuTriggerType.Other);
                    _tray.ExitRequested     += ShutdownApp;
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
        }

        // Respect ShowOnLaunch — when running in background the tray icon
        // is enough; only show the menu immediately if explicitly configured.
        if (!runInBackground || showOnLaunch)
            ShowMenu(MenuTriggerType.Other);
    }

    // Shows the menu, or closes it if already open (toggle behaviour)
    private void ToggleMenu(MenuTriggerType trigger)
    {
        if (_activeMenu != null)
        {
            _hooks?.ReleaseSuppressedWinKeys();
            _activeMenu.Close();
            // _activeMenu is cleared by the Closed handler below
            return;
        }
        ShowMenu(trigger);
    }

    private void ShowMenu(MenuTriggerType trigger)
    {
        if (_activeMenu != null) return; // already open

        try
        {
            var config  = IniParser.Load(_iniPath); // reload INI each time
            var window  = new MenuWindow(config, _iniPath, trigger, _parentPid);

            window.Closed += (_, _) =>
            {
                _hooks?.ReleaseSuppressedWinKeys();
                _activeMenu = null;
                if (_oneShot)
                    Environment.Exit(0);
            };

            _activeMenu = window;
            window.Activate();
        }
        catch (Exception ex)
        {
            _activeMenu = null;
            LogException("ShowMenu", ex);

            if (_oneShot)
                Environment.Exit(1);
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

    private void ShutdownApp()
    {
        _hooks?.Dispose();
        _tray?.Dispose();
        _activeMenu?.Close();
        _host?.Close();
        Environment.Exit(0); // ensure process actually terminates
    }

    private void StartParentMonitor(int parentPid)
    {
        _parentMonitor = new DispatcherTimer { Interval = TimeSpan.FromSeconds(5) };
        _parentMonitor.Tick += (_, _) =>
        {
            try
            {
                _ = Process.GetProcessById(parentPid);
            }
            catch
            {
                ShutdownApp();
            }
        };
        _parentMonitor.Start();
    }

    private static string BuildBridgeWindowTitle(string iniPath)
    {
        var fullPath = Path.GetFullPath(iniPath);
        return $"WinMacMenuWinUI3::{fullPath}";
    }

    private static string? ResolveConfigPath(string? path)
    {
        if (string.IsNullOrWhiteSpace(path))
            return null;

        return Path.IsPathRooted(path)
            ? path
            : Path.Combine(AppContext.BaseDirectory, path);
    }

    private sealed class LaunchOptions
    {
        public string? ConfigPath { get; private init; }
        public bool OneShot { get; private init; }
        public bool ShowOnLaunch { get; private init; }
        public bool Bridge { get; private init; }
        public int? ParentPid { get; private init; }

        public static LaunchOptions Parse(string[] args)
        {
            string? configPath = null;
            var oneShot = false;
            var showOnLaunch = false;
            var bridge = false;
            int? parentPid = null;

            for (var i = 1; i < args.Length; i++)
            {
                var arg = args[i];
                if (arg.Equals("--config", StringComparison.OrdinalIgnoreCase) && i + 1 < args.Length)
                {
                    configPath = args[++i];
                }
                else if (arg.Equals("--oneshot", StringComparison.OrdinalIgnoreCase))
                {
                    oneShot = true;
                }
                else if (arg.Equals("--show-on-launch", StringComparison.OrdinalIgnoreCase))
                {
                    showOnLaunch = true;
                }
                else if (arg.Equals("--bridge", StringComparison.OrdinalIgnoreCase))
                {
                    bridge = true;
                }
                else if (arg.Equals("--parent-pid", StringComparison.OrdinalIgnoreCase) && i + 1 < args.Length)
                {
                    if (int.TryParse(args[++i], out var pid) && pid > 0)
                        parentPid = pid;
                }
            }

            return new LaunchOptions
            {
                ConfigPath = string.IsNullOrWhiteSpace(configPath) ? null : configPath,
                OneShot = oneShot,
                ShowOnLaunch = showOnLaunch,
                Bridge = bridge,
                ParentPid = parentPid
            };
        }
    }
}
