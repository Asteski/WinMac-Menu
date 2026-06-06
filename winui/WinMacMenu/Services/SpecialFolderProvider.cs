using WinMacMenu.Models;

namespace WinMacMenu.Services;

/// <summary>Builds the This PC and Home (user profile) listings, mirroring menu.c.</summary>
public static class SpecialFolderProvider
{
    public static IReadOnlyList<DynamicEntry> ThisPc(Config cfg)
    {
        var entries = new List<DynamicEntry>();

        // Known user folders shown at the top of This PC, like Explorer.
        AddKnownFolder(entries, cfg, Environment.SpecialFolder.DesktopDirectory, cfg.ThisPCShowIcons, cfg.ThisPCItemsAsSubmenus);
        AddKnownFolder(entries, cfg, Environment.SpecialFolder.MyDocuments, cfg.ThisPCShowIcons, cfg.ThisPCItemsAsSubmenus);
        AddKnownFolder(entries, cfg, GetDownloads(), cfg.ThisPCShowIcons, cfg.ThisPCItemsAsSubmenus);
        AddKnownFolder(entries, cfg, Environment.SpecialFolder.MyMusic, cfg.ThisPCShowIcons, cfg.ThisPCItemsAsSubmenus);
        AddKnownFolder(entries, cfg, Environment.SpecialFolder.MyPictures, cfg.ThisPCShowIcons, cfg.ThisPCItemsAsSubmenus);
        AddKnownFolder(entries, cfg, Environment.SpecialFolder.MyVideos, cfg.ThisPCShowIcons, cfg.ThisPCItemsAsSubmenus);

        if (entries.Count > 0)
            entries.Add(DynamicEntry.Separator);

        foreach (var drive in SafeDrives())
        {
            var root = drive.RootDirectory.FullName;
            string label = string.IsNullOrEmpty(drive.VolumeLabelSafe())
                ? root.TrimEnd('\\')
                : $"{drive.VolumeLabelSafe()} ({root.TrimEnd('\\')})";
            entries.Add(MakeFolderEntry(label, root, cfg, cfg.ThisPCShowIcons, cfg.ThisPCItemsAsSubmenus));
        }

        return entries;
    }

    public static IReadOnlyList<DynamicEntry> Home(Config cfg)
    {
        var home = Environment.GetFolderPath(Environment.SpecialFolder.UserProfile);
        if (cfg.HomeItemsAsSubmenus)
            return FolderProvider.Enumerate(home, cfg, depth: 1);

        // Flat listing that opens each item directly.
        var entries = new List<DynamicEntry>();
        if (!Directory.Exists(home)) return entries;
        try
        {
            foreach (var dir in Directory.EnumerateDirectories(home).OrderBy(d => d, StringComparer.OrdinalIgnoreCase))
                entries.Add(MakeFolderEntry(Path.GetFileName(dir), dir, cfg, cfg.HomeShowIcons, asSubmenu: false));
        }
        catch { }
        return entries;
    }

    private static void AddKnownFolder(List<DynamicEntry> entries, Config cfg, Environment.SpecialFolder folder, bool showIcon, bool asSubmenu)
    {
        var path = Environment.GetFolderPath(folder);
        AddKnownFolder(entries, cfg, path, showIcon, asSubmenu);
    }

    private static void AddKnownFolder(List<DynamicEntry> entries, Config cfg, string path, bool showIcon, bool asSubmenu)
    {
        if (string.IsNullOrEmpty(path) || !Directory.Exists(path)) return;
        entries.Add(MakeFolderEntry(Path.GetFileName(path.TrimEnd(Path.DirectorySeparatorChar)), path, cfg, showIcon, asSubmenu));
    }

    private static DynamicEntry MakeFolderEntry(string label, string path, Config cfg, bool showIcon, bool asSubmenu)
        => new()
        {
            Label = label,
            Path = path,
            IsFolder = true,
            ShowIcon = showIcon,
            Children = asSubmenu ? () => FolderProvider.Enumerate(path, cfg, depth: 1) : null,
        };

    private static string GetDownloads()
    {
        // No SpecialFolder enum value for Downloads; derive from the profile.
        var profile = Environment.GetFolderPath(Environment.SpecialFolder.UserProfile);
        return string.IsNullOrEmpty(profile) ? string.Empty : Path.Combine(profile, "Downloads");
    }

    private static IEnumerable<DriveInfo> SafeDrives()
    {
        try { return DriveInfo.GetDrives().Where(d => d.IsReady); }
        catch { return Array.Empty<DriveInfo>(); }
    }

    private static string VolumeLabelSafe(this DriveInfo d)
    {
        try { return d.VolumeLabel ?? string.Empty; } catch { return string.Empty; }
    }
}
