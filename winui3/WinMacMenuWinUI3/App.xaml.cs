using Microsoft.UI.Xaml;
using WinMacMenuWinUI3.Services;

namespace WinMacMenuWinUI3;

public partial class App : Application
{
    private MenuWindow? _menuWindow;

    public App()
    {
        InitializeComponent();
    }

    protected override void OnLaunched(LaunchActivatedEventArgs args)
    {
        var iniPath = IniParser.FindConfigFile();
        if (iniPath == null)
        {
            // No config found — create default and open
            iniPath = Path.Combine(AppContext.BaseDirectory, "config.ini");
        }

        var config = IniParser.Load(iniPath);
        _menuWindow = new MenuWindow(config, iniPath);
        _menuWindow.Activate();
    }
}
