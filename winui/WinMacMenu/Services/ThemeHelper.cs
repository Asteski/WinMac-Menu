using Microsoft.UI.Xaml;
using Microsoft.Win32;

namespace WinMacMenu.Services;

/// <summary>Reads the user's light/dark preference, mirroring the C app's theme adaptation.</summary>
public static class ThemeHelper
{
    public static bool IsLightTheme()
    {
        try
        {
            using var key = Registry.CurrentUser.OpenSubKey(
                @"Software\Microsoft\Windows\CurrentVersion\Themes\Personalize");
            // AppsUseLightTheme: 1 = light, 0 = dark. Absent => light.
            return key?.GetValue("AppsUseLightTheme") is int v ? v != 0 : true;
        }
        catch
        {
            return true;
        }
    }

    public static ElementTheme CurrentElementTheme()
        => IsLightTheme() ? ElementTheme.Light : ElementTheme.Dark;
}
