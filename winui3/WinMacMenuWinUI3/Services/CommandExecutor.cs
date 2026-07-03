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

            foreach (var entry in entries.Take(cfg.MaxItems))
            {
                if (!cfg.ShowHidden && entry.Attributes.HasFlag(FileAttributes.Hidden))
                    continue;

                var name = entry.Name;
                if (entry is FileInfo fi && !cfg.ShowFileExtensions)
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

    public static List<ProcessEntry> GetRunningProcesses(AppConfig cfg)
    {
        var result = new List<ProcessEntry>();
        var excludes = cfg.TaskKillExcludes
            .Split(',', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries)
            .Select(e => e.ToLowerInvariant())
            .ToHashSet();

        var systemProcesses = new HashSet<string> { "system", "registry", "smss", "csrss", "wininit",
            "services", "lsass", "svchost", "dwm", "conhost", "winlogon", "fontdrvhost", "sihost" };

        try
        {
            var processes = Process.GetProcesses()
                .Where(p =>
                {
                    var name = p.ProcessName.ToLowerInvariant();
                    if (excludes.Contains(name)) return false;
                    if (cfg.TaskKillIgnoreSystem && systemProcesses.Contains(name)) return false;
                    if (p.Id == Environment.ProcessId) return false;
                    return true;
                })
                .OrderBy(p => p.ProcessName, StringComparer.OrdinalIgnoreCase)
                .Take(cfg.TaskKillMax);

            foreach (var p in processes)
            {
                result.Add(new ProcessEntry { Name = p.ProcessName, Pid = p.Id });
            }
        }
        catch { }
        return result;
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
