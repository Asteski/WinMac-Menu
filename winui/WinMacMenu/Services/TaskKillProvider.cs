using System.Diagnostics;
using WinMacMenu.Models;

namespace WinMacMenu.Services;

/// <summary>Builds the End Task submenu, mirroring the C app's TaskKill behaviour.</summary>
public static class TaskKillProvider
{
    private static readonly HashSet<string> SystemProcesses = new(StringComparer.OrdinalIgnoreCase)
    {
        "System", "Idle", "Registry", "Memory Compression", "smss", "csrss", "wininit",
        "winlogon", "services", "lsass", "svchost", "fontdrvhost", "dwm", "WUDFHost",
        "explorer", "RuntimeBroker", "SearchHost", "StartMenuExperienceHost", "TextInputHost",
        "ctfmon", "sihost", "taskhostw", "ApplicationFrameHost", "WinMacMenu",
    };

    public static IReadOnlyList<DynamicEntry> GetEntries(Config cfg)
    {
        var excludes = cfg.TaskKillExcludes
            .Split(',', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries)
            .ToHashSet(StringComparer.OrdinalIgnoreCase);

        int max = cfg.TaskKillMax > 0 ? cfg.TaskKillMax : int.MaxValue;
        int currentSession = Process.GetCurrentProcess().SessionId;

        var entries = new List<DynamicEntry>();
        foreach (var proc in Process.GetProcesses().OrderBy(p => SafeName(p), StringComparer.OrdinalIgnoreCase))
        {
            if (entries.Count >= max) break;
            using (proc)
            {
                var name = SafeName(proc);
                if (string.IsNullOrEmpty(name)) continue;
                if (excludes.Contains(name)) continue;
                if (cfg.TaskKillIgnoreSystem && SystemProcesses.Contains(name)) continue;
                if (!cfg.TaskKillAllDesktops && SafeSession(proc) != currentSession) continue;
                if (cfg.TaskKillListWindows && !HasVisibleWindow(proc)) continue;

                int pid = proc.Id;
                string exePath = SafePath(proc);
                string label = cfg.TaskKillListWindows && !string.IsNullOrEmpty(SafeTitle(proc))
                    ? SafeTitle(proc)
                    : name;

                entries.Add(new DynamicEntry
                {
                    Label = label,
                    Path = exePath,
                    ShowIcon = cfg.TaskKillShowIcons && !string.IsNullOrEmpty(exePath),
                    Action = () => Kill(pid),
                });
            }
        }

        return entries;
    }

    private static void Kill(int pid)
    {
        try
        {
            using var p = Process.GetProcessById(pid);
            p.Kill(entireProcessTree: true);
        }
        catch { }
    }

    private static string SafeName(Process p) { try { return p.ProcessName; } catch { return string.Empty; } }
    private static int SafeSession(Process p) { try { return p.SessionId; } catch { return -1; } }
    private static string SafeTitle(Process p) { try { return p.MainWindowTitle; } catch { return string.Empty; } }
    private static bool HasVisibleWindow(Process p) { try { return p.MainWindowHandle != 0 && !string.IsNullOrEmpty(p.MainWindowTitle); } catch { return false; } }

    private static string SafePath(Process p)
    {
        try { return p.MainModule?.FileName ?? string.Empty; }
        catch { return string.Empty; }
    }
}
