using System.Drawing;
using System.Drawing.Imaging;
using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media.Imaging;
using Windows.Storage.Streams;
using WinMacMenu.Interop;

namespace WinMacMenu.Services;

/// <summary>
/// Resolves icon specifications into WinUI <see cref="IconElement"/>s.
/// Supports the C app's formats: a path to a .ico file, or "module,index" where a
/// negative index is a resource ID (e.g. <c>shell32.dll,-271</c>), plus live shell
/// icons for filesystem paths.
/// </summary>
public sealed class IconResolver
{
    private const int IconSize = 32;
    private readonly DispatcherQueue _dispatcher;

    public IconResolver(DispatcherQueue dispatcher) => _dispatcher = dispatcher;

    /// <summary>Builds an icon element from a config icon spec, or null when none/failed.</summary>
    public IconElement? FromSpec(string? spec)
    {
        if (string.IsNullOrWhiteSpace(spec))
            return null;

        nint hIcon = LoadHIcon(spec.Trim());
        return hIcon == 0 ? null : FromHIcon(hIcon, destroy: true);
    }

    /// <summary>Builds a shell icon element for a filesystem path (file or folder).</summary>
    public IconElement? FromPath(string path, bool isDirectory)
    {
        if (string.IsNullOrWhiteSpace(path))
            return null;

        var sfi = new NativeMethods.SHFILEINFO();
        uint attrs = isDirectory ? NativeMethods.FILE_ATTRIBUTE_DIRECTORY : NativeMethods.FILE_ATTRIBUTE_NORMAL;
        uint flags = NativeMethods.SHGFI_ICON | NativeMethods.SHGFI_SMALLICON | NativeMethods.SHGFI_USEFILEATTRIBUTES;
        // Real path (not USEFILEATTRIBUTES) for existing items gives the accurate icon.
        if (File.Exists(path) || Directory.Exists(path))
            flags &= ~NativeMethods.SHGFI_USEFILEATTRIBUTES;

        var res = NativeMethods.SHGetFileInfo(path, attrs, ref sfi, (uint)System.Runtime.InteropServices.Marshal.SizeOf(sfi), flags);
        if (res == 0 || sfi.hIcon == 0)
            return null;
        return FromHIcon(sfi.hIcon, destroy: true);
    }

    private static nint LoadHIcon(string spec)
    {
        // Split "module,index" — only when the trailing token is an integer.
        string file = spec;
        int index = 0;
        bool hasIndex = false;
        int comma = spec.LastIndexOf(',');
        if (comma > 0 && int.TryParse(spec[(comma + 1)..].Trim(), out index))
        {
            file = spec[..comma].Trim();
            hasIndex = true;
        }

        try
        {
            if (!hasIndex && file.EndsWith(".ico", StringComparison.OrdinalIgnoreCase))
            {
                // Load a standalone .ico file at the desired size.
                const uint LR_LOADFROMFILE = 0x00000010;
                nint name = System.Runtime.InteropServices.Marshal.StringToHGlobalUni(file);
                try
                {
                    nint h = NativeMethods.LoadImage(0, name, NativeMethods.IMAGE_ICON, IconSize, IconSize,
                        LR_LOADFROMFILE | NativeMethods.LR_DEFAULTCOLOR);
                    if (h != 0) return h;
                }
                finally
                {
                    System.Runtime.InteropServices.Marshal.FreeHGlobal(name);
                }
            }

            if (index < 0)
            {
                // Negative index = resource ID. Load the module as a data file and pull the icon by ID.
                nint mod = NativeMethods.LoadLibraryEx(file, 0, NativeMethods.LOAD_LIBRARY_AS_DATAFILE);
                if (mod != 0)
                {
                    try
                    {
                        nint resId = -index; // MAKEINTRESOURCE
                        nint h = NativeMethods.LoadImage(mod, resId, NativeMethods.IMAGE_ICON, IconSize, IconSize, NativeMethods.LR_DEFAULTCOLOR);
                        if (h != 0) return h;
                    }
                    finally
                    {
                        NativeMethods.FreeLibrary(mod);
                    }
                }
                return 0;
            }

            // Positive index = icon position within the module.
            int count = NativeMethods.ExtractIconEx(file, index, out nint large, out nint small, 1);
            if (count > 0)
            {
                if (small != 0)
                {
                    if (large != 0) NativeMethods.DestroyIcon(large);
                    return small;
                }
                if (large != 0) return large;
            }
        }
        catch
        {
            // fall through
        }
        return 0;
    }

    /// <summary>Converts an HICON to an <see cref="ImageIcon"/>, loading pixels asynchronously.</summary>
    private IconElement? FromHIcon(nint hIcon, bool destroy)
    {
        byte[]? png = null;
        try
        {
            using var icon = Icon.FromHandle(hIcon);
            using var bmp = icon.ToBitmap();
            using var ms = new MemoryStream();
            bmp.Save(ms, ImageFormat.Png);
            png = ms.ToArray();
        }
        catch
        {
            return null;
        }
        finally
        {
            if (destroy) NativeMethods.DestroyIcon(hIcon);
        }

        if (png is null || png.Length == 0)
            return null;

        var bitmap = new BitmapImage();
        var image = new ImageIcon { Source = bitmap };
        // Fire-and-forget: decode the PNG into the BitmapImage on the UI thread.
        _ = LoadPngAsync(bitmap, png);
        return image;
    }

    private async Task LoadPngAsync(BitmapImage bitmap, byte[] png)
    {
        try
        {
            using var stream = new InMemoryRandomAccessStream();
            using (var writer = new DataWriter(stream))
            {
                writer.WriteBytes(png);
                await writer.StoreAsync();
                await writer.FlushAsync();
                writer.DetachStream();
            }
            stream.Seek(0);
            await bitmap.SetSourceAsync(stream);
        }
        catch
        {
            // Leave the icon blank on failure.
        }
    }
}
