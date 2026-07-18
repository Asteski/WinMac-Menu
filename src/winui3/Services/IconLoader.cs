using System.Runtime.InteropServices;
using System.Globalization;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;

namespace WinMacMenuWinUI3.Services;

public static class IconLoader
{
    // ── Win32 imports ─────────────────────────────────────────────────────

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern uint PrivateExtractIcons(
        string lpszFile, int nIconIndex, int cxIcon, int cyIcon,
        IntPtr[] phicon, uint[] piconid, uint nIcons, uint flags);

    [DllImport("user32.dll")]
    private static extern bool DestroyIcon(IntPtr hIcon);

    // SHGetFileInfo: retrieves the shell icon for any path — file, folder, or drive
    [DllImport("shell32.dll", CharSet = CharSet.Unicode)]
    private static extern IntPtr SHGetFileInfo(
        string pszPath, uint dwFileAttributes,
        ref SHFILEINFO psfi, uint cbSFI, uint uFlags);

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    private struct SHFILEINFO
    {
        public IntPtr hIcon;
        public int    iIcon;
        public uint   dwAttributes;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 260)] public string szDisplayName;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst =  80)] public string szTypeName;
    }

    private const uint SHGFI_ICON      = 0x000000100;
    private const uint SHGFI_SMALLICON = 0x000000001;
    private const uint SHGFI_USEFILEATTRIBUTES = 0x000000010;
    private const uint FILE_ATTRIBUTE_NORMAL = 0x00000080;
    private const uint FILE_ATTRIBUTE_DIRECTORY = 0x00000010;

    // ── Cache ─────────────────────────────────────────────────────────────

    private static readonly Dictionary<string, string?> _cache =
        new(StringComparer.OrdinalIgnoreCase);

    // ── Public API ────────────────────────────────────────────────────────

    public static IconElement? Load(string? iconPath, bool iconOnly = false)
    {
        if (string.IsNullOrWhiteSpace(iconPath)) return null;
        try
        {
            if (TryParseFluentGlyph(iconPath, out var codepoint))
            {
                return new FontIcon
                {
                    FontFamily = new FontFamily("Segoe Fluent Icons"),
                    Glyph = char.ConvertFromUtf32(codepoint)
                };
            }
            var cacheKey = iconOnly ? $"icon:{iconPath}" : $"default:{iconPath}";
            if (!_cache.TryGetValue(cacheKey, out var pngPath))
            {
                pngPath = Resolve(iconPath, iconOnly);
                _cache[cacheKey] = pngPath;
            }
            if (pngPath == null) return null;
            return new BitmapIcon { UriSource = new Uri(pngPath), ShowAsMonochrome = false };
        }
        catch { return null; }
    }

    private static bool TryParseFluentGlyph(string value, out int codepoint)
    {
        codepoint = 0;
        var text = value.Trim();
        string hex;

        if (text.StartsWith("\\u", StringComparison.OrdinalIgnoreCase))
            hex = text[2..];
        else if (text.StartsWith("&#x", StringComparison.OrdinalIgnoreCase))
            hex = text[3..].TrimEnd(';');
        else if (text.StartsWith('#'))
            hex = text[1..];
        else
            return false;

        if (hex.Length is < 4 or > 6 ||
            !int.TryParse(hex, NumberStyles.AllowHexSpecifier, CultureInfo.InvariantCulture, out codepoint))
            return false;

        return codepoint is > 0 and <= 0x10FFFF && codepoint is not (>= 0xD800 and <= 0xDFFF);
    }

    // ── Resolution ────────────────────────────────────────────────────────

    private static string? Resolve(string iconPath, bool iconOnly)
    {
        // DLL/EXE resource: "shell32.dll,-271"  or  "C:\foo\bar.exe,0"
        var comma = iconPath.LastIndexOf(',');
        if (comma > 0 && int.TryParse(iconPath[(comma + 1)..].Trim(), out var index))
        {
            var file = LocateFile(iconPath[..comma].Trim());
            return file == null ? null : ExtractFromResource(file, index);
        }

        var expanded = Environment.ExpandEnvironmentVariables(iconPath);

        // Image formats BitmapImage can load directly
        var ext = Path.GetExtension(expanded).ToLowerInvariant();
        if (!iconOnly && ext is ".ico" or ".png" or ".jpg" or ".jpeg" or ".bmp" && File.Exists(expanded))
            return expanded;

        // For any filesystem path (file or directory) use SHGetFileInfo —
        // it returns the correct shell icon regardless of whether it's a
        // file, folder, drive root, or special shell location.
        if (File.Exists(expanded) || Directory.Exists(expanded))
            return ShellIcon(expanded, iconOnly);

        return null;
    }

    // Resolves a bare filename against System32 (for shell32.dll, imageres.dll, etc.)
    private static string? LocateFile(string path)
    {
        path = Environment.ExpandEnvironmentVariables(path);
        if (File.Exists(path)) return path;
        if (!Path.IsPathRooted(path))
        {
            var sys = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.System), path);
            if (File.Exists(sys)) return sys;
        }
        return null;
    }

    private static string? ShellIcon(string path, bool iconOnly)
    {
        var shfi = new SHFILEINFO();
        uint flags = SHGFI_ICON | SHGFI_SMALLICON;
        uint attributes = 0;
        if (iconOnly)
        {
            flags |= SHGFI_USEFILEATTRIBUTES;
            attributes = Directory.Exists(path) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
        }
        var res  = SHGetFileInfo(path, attributes, ref shfi, (uint)Marshal.SizeOf(shfi),
                                 flags);
        if (res == IntPtr.Zero || shfi.hIcon == IntPtr.Zero) return null;
        try   { return HIconToPng(shfi.hIcon); }
        finally { DestroyIcon(shfi.hIcon); }
    }

    private static string? ExtractFromResource(string file, int index)
    {
        var phicon  = new IntPtr[1];
        var piconid = new uint[1];
        uint count  = PrivateExtractIcons(file, index, 16, 16, phicon, piconid, 1, 0);
        if (count == 0 || phicon[0] == IntPtr.Zero) return null;
        try   { return HIconToPng(phicon[0]); }
        finally { DestroyIcon(phicon[0]); }
    }

    private static string? HIconToPng(IntPtr hIcon)
    {
        using var icon = (System.Drawing.Icon)System.Drawing.Icon.FromHandle(hIcon).Clone();
        using var bmp  = icon.ToBitmap();
        var tmp = Path.Combine(Path.GetTempPath(), $"wmm_{Guid.NewGuid():N}.png");
        bmp.Save(tmp, System.Drawing.Imaging.ImageFormat.Png);
        return tmp;
    }
}
