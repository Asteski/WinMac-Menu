using System.Diagnostics;
using WinMacMenu.Interop;
using WinMacMenu.Models;

namespace WinMacMenu.Services;

/// <summary>Executes menu items and power actions, mirroring util.c / menu.c.</summary>
public static class ActionLauncher
{
    private const uint EWX_SHUTDOWN = 0x00000001;
    private const uint EWX_REBOOT = 0x00000002;
    private const uint SHTDN_REASON_MAJOR_OTHER = 0x00000000;

    /// <summary>Runs the action associated with a leaf config item.</summary>
    public static void Run(ConfigItem item)
    {
        switch (item.Type)
        {
            case ConfigItemType.Uri:
                OpenUri(item.Path);
                break;
            case ConfigItemType.File:
                OpenFile(item.Path, item.Params);
                break;
            case ConfigItemType.Cmd:
                // Matches C: run cmd.exe with params (or path) as the command line.
                RunProcess("cmd.exe", string.IsNullOrEmpty(item.Params) ? item.Path : item.Params);
                break;
            case ConfigItemType.Folder:
                OpenUri(item.Path);
                break;
            case ConfigItemType.PowerSleep:
                NativeMethods.SetSuspendState(false, false, false);
                break;
            case ConfigItemType.PowerHibernate:
                NativeMethods.SetSuspendState(true, false, false);
                break;
            case ConfigItemType.PowerShutdown:
                Shutdown(reboot: false);
                break;
            case ConfigItemType.PowerRestart:
                Shutdown(reboot: true);
                break;
            case ConfigItemType.PowerLock:
                NativeMethods.LockWorkStation();
                break;
            case ConfigItemType.PowerLogoff:
                Logoff();
                break;
        }
    }

    /// <summary>Opens a filesystem path (file or folder) via the shell.</summary>
    public static void OpenPath(string path) => OpenUri(path);

    public static void OpenUri(string uri)
    {
        if (string.IsNullOrWhiteSpace(uri)) return;
        try
        {
            Process.Start(new ProcessStartInfo(uri) { UseShellExecute = true });
        }
        catch
        {
            // Mirror the C app's best-effort ShellExecute (no error UI).
        }
    }

    private static void OpenFile(string file, string? args)
    {
        if (string.IsNullOrWhiteSpace(file)) return;
        var psi = new ProcessStartInfo(file) { UseShellExecute = true };
        if (!string.IsNullOrEmpty(args)) psi.Arguments = args;
        try { Process.Start(psi); } catch { }
    }

    private static void RunProcess(string file, string? args)
    {
        var psi = new ProcessStartInfo(file) { UseShellExecute = true };
        if (!string.IsNullOrEmpty(args)) psi.Arguments = args;
        try { Process.Start(psi); } catch { }
    }

    private static void Shutdown(bool reboot)
    {
        AcquireShutdownPrivilege();
        uint flags = (reboot ? EWX_REBOOT : EWX_SHUTDOWN) | NativeMethods.EWX_FORCEIFHUNG;
        NativeMethods.ExitWindowsEx(flags, SHTDN_REASON_MAJOR_OTHER);
    }

    private static void Logoff()
    {
        AcquireShutdownPrivilege();
        NativeMethods.ExitWindowsEx(NativeMethods.EWX_LOGOFF | NativeMethods.EWX_FORCEIFHUNG, SHTDN_REASON_MAJOR_OTHER);
    }

    private static void AcquireShutdownPrivilege()
    {
        if (!NativeMethods.OpenProcessToken(NativeMethods.GetCurrentProcess(),
                NativeMethods.TOKEN_ADJUST_PRIVILEGES | NativeMethods.TOKEN_QUERY, out var token))
            return;
        try
        {
            if (!NativeMethods.LookupPrivilegeValue(null, NativeMethods.SE_SHUTDOWN_NAME, out var luid))
                return;
            var tp = new NativeMethods.TOKEN_PRIVILEGES
            {
                PrivilegeCount = 1,
                Privileges = new NativeMethods.LUID_AND_ATTRIBUTES { Luid = luid, Attributes = NativeMethods.SE_PRIVILEGE_ENABLED },
            };
            NativeMethods.AdjustTokenPrivileges(token, false, ref tp, 0, 0, 0);
        }
        finally
        {
            NativeMethods.CloseHandle(token);
        }
    }
}
