using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml;
using System.Diagnostics;
using WinMacMenuWinUI3.Models;
using WinMacMenuWinUI3.Services;

namespace WinMacMenuWinUI3;

public partial class App : Application
{
    private const uint BridgeShowMessage = 0x8057;
    private const int MaxLogBytes = 1024 * 1024;
    private static readonly object LogLock = new();

    private HostWindow?  _host;
    private TrayService? _tray;
    private HookService? _hooks;
    private DispatcherTimer? _parentMonitor;
    private DispatcherTimer? _refreshTimer;
    private DispatcherTimer? _specialFolderRefreshTimer;
    private MenuWindow?  _activeMenu;   // non-null while the flyout is visible
    private MenuWindow?  _menuWindow;   // retained transparent anchor for fast reopening
    private string       _iniPath = "";
    private bool         _oneShot;
    private int?         _parentPid;
    private AppConfig?   _configCache;
    private DateTime     _configStampUtc;
    private int          _refreshInProgress;
    private int          _showMenuInProgress;
    private long         _configGeneration;

    public App() => InitializeComponent();

    protected override void OnLaunched(LaunchActivatedEventArgs args)
    {
        var launchOptions = LaunchOptions.Parse(Environment.GetCommandLineArgs());
        _oneShot = launchOptions.OneShot;
        _parentPid = launchOptions.ParentPid;

        _iniPath = ResolveConfigPath(launchOptions.ConfigPath)
                   ?? IniParser.FindConfigFile()
                   ?? Path.Combine(AppContext.BaseDirectory, "config.ini");

        var config = LoadConfigCache(force: true);
        var runInBackground = launchOptions.Bridge || (!_oneShot && config.RunInBackground);
        var showOnLaunch = _oneShot || launchOptions.ShowOnLaunch ||
                           (!launchOptions.Bridge && config.ShowOnLaunch);

        if (runInBackground)
        {
            _host = launchOptions.Bridge
                ? new HostWindow(BuildBridgeWindowTitle(_iniPath, _parentPid), BridgeShowMessage)
                : new HostWindow();
            _host.ShowMenuRequested += ToggleMenu;
            _host.ConfigReloadRequested += ReloadConfigNow;
            _host.ShutdownRequested += ShutdownApp;
            _host.ActivateHidden();
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

        StartBackgroundRefresh();
        StartSpecialFolderRefresh();
        WarmDynamicCaches(config);
        if (runInBackground && !showOnLaunch)
            PreloadMenuWindow(config);

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
            _activeMenu.Dismiss();
            return;
        }
        ShowMenu(trigger);
    }

    private void ShowMenu(MenuTriggerType trigger)
    {
        if (_activeMenu != null) return; // already open
        if (Interlocked.Exchange(ref _showMenuInProgress, 1) == 1) return;

        var started = Stopwatch.GetTimestamp();
        var reusedWindow = _menuWindow != null;
        try
        {
            var config = GetCachedConfig();
            // Warm in the background; the menu renders cached contents or placeholders.
            _ = MenuWindow.WarmSpecialFoldersForMenuOpenAsync(config);

            if (_activeMenu != null) return;

            if (_menuWindow == null)
            {
                var window = CreateMenuWindow(config, trigger);

                _menuWindow = window;
                _activeMenu = window;
                window.Activate();
            }
            else
            {
                _activeMenu = _menuWindow;
                _menuWindow.Open(config, trigger);
            }

            var elapsedMs = Stopwatch.GetElapsedTime(started).TotalMilliseconds;
            if (elapsedMs >= 100)
                LogBreadcrumb($"ShowMenu trigger={trigger} reused={reusedWindow} elapsedMs={elapsedMs:F0}");
        }
        catch (Exception ex)
        {
            _activeMenu = null;
            LogException("ShowMenu", ex);

            if (_oneShot)
                Environment.Exit(1);
        }
        finally
        {
            Interlocked.Exchange(ref _showMenuInProgress, 0);
        }
    }

    private void PreloadMenuWindow(AppConfig config)
    {
        if (_menuWindow != null) return;

        var window = CreateMenuWindow(config, MenuTriggerType.Other, showOnLoad: false);
        _menuWindow = window;
        window.Activate();
        window.KeepHidden();
    }

    private MenuWindow CreateMenuWindow(AppConfig config, MenuTriggerType trigger, bool showOnLoad = true)
    {
        var window = new MenuWindow(config, _iniPath, trigger, _parentPid, showOnLoad);
        window.Dismissed += () =>
        {
            _hooks?.ReleaseSuppressedWinKeys();
            _activeMenu = null;
            if (_oneShot)
                Environment.Exit(0);
        };
        return window;
    }

    private AppConfig GetCachedConfig()
        => _configCache ?? LoadConfigCache(force: true);

    private AppConfig LoadConfigCache(bool force)
    {
        var stamp = GetConfigStampUtc();
        if (!force && _configCache != null && stamp == _configStampUtc)
            return _configCache;

        var config = IniParser.Load(_iniPath);
        _configCache = config;
        _configStampUtc = stamp;
        return config;
    }

    private void ReloadConfigNow()
    {
        ++_configGeneration;
        var config = LoadConfigCache(force: true);
        MenuWindow.InvalidateCachedData();
        WarmDynamicCaches(config);

        var staleWindow = _menuWindow;
        _menuWindow = null;
        if (staleWindow != null)
        {
            if (_activeMenu == staleWindow)
            {
                _activeMenu = null;
                staleWindow.Dismiss();
                staleWindow.Close();
            }
            else
            {
                staleWindow.Close();
            }
        }

        PreloadMenuWindow(config);
    }

    private DateTime GetConfigStampUtc()
    {
        try { return File.GetLastWriteTimeUtc(_iniPath); }
        catch { return DateTime.MinValue; }
    }

    private void StartBackgroundRefresh()
    {
        _refreshTimer = new DispatcherTimer { Interval = TimeSpan.FromMinutes(2) };
        _refreshTimer.Tick += (_, _) => RefreshInBackground();
        _refreshTimer.Start();
    }

    private void StartSpecialFolderRefresh()
    {
        _specialFolderRefreshTimer = new DispatcherTimer { Interval = TimeSpan.FromMinutes(10) };
        _specialFolderRefreshTimer.Tick += (_, _) =>
            MenuWindow.RefreshOpenedSpecialFolderCaches(GetCachedConfig());
        _specialFolderRefreshTimer.Start();
    }

    private async void RefreshInBackground()
    {
        if (Interlocked.Exchange(ref _refreshInProgress, 1) == 1)
            return;

        var iniPath = _iniPath;
        var generation = _configGeneration;
        var cachedStamp = _configStampUtc;
        var cachedConfig = GetCachedConfig();

        try
        {
            var result = await System.Threading.Tasks.Task.Run(() =>
            {
                var config = cachedConfig;
                var stamp = DateTime.MinValue;
                try { stamp = File.GetLastWriteTimeUtc(iniPath); } catch { }

                if (stamp != cachedStamp)
                    config = IniParser.Load(iniPath);

                MenuWindow.WarmDynamicCaches(config);
                return (config, stamp);
            });

            if (generation != _configGeneration) return;
            _configCache = result.config;
            _configStampUtc = result.stamp;
        }
        catch
        {
        }
        finally
        {
            Interlocked.Exchange(ref _refreshInProgress, 0);
        }
    }

    private static void WarmDynamicCaches(AppConfig config)
    {
        _ = System.Threading.Tasks.Task.Run(() => MenuWindow.WarmDynamicCaches(config));
    }

    internal static void LogException(string context, Exception ex)
        => WriteLog($"{context}{Environment.NewLine}{ex}");

    internal static void LogBreadcrumb(string message)
        => WriteLog(message);

    private static void WriteLog(string message)
    {
        try
        {
            // Bound individual records as well as file growth. Retain one previous
            // file for diagnosis; logging failure must never interrupt the menu.
            if (message.Length > 16384) message = message[..16384] + " [truncated]";
            var record = $"[{DateTime.Now:yyyy-MM-dd HH:mm:ss.fff}] {message}{Environment.NewLine}";
            lock (LogLock)
            {
                var path = Path.Combine(Path.GetTempPath(), "WinMacMenuWinUI3.log");
                var length = new FileInfo(path);
                if (length.Exists && length.Length + System.Text.Encoding.UTF8.GetByteCount(record) > MaxLogBytes)
                    File.Move(path, path + ".old", overwrite: true);
                File.AppendAllText(path, record);
            }
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
                using var parent = Process.GetProcessById(parentPid);
                if (parent.HasExited) ShutdownApp();
            }
            catch
            {
                ShutdownApp();
            }
        };
        _parentMonitor.Start();
    }

    private static string BuildBridgeWindowTitle(string iniPath, int? parentPid)
    {
        var fullPath = Path.GetFullPath(iniPath);
        return $"WinMacMenuWinUI3::{parentPid.GetValueOrDefault()}::{fullPath}";
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
