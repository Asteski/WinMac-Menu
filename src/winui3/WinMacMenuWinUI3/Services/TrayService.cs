using System.Runtime.InteropServices;
using WinMacMenuWinUI3.Models;

namespace WinMacMenuWinUI3.Services;

/// <summary>
/// Manages a system tray icon via Shell_NotifyIcon.
/// Subclasses the host window's HWND to receive tray callback messages
/// without replacing WinUI3's own WndProc.
/// </summary>
public sealed class TrayService : IDisposable
{
    // ── Shell_NotifyIcon ──────────────────────────────────────────────────

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    private struct NOTIFYICONDATA
    {
        public uint   cbSize;
        public IntPtr hWnd;
        public uint   uID;
        public uint   uFlags;
        public uint   uCallbackMessage;
        public IntPtr hIcon;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 128)] public string szTip;
        public uint   dwState;
        public uint   dwStateMask;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 256)] public string szInfo;
        public uint   uTimeoutOrVersion;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 64)]  public string szInfoTitle;
        public uint   dwInfoFlags;
        public Guid   guidItem;
        public IntPtr hBalloonIcon;
    }

    private const uint NIM_ADD     = 0;
    private const uint NIM_MODIFY  = 1;
    private const uint NIM_DELETE  = 2;
    private const uint NIF_MESSAGE = 0x01;
    private const uint NIF_ICON    = 0x02;
    private const uint NIF_TIP     = 0x04;
    private const uint TRAY_ID     = 1;
    private const uint WM_TRAY     = 0x8800; // custom callback message

    [DllImport("shell32.dll")] private static extern bool Shell_NotifyIcon(uint dwMessage, ref NOTIFYICONDATA pnid);

    // ── Icon loading ──────────────────────────────────────────────────────

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern uint PrivateExtractIcons(string file, int iconIndex,
        int cx, int cy, IntPtr[] phicon, uint[] piconid, uint nIcons, uint flags);

    [DllImport("user32.dll")] private static extern bool DestroyIcon(IntPtr hIcon);

    // ── Tray context menu ─────────────────────────────────────────────────

    [DllImport("user32.dll")] private static extern IntPtr CreatePopupMenu();
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern bool AppendMenu(IntPtr hMenu, uint uFlags, uint uIDNewItem, string? lpNewItem);
    [DllImport("user32.dll")] private static extern bool DestroyMenu(IntPtr hMenu);
    [DllImport("user32.dll")] private static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")]
    private static extern uint TrackPopupMenu(IntPtr hMenu, uint uFlags, int x, int y,
        int nReserved, IntPtr hWnd, IntPtr prcRect);
    [DllImport("user32.dll")] private static extern bool GetCursorPos(out POINT pt);

    [StructLayout(LayoutKind.Sequential)] private struct POINT { public int x; public int y; }

    private const uint MF_STRING    = 0x00;
    private const uint MF_SEPARATOR = 0x800;
    private const uint TPM_RETURNCMD   = 0x0100;
    private const uint TPM_RIGHTBUTTON = 0x0002;
    private const uint TPM_BOTTOMALIGN = 0x0020;
    private const int  IDM_OPEN = 1;
    private const int  IDM_EXIT = 2;

    // ── Window subclassing ────────────────────────────────────────────────

    private delegate IntPtr SubclassProc(IntPtr hWnd, uint uMsg, IntPtr wParam, IntPtr lParam,
                                          UIntPtr uIdSubclass, UIntPtr dwRefData);

    [DllImport("comctl32.dll")] private static extern bool SetWindowSubclass(
        IntPtr hWnd, SubclassProc pfnSubclass, UIntPtr uIdSubclass, UIntPtr dwRefData);
    [DllImport("comctl32.dll")] private static extern bool RemoveWindowSubclass(
        IntPtr hWnd, SubclassProc pfnSubclass, UIntPtr uIdSubclass);
    [DllImport("comctl32.dll")] private static extern IntPtr DefSubclassProc(
        IntPtr hWnd, uint uMsg, IntPtr wParam, IntPtr lParam);

    // ── Fields ────────────────────────────────────────────────────────────

    private readonly IntPtr   _hwnd;
    private readonly AppConfig _config;
    private          IntPtr   _hIcon;
    private readonly SubclassProc _subclassDelegate; // must stay rooted
    private bool _disposed;

    public event Action? ShowMenuRequested;
    public event Action? ExitRequested;

    private static readonly UIntPtr SubclassId = new(0xBEEF);

    // ── Constructor / Dispose ─────────────────────────────────────────────

    public TrayService(IntPtr hwnd, AppConfig config)
    {
        _hwnd   = hwnd;
        _config = config;

        _hIcon = LoadIcon(config);

        // Subclass the host window so we can intercept WM_TRAY without
        // replacing WinUI3's WndProc entirely
        _subclassDelegate = SubclassProcImpl;
        SetWindowSubclass(_hwnd, _subclassDelegate, SubclassId, UIntPtr.Zero);

        var nid = BuildNid();
        Shell_NotifyIcon(NIM_ADD, ref nid);
    }

    public void Dispose()
    {
        if (_disposed) return;
        _disposed = true;

        var nid = BuildNid();
        Shell_NotifyIcon(NIM_DELETE, ref nid);

        RemoveWindowSubclass(_hwnd, _subclassDelegate, SubclassId);

        if (_hIcon != IntPtr.Zero) { DestroyIcon(_hIcon); _hIcon = IntPtr.Zero; }
    }

    // ── Helpers ───────────────────────────────────────────────────────────

    private NOTIFYICONDATA BuildNid() => new()
    {
        cbSize          = (uint)Marshal.SizeOf<NOTIFYICONDATA>(),
        hWnd            = _hwnd,
        uID             = TRAY_ID,
        uFlags          = NIF_MESSAGE | NIF_ICON | NIF_TIP,
        uCallbackMessage = WM_TRAY,
        hIcon           = _hIcon,
        szTip           = "WinMacMenu",
    };

    private static IntPtr LoadIcon(AppConfig config)
    {
        // Try configured tray icon path
        var path = config.TrayIconPath;
        if (!string.IsNullOrEmpty(path))
            path = Environment.ExpandEnvironmentVariables(path);

        // Fall back to shell32 default app icon
        if (string.IsNullOrEmpty(path) || !File.Exists(path))
            path = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.System), "shell32.dll");

        var phicon  = new IntPtr[1];
        var piconid = new uint[1];
        PrivateExtractIcons(path, 0, 16, 16, phicon, piconid, 1, 0);
        return phicon[0];
    }

    // ── Window subclass procedure ─────────────────────────────────────────

    private IntPtr SubclassProcImpl(IntPtr hWnd, uint uMsg, IntPtr wParam, IntPtr lParam,
                                     UIntPtr uIdSubclass, UIntPtr dwRefData)
    {
        if (uMsg == WM_TRAY)
        {
            var mouseMsg = (uint)(lParam.ToInt64() & 0xFFFF);

            if (mouseMsg == 0x0201 || mouseMsg == 0x0203) // WM_LBUTTONDOWN or WM_LBUTTONDBLCLK
            {
                ShowMenuRequested?.Invoke();
            }
            else if (mouseMsg == 0x0205) // WM_RBUTTONUP
            {
                ShowTrayContextMenu();
            }

            return IntPtr.Zero;
        }

        return DefSubclassProc(hWnd, uMsg, wParam, lParam);
    }

    private void ShowTrayContextMenu()
    {
        var hMenu = CreatePopupMenu();
        AppendMenu(hMenu, MF_STRING,    IDM_OPEN, "Open WinMacMenu");
        AppendMenu(hMenu, MF_SEPARATOR, 0,        null);
        AppendMenu(hMenu, MF_STRING,    IDM_EXIT, "Exit");

        SetForegroundWindow(_hwnd);
        GetCursorPos(out var pt);

        var cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_BOTTOMALIGN,
                                 pt.x, pt.y, 0, _hwnd, IntPtr.Zero);
        DestroyMenu(hMenu);

        if (cmd == IDM_OPEN) ShowMenuRequested?.Invoke();
        if (cmd == IDM_EXIT) ExitRequested?.Invoke();
    }
}
