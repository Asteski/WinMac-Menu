using Microsoft.Win32;

namespace WinMacMenu.Services;

/// <summary>Manages the optional HKCU\...\Run entry, mirroring util.c's StartOnLogin handling.</summary>
public static class StartupRegistry
{
    private const string RunKey = @"Software\Microsoft\Windows\CurrentVersion\Run";
    private const string ValueName = "WinMacMenu";

    private static string CommandLine(string iniPath)
    {
        var exe = Environment.ProcessPath ?? AppContext.BaseDirectory;
        var defaultIni = ConfigDefaultPath();
        // Only pass --config when a non-default INI is in use, matching the C app.
        return string.Equals(iniPath, defaultIni, StringComparison.OrdinalIgnoreCase)
            ? $"\"{exe}\""
            : $"\"{exe}\" --config \"{iniPath}\"";
    }

    private static string ConfigDefaultPath()
        => System.IO.Path.Combine(AppContext.BaseDirectory, "config.ini");

    public static bool IsEnabled()
    {
        try
        {
            using var key = Registry.CurrentUser.OpenSubKey(RunKey);
            return key?.GetValue(ValueName) is not null;
        }
        catch { return false; }
    }

    public static void Set(bool enabled, string iniPath)
    {
        try
        {
            using var key = Registry.CurrentUser.CreateSubKey(RunKey, writable: true);
            if (key is null) return;
            if (enabled)
                key.SetValue(ValueName, CommandLine(iniPath), RegistryValueKind.String);
            else
                key.DeleteValue(ValueName, throwOnMissingValue: false);
        }
        catch { }
    }

    /// <summary>Reconciles config vs registry, returning the effective state.</summary>
    public static void Sync(bool desired, string iniPath)
    {
        if (desired != IsEnabled())
            Set(desired, iniPath);
    }
}
