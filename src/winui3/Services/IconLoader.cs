using System.Runtime.InteropServices;
using System.Globalization;
using System.Security.Cryptography;
using System.Text;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Microsoft.UI.Xaml.Media.Imaging;

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

    [DllImport("user32.dll")]
    private static extern bool EnumChildWindows(IntPtr hWndParent, EnumWindowsProc lpEnumFunc, IntPtr lParam);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern int GetClassName(IntPtr hWnd, System.Text.StringBuilder lpClassName, int nMaxCount);

    [DllImport("user32.dll", EntryPoint = "GetClassLongPtrW")]
    private static extern IntPtr GetClassLongPtr(IntPtr hWnd, int nIndex);

    [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern IntPtr SendMessageTimeout(
        IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam,
        uint flags, uint timeout, out IntPtr result);

    [DllImport("shell32.dll", CharSet = CharSet.Unicode)]
    private static extern int SHCreateItemInKnownFolder(ref Guid kfid, uint flags, string item, ref Guid riid, out IShellItemImageFactory imageFactory);

    [DllImport("shell32.dll", CharSet = CharSet.Unicode)]
    private static extern int SHCreateItemFromParsingName(
        string path, IntPtr bindContext, ref Guid riid, out IShellItemImageFactory imageFactory);

    [DllImport("ole32.dll")]
    private static extern int CoInitializeEx(IntPtr pvReserved, uint dwCoInit);

    [DllImport("ole32.dll")]
    private static extern void CoUninitialize();

    [DllImport("gdi32.dll")]
    private static extern bool DeleteObject(IntPtr hObject);

    [DllImport("gdi32.dll")]
    private static extern int GetObject(IntPtr hgdiobj, int cbBuffer, out BITMAP lpvObject);

    [DllImport("gdi32.dll")]
    private static extern int GetDIBits(
        IntPtr hdc, IntPtr hbm, uint startScan, uint scanLines,
        byte[] bits, ref BITMAPINFO info, uint usage);

    [DllImport("user32.dll")]
    private static extern IntPtr GetDC(IntPtr hWnd);

    [DllImport("user32.dll")]
    private static extern int ReleaseDC(IntPtr hWnd, IntPtr hDC);

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
    private const uint WM_GETICON = 0x007F;
    private const int ICON_SMALL = 0;
    private const int ICON_BIG = 1;
    private const int ICON_SMALL2 = 2;
    private const int GCLP_HICON = -14;
    private const int GCLP_HICONSM = -34;
    private const uint SMTO_ABORTIFHUNG = 0x0002;
    private const uint SIIGBF_ICONONLY = 0x00000004;
    // Icon resolution runs on thread-pool threads, which are normally MTA.
    // Using MTA here avoids RPC_E_CHANGED_MODE and intermittent shell failures.
    private const uint COINIT_MULTITHREADED = 0x0;
    private const uint BI_RGB = 0;
    private const uint DIB_RGB_COLORS = 0;
    private const int S_OK = 0;
    private const int S_FALSE = 1;
    private static readonly Guid IID_IShellItemImageFactory = new("BCC18B79-BA16-442F-80C4-8A59C30C463B");
    private static readonly Guid FOLDERID_AppsFolder = new("1E87508D-89C2-42F0-8A7E-645A0F50CA58");

    private delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);

    // ── Cache ─────────────────────────────────────────────────────────────

    private static readonly Dictionary<string, string> _cache =
        new(StringComparer.OrdinalIgnoreCase);
    private static readonly object _cacheLock = new();
    private static readonly string CacheDirectory =
        Path.Combine(Path.GetTempPath(), "WinMacMenu", "IconCache");
    private static readonly TimeSpan CacheRetention = TimeSpan.FromDays(7);
    private const int MaxCacheFiles = 2048;
    // UI-thread only. Share decoded sources, never visual elements.
    private static readonly Dictionary<string, BitmapImage> ImageSources = new(StringComparer.OrdinalIgnoreCase);
    private static readonly Queue<(string Key, BitmapImage Source)> ImageSourceOrder = new();

    public static IconElement CreateImageIcon(Uri uri)
    {
        var key = uri.AbsoluteUri;
        if (!ImageSources.TryGetValue(key, out var source))
        {
            source = new BitmapImage();
            ImageSources.Add(key, source);
            ImageSourceOrder.Enqueue((key, source));
            source.ImageFailed += (_, _) =>
            {
                if (ImageSources.TryGetValue(key, out var current) && ReferenceEquals(current, source))
                    ImageSources.Remove(key);
            };
            source.UriSource = uri;
            while (ImageSourceOrder.Count > 512)
            {
                var oldest = ImageSourceOrder.Dequeue();
                if (ImageSources.TryGetValue(oldest.Key, out var current) && ReferenceEquals(current, oldest.Source))
                    ImageSources.Remove(oldest.Key);
            }
        }
        return new ImageIcon { Source = source };
    }

    static IconLoader()
    {
        CleanupStaleCacheFiles();
    }

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

            var uri = ResolveUri(iconPath, iconOnly);
            return uri == null ? null : CreateImageIcon(uri);
        }
        catch { return null; }
    }

    // UI fast path: never consult the filesystem or shell while constructing a menu.
    public static Uri? GetMemoryCachedUri(string iconPath, bool iconOnly = false)
    {
        var cacheKey = iconOnly ? $"icon:{iconPath}" : $"default:{iconPath}";
        lock (_cacheLock)
            return _cache.TryGetValue(cacheKey, out var path) ? new Uri(path) : null;
    }

    public static Uri? ResolveUri(string? iconPath, bool iconOnly = false)
    {
        if (string.IsNullOrWhiteSpace(iconPath)) return null;
        try
        {
            var cacheKey = iconOnly ? $"icon:{iconPath}" : $"default:{iconPath}";
            if (TryGetCachedUri(cacheKey, out var cachedUri))
                return cachedUri;

            var pngPath = Resolve(iconPath, iconOnly);
            // Do not cache transient shell failures. A failed lookup must be
            // allowed to retry when the menu is opened again.
            if (pngPath != null) StoreCachedPath(cacheKey, pngPath);

            return pngPath == null ? null : new Uri(pngPath);
        }
        catch { return null; }
    }

    public static IconElement? LoadWindowIcon(IntPtr hWnd)
    {
        if (hWnd == IntPtr.Zero) return null;
        try
        {
            var pngPath = WindowIcon(hWnd);
            if (pngPath == null) return null;
            return CreateImageIcon(new Uri(pngPath));
        }
        catch { return null; }
    }

    public static IconElement? LoadAppUserModelIcon(string? appUserModelId)
    {
        if (string.IsNullOrWhiteSpace(appUserModelId)) return null;
        try
        {
            var cacheKey = $"aumid:{appUserModelId}";
            if (TryGetCachedUri(cacheKey, out var cachedUri))
                return CreateImageIcon(cachedUri!);

            var pngPath = AppUserModelIcon(appUserModelId);
            if (pngPath != null) StoreCachedPath(cacheKey, pngPath);
            if (pngPath == null) return null;
            return CreateImageIcon(new Uri(pngPath));
        }
        catch { return null; }
    }

    public static IconElement? LoadKnownHostedAppIcon(string? displayName)
    {
        if (string.IsNullOrWhiteSpace(displayName)) return null;

        foreach (var appUserModelId in GetKnownHostedAppIds(displayName))
        {
            var icon = LoadAppUserModelIcon(appUserModelId);
            if (icon != null) return icon;
        }

        return null;
    }

    internal static bool TryParseFluentGlyph(string value, out int codepoint)
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

    private static IEnumerable<string> GetKnownHostedAppIds(string displayName)
    {
        var name = displayName.Trim();
        if (string.Equals(name, "Settings", StringComparison.OrdinalIgnoreCase) ||
            string.Equals(name, "System Settings", StringComparison.OrdinalIgnoreCase))
        {
            yield return "windows.immersivecontrolpanel_cw5n1h2txyewy!microsoft.windows.immersivecontrolpanel";
            yield return "Windows.ImmersiveControlPanel_cw5n1h2txyewy!Microsoft.Windows.ImmersiveControlPanel";
        }
        else if (string.Equals(name, "XBOX", StringComparison.OrdinalIgnoreCase) ||
                 string.Equals(name, "Xbox", StringComparison.OrdinalIgnoreCase))
        {
            yield return "Microsoft.GamingApp_8wekyb3d8bbwe!Microsoft.Xbox.App";
            yield return "Microsoft.GamingApp_8wekyb3d8bbwe!Microsoft.GamingApp";
            yield return "Microsoft.XboxApp_8wekyb3d8bbwe!Microsoft.XboxApp";
        }
        else if (string.Equals(name, "About Windows", StringComparison.OrdinalIgnoreCase) ||
                 string.Equals(name, "Winver UWP", StringComparison.OrdinalIgnoreCase))
        {
            yield return "2505FireCubeStudios.WinverUWP_k45w5yt88e21j!App";
        }
        else if (string.Equals(name, "Windows Security", StringComparison.OrdinalIgnoreCase))
        {
            yield return "Microsoft.SecHealthUI_8wekyb3d8bbwe!SecHealthUI";
        }
        else if (string.Equals(name, "Camera", StringComparison.OrdinalIgnoreCase) ||
                 string.Equals(name, "Windows Camera", StringComparison.OrdinalIgnoreCase))
        {
            yield return "Microsoft.WindowsCamera_8wekyb3d8bbwe!App";
        }
        else if (string.Equals(name, "Photos", StringComparison.OrdinalIgnoreCase) ||
                 string.Equals(name, "Microsoft Photos", StringComparison.OrdinalIgnoreCase))
        {
            yield return "Microsoft.Windows.Photos_8wekyb3d8bbwe!App";
        }
        else if (string.Equals(name, "Sound Recorder", StringComparison.OrdinalIgnoreCase) ||
                 string.Equals(name, "Windows Sound Recorder", StringComparison.OrdinalIgnoreCase) ||
                 string.Equals(name, "Voice Recorder", StringComparison.OrdinalIgnoreCase))
        {
            yield return "Microsoft.WindowsSoundRecorder_8wekyb3d8bbwe!App";
        }
        else if (string.Equals(name, "Microsoft Store", StringComparison.OrdinalIgnoreCase) ||
                 string.Equals(name, "Store", StringComparison.OrdinalIgnoreCase))
        {
            yield return "Microsoft.WindowsStore_8wekyb3d8bbwe!App";
        }
        else if (string.Equals(name, "Microsoft To Do", StringComparison.OrdinalIgnoreCase) ||
                 string.Equals(name, "To Do", StringComparison.OrdinalIgnoreCase))
        {
            yield return "Microsoft.Todos_8wekyb3d8bbwe!App";
        }
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

        // Use the same path-based shell lookup as the Win32 menu first. This
        // preserves desktop.ini/custom folder icons and special folder icons.
        // The image factory remains a fallback for namespace-only folders.
        if (Directory.Exists(expanded))
            return ShellIcon(expanded, iconOnly: false) ?? ShellItemIcon(expanded);

        if (File.Exists(expanded))
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
        var cacheKey = CreatePathCacheKey("shell", path, iconOnly);
        if (TryGetCachedPath(cacheKey, out var cachedPath))
            return cachedPath;

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
        try   { return HIconToPng(shfi.hIcon, cacheKey); }
        finally { DestroyIcon(shfi.hIcon); }
    }

    private static string? ShellItemIcon(string path)
    {
        var cacheKey = CreatePathCacheKey("shell-item", path, iconOnly: false);
        if (TryGetCachedPath(cacheKey, out var cachedPath))
            return cachedPath;

        var coInit = CoInitializeEx(IntPtr.Zero, COINIT_MULTITHREADED);
        var shouldUninitialize = coInit is S_OK or S_FALSE;
        IShellItemImageFactory? imageFactory = null;
        try
        {
            var iid = IID_IShellItemImageFactory;
            if (SHCreateItemFromParsingName(path, IntPtr.Zero, ref iid, out imageFactory) != 0 || imageFactory == null)
                return null;

            var size = new SIZE { cx = 16, cy = 16 };
            if (imageFactory.GetImage(size, SIIGBF_ICONONLY, out var hBitmap) != 0 || hBitmap == IntPtr.Zero)
                return null;

            try { return HBitmapToPng(hBitmap, cacheKey); }
            finally { DeleteObject(hBitmap); }
        }
        catch { return null; }
        finally
        {
            if (imageFactory != null) Marshal.ReleaseComObject(imageFactory);
            if (shouldUninitialize) CoUninitialize();
        }
    }

    private static string? ExtractFromResource(string file, int index)
    {
        var cacheKey = CreateResourceCacheKey(file, index);
        if (TryGetCachedPath(cacheKey, out var cachedPath))
            return cachedPath;

        var phicon  = new IntPtr[1];
        var piconid = new uint[1];
        uint count  = PrivateExtractIcons(file, index, 16, 16, phicon, piconid, 1, 0);
        if (count == 0 || phicon[0] == IntPtr.Zero) return null;
        try   { return HIconToPng(phicon[0], cacheKey); }
        finally { DestroyIcon(phicon[0]); }
    }

    private static string? WindowIcon(IntPtr hWnd)
    {
        var hIcon = IntPtr.Zero;
        if (string.Equals(GetWindowClass(hWnd), "ApplicationFrameWindow", StringComparison.OrdinalIgnoreCase))
            hIcon = FindChildIcon(hWnd);

        if (hIcon == IntPtr.Zero)
            hIcon = GetIconFromWindow(hWnd);
        if (hIcon == IntPtr.Zero)
            hIcon = FindChildIcon(hWnd);

        return hIcon == IntPtr.Zero ? null : HIconToPng(hIcon, $"window:{hWnd}");
    }

    private static string? AppUserModelIcon(string appUserModelId)
    {
        var cacheKey = $"aumid:{appUserModelId}";
        if (TryGetCachedPath(cacheKey, out var cachedPath))
            return cachedPath;

        var coInit = CoInitializeEx(IntPtr.Zero, COINIT_MULTITHREADED);
        var shouldUninitialize = coInit is S_OK or S_FALSE;
        IShellItemImageFactory? imageFactory = null;
        try
        {
            var folder = FOLDERID_AppsFolder;
            var iid = IID_IShellItemImageFactory;
            if (SHCreateItemInKnownFolder(ref folder, 0, appUserModelId, ref iid, out imageFactory) != 0 || imageFactory == null)
                return null;

            var size = new SIZE { cx = 16, cy = 16 };
            if (imageFactory.GetImage(size, SIIGBF_ICONONLY, out var hBitmap) != 0 || hBitmap == IntPtr.Zero)
                return null;

            try { return HBitmapToPng(hBitmap, cacheKey); }
            finally { DeleteObject(hBitmap); }
        }
        catch { return null; }
        finally
        {
            if (imageFactory != null) Marshal.ReleaseComObject(imageFactory);
            if (shouldUninitialize) CoUninitialize();
        }
    }

    private static IntPtr FindChildIcon(IntPtr parent)
    {
        var best = IntPtr.Zero;
        EnumChildWindows(parent, (child, _) =>
        {
            best = GetIconFromWindow(child);
            return best == IntPtr.Zero;
        }, IntPtr.Zero);
        return best;
    }

    private static IntPtr GetIconFromWindow(IntPtr hWnd)
    {
        var hIcon = SendGetIcon(hWnd, ICON_SMALL2);
        if (hIcon == IntPtr.Zero) hIcon = SendGetIcon(hWnd, ICON_SMALL);
        if (hIcon == IntPtr.Zero) hIcon = SendGetIcon(hWnd, ICON_BIG);
        if (hIcon == IntPtr.Zero) hIcon = GetClassLongPtr(hWnd, GCLP_HICONSM);
        if (hIcon == IntPtr.Zero) hIcon = GetClassLongPtr(hWnd, GCLP_HICON);
        return hIcon;
    }

    private static IntPtr SendGetIcon(IntPtr hWnd, int iconKind)
    {
        var ok = SendMessageTimeout(hWnd, WM_GETICON, new IntPtr(iconKind), IntPtr.Zero,
            SMTO_ABORTIFHUNG, 100, out var result);
        return ok == IntPtr.Zero ? IntPtr.Zero : result;
    }

    private static string GetWindowClass(IntPtr hWnd)
    {
        var sb = new System.Text.StringBuilder(256);
        return GetClassName(hWnd, sb, sb.Capacity) > 0 ? sb.ToString() : "";
    }

    private static bool TryGetCachedUri(string cacheKey, out Uri? uri)
    {
        uri = null;
        if (!TryGetCachedPath(cacheKey, out var path))
            return false;

        uri = new Uri(path);
        return true;
    }

    private static bool TryGetCachedPath(string cacheKey, out string path)
    {
        lock (_cacheLock)
        {
            if (_cache.TryGetValue(cacheKey, out path!) && File.Exists(path))
                return true;

            if (path != null)
                _cache.Remove(cacheKey);
        }

        path = GetCachePath(cacheKey);
        if (!File.Exists(path))
            return false;

        try { File.SetLastAccessTimeUtc(path, DateTime.UtcNow); } catch { }
        StoreCachedPath(cacheKey, path);
        return true;
    }

    private static void StoreCachedPath(string cacheKey, string path)
    {
        lock (_cacheLock)
        {
            // Bound process-lifetime metadata even when browsing many folders.
            if (_cache.Count >= 1024 && !_cache.ContainsKey(cacheKey))
                _cache.Remove(_cache.Keys.First());
            _cache[cacheKey] = path;
        }
    }

    private static string? HIconToPng(IntPtr hIcon, string cacheKey)
    {
        using var icon = (System.Drawing.Icon)System.Drawing.Icon.FromHandle(hIcon).Clone();
        using var bmp  = icon.ToBitmap();
        var path = GetCachePath(cacheKey);
        SaveBitmapToCache(bmp, path);
        return path;
    }

    private static string HBitmapToPng(IntPtr hBitmap, string cacheKey)
    {
        if (TryHBitmapToPngWithAlpha(hBitmap, cacheKey, out var alphaPath))
            return alphaPath;

        using var bmp = System.Drawing.Image.FromHbitmap(hBitmap);
        var path = GetCachePath(cacheKey);
        SaveBitmapToCache(bmp, path);
        return path;
    }

    private static bool TryHBitmapToPngWithAlpha(IntPtr hBitmap, string cacheKey, out string pngPath)
    {
        pngPath = "";
        if (GetObject(hBitmap, Marshal.SizeOf<BITMAP>(), out var source) == 0)
            return false;

        var width = Math.Abs(source.bmWidth);
        var height = Math.Abs(source.bmHeight);
        if (width <= 0 || height <= 0)
            return false;

        var info = new BITMAPINFO
        {
            bmiHeader = new BITMAPINFOHEADER
            {
                biSize = (uint)Marshal.SizeOf<BITMAPINFOHEADER>(),
                biWidth = width,
                biHeight = -height,
                biPlanes = 1,
                biBitCount = 32,
                biCompression = BI_RGB
            }
        };

        var pixels = new byte[checked(width * height * 4)];
        var hdc = GetDC(IntPtr.Zero);
        if (hdc == IntPtr.Zero)
            return false;

        try
        {
            if (GetDIBits(hdc, hBitmap, 0, (uint)height, pixels, ref info, DIB_RGB_COLORS) == 0)
                return false;
        }
        finally
        {
            ReleaseDC(IntPtr.Zero, hdc);
        }

        var anyAlpha = false;
        var anyTransparent = false;
        for (var i = 3; i < pixels.Length; i += 4)
        {
            if (pixels[i] > 0) anyAlpha = true;
            if (pixels[i] < 255) anyTransparent = true;
        }
        if (!anyAlpha || !anyTransparent)
            return false;

        // GetDIBits returns straight BGRA pixels. Format32bppArgb keeps those
        // values intact; treating them as premultiplied can distort translucent
        // shell icons when WinUI decodes the generated PNG.
        using var bmp = new System.Drawing.Bitmap(
            width, height, System.Drawing.Imaging.PixelFormat.Format32bppArgb);
        var rect = new System.Drawing.Rectangle(0, 0, width, height);
        var data = bmp.LockBits(rect, System.Drawing.Imaging.ImageLockMode.WriteOnly,
            System.Drawing.Imaging.PixelFormat.Format32bppPArgb);
        try
        {
            Marshal.Copy(pixels, 0, data.Scan0, pixels.Length);
        }
        finally
        {
            bmp.UnlockBits(data);
        }

        pngPath = GetCachePath(cacheKey);
        SaveBitmapToCache(bmp, pngPath);
        return true;
    }

    private static string CreatePathCacheKey(string kind, string path, bool iconOnly)
    {
        var expanded = Environment.ExpandEnvironmentVariables(path);
        var fullPath = expanded;
        try { fullPath = Path.GetFullPath(expanded); } catch { }

        var stamp = "";
        try
        {
            if (File.Exists(fullPath))
            {
                var file = new FileInfo(fullPath);
                stamp = $"{file.LastWriteTimeUtc.Ticks}:{file.Length}";
            }
            else if (Directory.Exists(fullPath))
            {
                stamp = Directory.GetLastWriteTimeUtc(fullPath).Ticks.ToString(CultureInfo.InvariantCulture);
                var desktopIni = Path.Combine(fullPath, "desktop.ini");
                if (File.Exists(desktopIni))
                    stamp += ":" + File.GetLastWriteTimeUtc(desktopIni).Ticks.ToString(CultureInfo.InvariantCulture);
            }
        }
        catch { }

        return $"{kind}:{iconOnly}:{fullPath}:{stamp}";
    }

    private static string CreateResourceCacheKey(string file, int index)
    {
        var fullPath = file;
        try { fullPath = Path.GetFullPath(file); } catch { }
        var stamp = "";
        try
        {
            var info = new FileInfo(fullPath);
            if (info.Exists)
                stamp = $"{info.LastWriteTimeUtc.Ticks}:{info.Length}";
        }
        catch { }
        return $"resource:{fullPath}:{index}:{stamp}";
    }

    private static string GetCachePath(string cacheKey)
    {
        Directory.CreateDirectory(CacheDirectory);
        var hash = Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(cacheKey))).ToLowerInvariant();
        return Path.Combine(CacheDirectory, hash + ".png");
    }

    private static void SaveBitmapToCache(System.Drawing.Image image, string path)
    {
        Directory.CreateDirectory(CacheDirectory);
        var tmp = path + "." + Guid.NewGuid().ToString("N") + ".tmp";
        image.Save(tmp, System.Drawing.Imaging.ImageFormat.Png);
        File.Move(tmp, path, overwrite: true);
    }

    private static void CleanupStaleCacheFiles()
    {
        try
        {
            var dir = new DirectoryInfo(CacheDirectory);
            if (!dir.Exists) return;

            var cutoff = DateTime.UtcNow - CacheRetention;
            var files = new List<FileInfo>();
            foreach (var file in dir.EnumerateFiles("*.png"))
            {
                try
                {
                    if (file.LastAccessTimeUtc < cutoff && file.LastWriteTimeUtc < cutoff)
                    {
                        file.Delete();
                        continue;
                    }

                    files.Add(file);
                }
                catch { }
            }

            if (files.Count <= MaxCacheFiles)
                return;

            foreach (var file in files
                         .OrderBy(f => f.LastAccessTimeUtc < f.LastWriteTimeUtc ? f.LastAccessTimeUtc : f.LastWriteTimeUtc)
                         .Take(files.Count - MaxCacheFiles))
            {
                try { file.Delete(); } catch { }
            }
        }
        catch { }
    }
}

[StructLayout(LayoutKind.Sequential)]
internal struct BITMAP
{
    public int bmType;
    public int bmWidth;
    public int bmHeight;
    public int bmWidthBytes;
    public ushort bmPlanes;
    public ushort bmBitsPixel;
    public IntPtr bmBits;
}

[StructLayout(LayoutKind.Sequential)]
internal struct BITMAPINFOHEADER
{
    public uint biSize;
    public int biWidth;
    public int biHeight;
    public ushort biPlanes;
    public ushort biBitCount;
    public uint biCompression;
    public uint biSizeImage;
    public int biXPelsPerMeter;
    public int biYPelsPerMeter;
    public uint biClrUsed;
    public uint biClrImportant;
}

[StructLayout(LayoutKind.Sequential)]
internal struct BITMAPINFO
{
    public BITMAPINFOHEADER bmiHeader;
    public uint bmiColors;
}

[StructLayout(LayoutKind.Sequential)]
internal struct SIZE
{
    public int cx;
    public int cy;
}

[ComImport, Guid("BCC18B79-BA16-442F-80C4-8A59C30C463B"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
internal interface IShellItemImageFactory
{
    [PreserveSig] int GetImage(SIZE size, uint flags, out IntPtr phbm);
}
