using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml;
using WinMacMenu.Configuration;
using WinMacMenu.Models;
using WinMacMenu.Services;

namespace WinMacMenu;

public partial class App : Application
{
    private readonly SingleInstance _single;
    private readonly string _configPath;

    private DispatcherQueue _dispatcher = null!;
    private Config _config = null!;
    private HostWindow _host = null!;
    private MessageWindow? _messageWindow;
    private TrayIcon? _tray;
    private KeyboardTrigger? _keyboard;

    public App(SingleInstance single, string configPath)
    {
        _single = single;
        _configPath = configPath;
        InitializeComponent();
    }

    protected override void OnLaunched(LaunchActivatedEventArgs args)
    {
        _dispatcher = DispatcherQueue.GetForCurrentThread();
        _config = ConfigLoader.Load(_configPath);

        _host = new HostWindow();
        _host.MenuClosed += OnMenuClosed;

        // Reconcile StartOnLogin with the registry, as the C app does on startup.
        StartupRegistry.Sync(_config.StartOnLogin, _config.IniPath);

        if (_config.RunInBackground)
        {
            if (_config.ShowTrayIcon)
                SetupTray();

            _keyboard = new KeyboardTrigger(_config, () => _dispatcher.TryEnqueue(ToggleMenu));
            _keyboard.Install();

            _single.StartToggleListener(_dispatcher, ToggleMenu);

            if (_config.ShowOnLaunch)
                ShowMenu();
        }
        else
        {
            // Single-run (legacy) mode: show once, then exit when the menu closes.
            _single.StartToggleListener(_dispatcher, ToggleMenu);
            if (_config.ShowOnLaunch)
                ShowMenu();
            else
                ExitApp();
        }
    }

    private void SetupTray()
    {
        _messageWindow = new MessageWindow();
        _tray = new TrayIcon(_messageWindow, _config);
        _tray.ShowMenuRequested += () => _dispatcher.TryEnqueue(ShowMenu);
        _tray.ReloadRequested += () => _dispatcher.TryEnqueue(ReloadConfig);
        _tray.ExitRequested += () => _dispatcher.TryEnqueue(ExitApp);
        _tray.StartOnLoginToggled += enabled => _dispatcher.TryEnqueue(() =>
        {
            StartupRegistry.Set(enabled, _config.IniPath);
            _config.StartOnLogin = enabled;
        });
        _tray.Show();
    }

    private void ToggleMenu()
    {
        if (_host.IsMenuOpen)
            _host.CloseMenu();
        else
            ShowMenu();
    }

    private void ShowMenu()
    {
        // Honour the configured Windows-key action when triggered; tray/explicit shows always open ours.
        _host.ShowMenu(_config);
    }

    private void ReloadConfig()
    {
        _config = ConfigLoader.Load(_configPath);
    }

    private void OnMenuClosed()
    {
        if (!_config.RunInBackground)
            ExitApp();
    }

    private void ExitApp()
    {
        _keyboard?.Dispose();
        _tray?.Dispose();
        _messageWindow?.Dispose();
        _host?.Close();
        Exit();
    }
}
