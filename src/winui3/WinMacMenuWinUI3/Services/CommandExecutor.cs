using System.Diagnostics;
using System.Runtime.InteropServices;
using WinMacMenuWinUI3.Models;

namespace WinMacMenuWinUI3.Services;

public static class CommandExecutor
{
    [DllImport("user32.dll")]
    private static extern bool LockWorkStation();

    [DllImport("user32.dll", SetLastError = true)]
    private static extern bool ExitWindowsEx(uint uFlags, uint dwReason);

    [DllImport("powrprof.dll", SetLastError = true)]
    private static extern bool SetSuspendState(bool hibernate, bool forceCritical, bool disableWakeEvent);

    public static void Execute(ConfigItem item)
    {
        try
        {
            switch (item.Type)
            {
                case ConfigItemType.Uri:
                    OpenUri(item.Path);
                    break;
                case ConfigItemType.File:
                    ShellOpen(item.Path, item.Params);
                    break;
                case ConfigItemType.Cmd:
                    RunCmd(item.Path);
                    break;
                case ConfigItemType.Folder:
                case ConfigItemType.FolderSubmenu:
                    ShellOpen(item.Path);
                    break;
                case ConfigItemType.PowerSleep:
                    SetSuspendState(false, false, false);
                    break;
                case ConfigItemType.PowerHibernate:
                    SetSuspendState(true, false, false);
                    break;
                case ConfigItemType.PowerShutdown:
                    Process.Start("shutdown", "/s /t 0");
                    break;
                case ConfigItemType.PowerRestart:
                    Process.Start("shutdown", "/r /t 0");
                    break;
                case ConfigItemType.PowerLock:
                    LockWorkStation();
                    break;
                case ConfigItemType.PowerLogoff:
                    ExitWindowsEx(0x00, 0);
                    break;
                case ConfigItemType.ThisPC:
                    ShellOpen("shell:MyComputerFolder");
                    break;
                case ConfigItemType.Home:
                    ShellOpen(Environment.GetFolderPath(Environment.SpecialFolder.UserProfile));
                    break;
            }
        }
        catch (Exception ex)
        {
            Debug.WriteLine($"Execute failed for {item.Type}: {ex.Message}");
        }
    }

    public static void OpenUri(string uri)
    {
        if (string.IsNullOrEmpty(uri)) return;

        // .msc snap-ins live in System32 and must be launched via mmc.exe
        if (uri.EndsWith(".msc", StringComparison.OrdinalIgnoreCase))
        {
            var mscPath = Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.System), uri);
            Process.Start(new ProcessStartInfo("mmc.exe", $"\"{mscPath}\"")
            {
                UseShellExecute = true
            });
            return;
        }

        Process.Start(new ProcessStartInfo(uri) { UseShellExecute = true });
    }

    public static void ShellOpen(string path, string? args = null)
    {
        if (string.IsNullOrEmpty(path)) return;
        var psi = new ProcessStartInfo(path)
        {
            UseShellExecute = true,
            Arguments = args ?? ""
        };
        Process.Start(psi);
    }

    private static void RunCmd(string command)
    {
        var psi = new ProcessStartInfo("cmd.exe", $"/c {command}")
        {
            UseShellExecute = false,
            CreateNoWindow = true
        };
        Process.Start(psi);
    }

    public static List<FolderEntry> GetFolderContents(string folderPath, AppConfig cfg)
    {
        var result = new List<FolderEntry>();
        try
        {
            var dir = new DirectoryInfo(folderPath);
            if (!dir.Exists) return result;

            var sortMultiplier = cfg.SortDirection == "descending" ? -1 : 1;

            IEnumerable<FileSystemInfo> entries = cfg.FoldersFirst
                ? dir.GetDirectories().Cast<FileSystemInfo>().Concat(dir.GetFiles())
                : dir.GetFileSystemInfos();

            entries = cfg.SortBy switch
            {
                "date_modified" => entries.OrderBy(e => e.LastWriteTime.Ticks * sortMultiplier),
                "date_created"  => entries.OrderBy(e => e.CreationTime.Ticks * sortMultiplier),
                "size"          => entries.OrderBy(e => (e is FileInfo f ? f.Length : 0) * sortMultiplier),
                "type"          => entries.OrderBy(e => e.Extension.ToLowerInvariant()),
                _               => entries.OrderBy(e => e.Name, StringComparer.OrdinalIgnoreCase),
            };

            if (!cfg.ShowHidden)
                entries = entries.Where(e => !e.Attributes.HasFlag(FileAttributes.Hidden));

            if (cfg.MaxItems > 0)
                entries = entries.Take(cfg.MaxItems);

            foreach (var entry in entries)
            {
                var name = entry.Name;
                if (entry is FileInfo && !cfg.ShowFileExtensions)
                    name = Path.GetFileNameWithoutExtension(name);

                result.Add(new FolderEntry
                {
                    Name = name,
                    FullPath = entry.FullName,
                    IsDirectory = entry is DirectoryInfo
                });
            }
        }
        catch { }
        return result;
    }

    // ── Win32 for window enumeration ──────────────────────────────────────

    private delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);

    [DllImport("user32.dll")] private static extern bool EnumWindows(EnumWindowsProc lpEnumFunc, IntPtr lParam);
    [DllImport("user32.dll")] private static extern bool IsWindowVisible(IntPtr hWnd);
    [DllImport("user32.dll")] private static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint lpdwProcessId);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern int GetWindowText(IntPtr hWnd, System.Text.StringBuilder lpString, int nMaxCount);
    [DllImport("user32.dll")] private static extern int GetWindowTextLength(IntPtr hWnd);
    [DllImport("user32.dll")] private static extern IntPtr GetAncestor(IntPtr hWnd, uint gaFlags);
    [DllImport("dwmapi.dll")] private static extern int DwmGetWindowAttribute(IntPtr hwnd, int dwAttribute, out int pvAttribute, int cbAttribute);

    private const uint GA_ROOT = 2;
    private const int DWMWA_CLOAKED = 14;

    // Background/hidden Windows system process names — excluded when TaskKillIgnoreSystem=true
    private static readonly HashSet<string> SystemProcessNames = new(StringComparer.OrdinalIgnoreCase)
    {
        "system", "registry", "smss", "csrss", "wininit", "services", "lsass", "lsaiso",
        "svchost", "dwm", "conhost", "winlogon", "fontdrvhost", "sihost", "taskhostw",
        "searchindexer", "searchhost", "spoolsv", "audiodg", "ctfmon", "dllhost",
        "securityhealthservice", "msmpeng", "nisSrv", "sgrmbroker", "wudfhost",
        "wmiprvse", "runtimebroker", "backgroundtaskhost", "applicationframehost",
        "shellexperiencehost", "startmenuexperiencehost", "textinputhost",
        "systemsettings", "lockapp", "logonui", "userinit",
    };

    public static List<ProcessEntry> GetRunningProcesses(AppConfig cfg)
    {
        var excludes = cfg.TaskKillExcludes
            .Split(',', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries)
            .Select(e => e.Trim().ToLowerInvariant())
            .ToHashSet();

        return cfg.TaskKillListWindows
            ? EnumerateWindows(cfg, excludes)
            : EnumerateProcessesWithWindows(cfg, excludes);
    }

    // TaskKillListWindows=false (default): one entry per process that owns a visible window
    private static List<ProcessEntry> EnumerateProcessesWithWindows(AppConfig cfg, HashSet<string> excludes)
    {
        var seen    = new HashSet<int>();   // deduplicate by PID
        var result  = new List<ProcessEntry>();

        EnumWindows((hWnd, _) =>
        {
            if (result.Count >= cfg.TaskKillMax) return false;
            if (!IsRealUserWindow(hWnd)) return true;

            GetWindowThreadProcessId(hWnd, out var pid);
            if ((int)pid == Environment.ProcessId) return true;
            if (seen.Contains((int)pid)) return true;
            seen.Add((int)pid);

            try
            {
                using var proc = Process.GetProcessById((int)pid);
                var name = proc.ProcessName;
                if (cfg.TaskKillIgnoreSystem && SystemProcessNames.Contains(name)) return true;
                if (excludes.Contains(name.ToLowerInvariant())) return true;

                result.Add(new ProcessEntry { Name = name, Pid = (int)pid });
            }
            catch { }

            return true;
        }, IntPtr.Zero);

        return result.OrderBy(p => p.Name, StringComparer.OrdinalIgnoreCase).ToList();
    }

    // TaskKillListWindows=true: one entry per visible window (by title), may repeat same process
    private static List<ProcessEntry> EnumerateWindows(AppConfig cfg, HashSet<string> excludes)
    {
        var result = new List<ProcessEntry>();

        EnumWindows((hWnd, _) =>
        {
            if (result.Count >= cfg.TaskKillMax) return false;
            if (!IsRealUserWindow(hWnd)) return true;

            var len = GetWindowTextLength(hWnd);
            if (len == 0) return true;
            var sb = new System.Text.StringBuilder(len + 1);
            GetWindowText(hWnd, sb, sb.Capacity);
            var title = sb.ToString();

            GetWindowThreadProcessId(hWnd, out var pid);
            if ((int)pid == Environment.ProcessId) return true;

            try
            {
                using var proc = Process.GetProcessById((int)pid);
                var name = proc.ProcessName;
                if (cfg.TaskKillIgnoreSystem && SystemProcessNames.Contains(name)) return true;
                if (excludes.Contains(name.ToLowerInvariant())) return true;

                result.Add(new ProcessEntry { Name = title, Pid = (int)pid });
            }
            catch { }

            return true;
        }, IntPtr.Zero);

        return result;
    }

    // A "real" user window: visible, top-level, not cloaked, not a tool/message window
    private static bool IsRealUserWindow(IntPtr hWnd)
    {
        if (!IsWindowVisible(hWnd)) return false;

        // Must be a root window (no owner that is also a root)
        if (GetAncestor(hWnd, GA_ROOT) != hWnd) return false;

        // Skip cloaked windows (e.g. UWP apps on other virtual desktops)
        DwmGetWindowAttribute(hWnd, DWMWA_CLOAKED, out var cloaked, sizeof(int));
        if (cloaked != 0) return false;

        return true;
    }

    public static void KillProcess(int pid)
    {
        try { Process.GetProcessById(pid)?.Kill(); } catch { }
    }
}

public record FolderEntry
{
    public string Name { get; init; } = "";
    public string FullPath { get; init; } = "";
    public bool IsDirectory { get; init; }
}

public record ProcessEntry
{
    public string Name { get; init; } = "";
    public int Pid { get; init; }
}
