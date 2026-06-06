using WinMacMenu.Models;

namespace WinMacMenu.Services;

/// <summary>Enumerates folder contents into submenu entries, with the C app's filtering and sorting.</summary>
public static class FolderProvider
{
    public static IReadOnlyList<DynamicEntry> Enumerate(string path, Config cfg, int depth)
    {
        var entries = new List<DynamicEntry>();
        if (string.IsNullOrEmpty(path) || !Directory.Exists(path))
            return entries;

        List<FileSystemInfo> infos;
        try
        {
            var di = new DirectoryInfo(path);
            infos = di.EnumerateFileSystemInfos().Where(fsi => IsVisible(fsi, cfg)).ToList();
        }
        catch
        {
            return entries;
        }

        Sort(infos, cfg);

        int max = cfg.MaxItems > 0 ? cfg.MaxItems : int.MaxValue;
        bool canRecurse = depth < cfg.FolderMaxDepth;

        if (cfg.FolderShowOpenEntry && cfg.FolderSingleClickOpen)
        {
            var folderPath = path;
            entries.Add(new DynamicEntry
            {
                Label = $"Open {Path.GetFileName(path.TrimEnd(Path.DirectorySeparatorChar))}",
                Path = folderPath,
                ShowIcon = cfg.ShowFolderIcons,
                IsFolder = true,
            });
            entries.Add(DynamicEntry.Separator);
        }

        foreach (var fsi in infos)
        {
            if (entries.Count >= max) break;
            bool isDir = (fsi.Attributes & FileAttributes.Directory) != 0;
            string label = isDir ? fsi.Name
                : (cfg.ShowExtensions ? fsi.Name : Path.GetFileNameWithoutExtension(fsi.Name));

            if (isDir && canRecurse)
            {
                var childPath = fsi.FullName;
                entries.Add(new DynamicEntry
                {
                    Label = label,
                    Path = childPath,
                    IsFolder = true,
                    ShowIcon = cfg.ShowFolderIcons,
                    Children = () => Enumerate(childPath, cfg, depth + 1),
                });
            }
            else
            {
                entries.Add(new DynamicEntry
                {
                    Label = label,
                    Path = fsi.FullName,
                    IsFolder = isDir,
                    ShowIcon = isDir ? cfg.ShowFolderIcons : cfg.ShowFileIcons,
                });
            }
        }

        return entries;
    }

    private static bool IsVisible(FileSystemInfo fsi, Config cfg)
    {
        bool isDir = (fsi.Attributes & FileAttributes.Directory) != 0;
        if (!cfg.ShowHidden && (fsi.Attributes & FileAttributes.Hidden) != 0) return false;
        if ((fsi.Attributes & FileAttributes.System) != 0 && !cfg.ShowHidden) return false;

        if (fsi.Name.StartsWith('.'))
        {
            // DotMode: 0 none, 1 files only, 2 folders only, 3 both.
            return cfg.DotMode switch
            {
                3 => true,
                1 => !isDir,
                2 => isDir,
                _ => false,
            };
        }
        return true;
    }

    private static void Sort(List<FileSystemInfo> infos, Config cfg)
    {
        Comparison<FileSystemInfo> cmp = (a, b) =>
        {
            int byField = cfg.SortField switch
            {
                SortField.DateModified => a.LastWriteTimeUtc.CompareTo(b.LastWriteTimeUtc),
                SortField.DateCreated => a.CreationTimeUtc.CompareTo(b.CreationTimeUtc),
                SortField.Type => string.Compare(a.Extension, b.Extension, StringComparison.OrdinalIgnoreCase),
                SortField.Size => SizeOf(a).CompareTo(SizeOf(b)),
                _ => string.Compare(a.Name, b.Name, StringComparison.OrdinalIgnoreCase),
            };
            return cfg.SortDescending ? -byField : byField;
        };

        infos.Sort((a, b) =>
        {
            if (cfg.SortObjectPriority != SortObjectPriority.Disabled)
            {
                bool aDir = (a.Attributes & FileAttributes.Directory) != 0;
                bool bDir = (b.Attributes & FileAttributes.Directory) != 0;
                if (aDir != bDir)
                {
                    bool foldersFirst = cfg.SortObjectPriority == SortObjectPriority.FoldersFirst;
                    return (aDir == foldersFirst) ? -1 : 1;
                }
            }
            return cmp(a, b);
        });
    }

    private static long SizeOf(FileSystemInfo fsi)
        => fsi is FileInfo fi ? fi.Length : 0;
}
