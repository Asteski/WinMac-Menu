using System.Diagnostics;
using WinMacMenu.Interop;
using WinMacMenu.Models;

namespace WinMacMenu.Services;

/// <summary>Builds the Recent Items submenu from %AppData%\Microsoft\Windows\Recent, mirroring recent.c.</summary>
public static class RecentItemsProvider
{
    public static IReadOnlyList<DynamicEntry> GetEntries(Config cfg)
    {
        var result = new List<DynamicEntry>();
        var recent = Environment.GetFolderPath(Environment.SpecialFolder.Recent);
        int max = cfg.RecentMax > 0 ? cfg.RecentMax : 12;

        if (Directory.Exists(recent))
        {
            foreach (var lnk in Directory.EnumerateFiles(recent, "*.lnk"))
            {
                if (result.Count >= max) break;
                var target = ShellLink.ResolveTarget(lnk);
                if (string.IsNullOrEmpty(target)) continue;

                bool isFolder;
                try
                {
                    var attr = File.GetAttributes(target);
                    isFolder = (attr & FileAttributes.Directory) != 0;
                }
                catch
                {
                    continue; // stale target — skip, like the C app.
                }

                result.Add(new DynamicEntry
                {
                    Label = FormatLabel(target, cfg, isFolder),
                    Path = target,
                    IsFolder = isFolder,
                    ShowIcon = cfg.RecentShowIcons,
                });
            }
        }

        if (cfg.RecentShowCleanItems && result.Count > 0)
        {
            result.Add(DynamicEntry.Separator);
            result.Add(new DynamicEntry
            {
                Label = "Clear Recent Items list",
                Action = ClearAll,
            });
        }

        return result;
    }

    private static string FormatLabel(string target, Config cfg, bool isFolder)
    {
        string label = cfg.RecentLabelMode == 1 ? Path.GetFileName(target) : target;
        if (string.IsNullOrEmpty(label)) label = target;
        if (!isFolder && !cfg.RecentShowExtensions)
        {
            var noExt = Path.GetFileNameWithoutExtension(label);
            if (!string.IsNullOrEmpty(noExt))
                label = cfg.RecentLabelMode == 1 ? noExt : Path.Combine(Path.GetDirectoryName(label) ?? "", noExt);
        }
        return label;
    }

    public static void OpenParentFolder(string path)
    {
        try
        {
            var attr = File.GetAttributes(path);
            if ((attr & FileAttributes.Directory) != 0)
                Process.Start(new ProcessStartInfo(path) { UseShellExecute = true });
            else
                Process.Start("explorer.exe", $"/select,\"{path}\"");
        }
        catch { }
    }

    private static void ClearAll()
    {
        var recent = Environment.GetFolderPath(Environment.SpecialFolder.Recent);
        if (!Directory.Exists(recent)) return;
        foreach (var lnk in Directory.EnumerateFiles(recent, "*.lnk"))
        {
            try { File.Delete(lnk); } catch { }
        }
    }
}
