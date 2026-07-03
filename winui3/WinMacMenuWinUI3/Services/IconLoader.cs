using System.Runtime.InteropServices;
using Microsoft.UI.Xaml.Controls;

namespace WinMacMenuWinUI3.Services;

/// <summary>
/// Loads icons from file paths or DLL/EXE resource strings (e.g. "shell32.dll,-271")
/// and returns a WinUI3 IconElement ready to assign to MenuFlyoutItem.Icon.
/// Results are cached by path so each unique icon is only extracted once per session.
/// </summary>
public static class IconLoader
{
    // ── Win32 imports ────────────────────────────────────────────────────────

    // PrivateExtractIcons supports negative resource-ID indices (e.g. -271)
    // unlike ExtractIconEx which only accepts sequential indices.
    [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern uint PrivateExtractIcons(
        string lpszFile, int nIconIndex, int cxIcon, int cyIcon,
        IntPtr[] phicon, uint[] piconid, uint nIcons, uint flags);

    [DllImport("user32.dll")]
    private static extern bool DestroyIcon(IntPtr hIcon);

    // ── Cache: icon path string → temp PNG path ───────────────────────────

    private static readonly Dictionary<string, string?> _cache = new(StringComparer.OrdinalIgnoreCase);

    // ── Public API ────────────────────────────────────────────────────────

    /// <summary>
    /// Returns a BitmapIcon for the given icon path, or null if it can't be loaded.
    /// </summary>
    public static IconElement? Load(string? iconPath)
    {
        if (string.IsNullOrWhiteSpace(iconPath)) return null;

        try
        {
            if (!_cache.TryGetValue(iconPath, out var pngPath))
            {
                pngPath = Resolve(iconPath);
                _cache[iconPath] = pngPath;
            }

            if (pngPath == null) return null;

            return new BitmapIcon
            {
                UriSource       = new Uri(pngPath),
                ShowAsMonochrome = false,
            };
        }
        catch { return null; }
    }

    // ── Resolution logic ─────────────────────────────────────────────────

    private static string? Resolve(string iconPath)
    {
        // DLL/EXE resource: "shell32.dll,-271"  or  "C:\foo\bar.exe,0"
        var comma = iconPath.LastIndexOf(',');
        if (comma > 0 && int.TryParse(iconPath[(comma + 1)..].Trim(), out var index))
        {
            var file = ExpandAndLocate(iconPath[..comma].Trim());
            if (file == null) return null;
            return ExtractFromResource(file, index);
        }

        // Direct file (ico / png / jpg / bmp / exe / dll without index)
        var expanded = ExpandAndLocate(iconPath);
        if (expanded == null) return null;

        var ext = Path.GetExtension(expanded).ToLowerInvariant();
        if (ext is ".ico" or ".png" or ".jpg" or ".jpeg" or ".bmp")
            return expanded; // BitmapImage can handle these directly

        // EXE or other binary — extract associated icon
        return ExtractAssociated(expanded);
    }

    private static string? ExpandAndLocate(string path)
    {
        path = Environment.ExpandEnvironmentVariables(path);

        if (File.Exists(path)) return path;

        // Relative paths: try System32 (covers shell32.dll, imageres.dll, etc.)
        if (!Path.IsPathRooted(path))
        {
            var sys32 = Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.System), path);
            if (File.Exists(sys32)) return sys32;
        }

        return null;
    }

    private static string? ExtractFromResource(string file, int index)
    {
        // Icon size: 16×16 for standard menu icons
        const int size = 16;
        var phicon  = new IntPtr[1];
        var piconid = new uint[1];

        uint count = PrivateExtractIcons(file, index, size, size, phicon, piconid, 1, 0);
        if (count == 0 || phicon[0] == IntPtr.Zero) return null;

        try   { return HIconToPng(phicon[0]); }
        finally { DestroyIcon(phicon[0]); }
    }

    private static string? ExtractAssociated(string filePath)
    {
        using var icon = System.Drawing.Icon.ExtractAssociatedIcon(filePath);
        if (icon == null) return null;
        return HIconToPng(icon.Handle);
    }

    private static string? HIconToPng(IntPtr hIcon)
    {
        // Clone via System.Drawing so we own the handle lifetime
        using var icon = (System.Drawing.Icon)System.Drawing.Icon.FromHandle(hIcon).Clone();
        using var bmp  = icon.ToBitmap();

        var tmp = Path.Combine(Path.GetTempPath(), $"wmm_{Guid.NewGuid():N}.png");
        bmp.Save(tmp, System.Drawing.Imaging.ImageFormat.Png);
        return tmp;
    }
}
