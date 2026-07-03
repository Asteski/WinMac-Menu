using System.Diagnostics;
using System.Runtime.InteropServices;
using Microsoft.UI.Dispatching;
using WinMacMenuWinUI3.Models;

namespace WinMacMenuWinUI3.Services;

public sealed class HookService : IDisposable
{
    // ── Win32 ─────────────────────────────────────────────────────────────

    private delegate IntPtr HookProc(int nCode, IntPtr wParam, IntPtr lParam);

    [DllImport("user32.dll")] private static extern IntPtr SetWindowsHookEx(int idHook, HookProc fn, IntPtr hMod, uint threadId);
    [DllImport("user32.dll")] private static extern bool   UnhookWindowsHookEx(IntPtr hhk);
    [DllImport("user32.dll")] private static extern IntPtr CallNextHookEx(IntPtr hhk, int nCode, IntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll")] private static extern short  GetAsyncKeyState(int vKey);
    [DllImport("user32.dll")] private static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] private static extern bool   GetWindowRect(IntPtr hWnd, out RECT r);
    [DllImport("user32.dll")] private static extern IntPtr MonitorFromWindow(IntPtr hWnd, uint flags);
    [DllImport("user32.dll")] private static extern bool   GetMonitorInfo(IntPtr hMon, ref MONITORINFO mi);
    [DllImport("user32.dll")] private static extern uint   GetWindowThreadProcessId(IntPtr hWnd, out uint pid);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern IntPtr FindWindow(string? cls, string? wnd);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern IntPtr FindWindowEx(IntPtr parent, IntPtr after, string? cls, string? wnd);
    [DllImport("user32.dll")] private static extern IntPtr WindowFromPoint(POINT pt);

    [StructLayout(LayoutKind.Sequential)] private struct POINT { public int x, y; }
    [StructLayout(LayoutKind.Sequential)] private struct RECT  { public int left, top, right, bottom; }
    [StructLayout(LayoutKind.Sequential)]
    private struct MONITORINFO { public uint cbSize; public RECT rcMonitor, rcWork; public uint dwFlags; }

    [StructLayout(LayoutKind.Sequential)]
    private struct KBDLLHOOKSTRUCT { public uint vkCode, scanCode, flags, time; public UIntPtr extra; }

    [StructLayout(LayoutKind.Sequential)]
    private struct MSLLHOOKSTRUCT { public POINT pt; public uint mouseData, flags, time; public UIntPtr extra; }

    private const int WH_KEYBOARD_LL = 13;
    private const int WH_MOUSE_LL    = 14;
    private const int WM_KEYDOWN     = 0x0100;
    private const int WM_KEYUP       = 0x0101;
    private const int WM_LBUTTONDOWN = 0x0201;
    private const int WM_RBUTTONDOWN = 0x0204;
    private const int WM_MBUTTONDOWN = 0x0207;
    private const int VK_LWIN = 0x5B;
    private const int VK_RWIN = 0x5C;
    private const int VK_SHIFT= 0x10;
    private const uint MONITOR_DEFAULTTONEAREST = 2;

    // ── Fields ────────────────────────────────────────────────────────────

    private readonly AppConfig        _config;
    private readonly HashSet<string>  _exclusions;
    private readonly DispatcherQueue  _dispatcher;
    private readonly HookProc         _kbProc, _mouseProc; // pinned delegates
    private IntPtr _kbHook, _mouseHook;
    private bool   _winKeyConsumed;
    private RECT   _startBtnRect;
    private int    _startBtnRefreshTick;

    public event Action? MenuRequested;

    // ── Init / Dispose ────────────────────────────────────────────────────

    public HookService(AppConfig config, DispatcherQueue dispatcher)
    {
        _config     = config;
        _dispatcher = dispatcher;
        _exclusions = config.FullscreenExclusionList
            .Split(',', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries)
            .ToHashSet(StringComparer.OrdinalIgnoreCase);

        _kbProc    = KeyboardProc;
        _mouseProc = MouseProc;

        RefreshStartButtonRect();

        // For LL hooks, hMod must be the module handle of the calling exe
        var hMod = Marshal.GetHINSTANCE(typeof(HookService).Module);

        bool needsKb    = config.WindowsKey || config.ShiftWindowsKey;
        bool needsMouse = config.LeftClick || config.RightClick || config.MiddleClick ||
                          config.ShiftLeftClick || config.ShiftRightClick || config.ShiftMiddleClick;

        if (needsKb)    _kbHook    = SetWindowsHookEx(WH_KEYBOARD_LL, _kbProc,    hMod, 0);
        if (needsMouse) _mouseHook = SetWindowsHookEx(WH_MOUSE_LL,    _mouseProc, hMod, 0);
    }

    public void Dispose()
    {
        if (_kbHook    != IntPtr.Zero) { UnhookWindowsHookEx(_kbHook);    _kbHook    = IntPtr.Zero; }
        if (_mouseHook != IntPtr.Zero) { UnhookWindowsHookEx(_mouseHook); _mouseHook = IntPtr.Zero; }
    }

    // ── Keyboard hook ─────────────────────────────────────────────────────

    private IntPtr KeyboardProc(int nCode, IntPtr wParam, IntPtr lParam)
    {
        if (nCode >= 0)
        {
            var kb  = Marshal.PtrToStructure<KBDLLHOOKSTRUCT>(lParam);
            bool isWin = kb.vkCode == VK_LWIN || kb.vkCode == VK_RWIN;

            if (isWin)
            {
                if ((int)wParam == WM_KEYDOWN && !_winKeyConsumed)
                {
                    bool shift   = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
                    bool trigger = (!shift && _config.WindowsKey) ||
                                   ( shift && _config.ShiftWindowsKey);

                    if (trigger && !ShouldIgnore())
                    {
                        _winKeyConsumed = true;
                        // Dispatch to UI thread and return immediately — do NOT do any
                        // heavy work here or Windows will bypass the hook after ~300 ms.
                        _dispatcher.TryEnqueue(() => MenuRequested?.Invoke());
                        return (IntPtr)1; // block key → prevents Start menu
                    }
                }
                else if ((int)wParam == WM_KEYUP && _winKeyConsumed)
                {
                    _winKeyConsumed = false;
                    return (IntPtr)1; // block matching keyup too
                }
            }
        }
        return CallNextHookEx(_kbHook, nCode, wParam, lParam);
    }

    // ── Mouse hook ────────────────────────────────────────────────────────

    private IntPtr MouseProc(int nCode, IntPtr wParam, IntPtr lParam)
    {
        if (nCode >= 0)
        {
            int msg = (int)wParam;
            if (msg is WM_LBUTTONDOWN or WM_RBUTTONDOWN or WM_MBUTTONDOWN)
            {
                var ms = Marshal.PtrToStructure<MSLLHOOKSTRUCT>(lParam);

                // Refresh the start button rect periodically (taskbar can restart/move)
                if (Environment.TickCount - _startBtnRefreshTick > 5000)
                    RefreshStartButtonRect();

                if (PointInRect(ms.pt, _startBtnRect))
                {
                    bool shift   = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
                    bool trigger = msg switch
                    {
                        WM_LBUTTONDOWN => shift ? _config.ShiftLeftClick  : _config.LeftClick,
                        WM_RBUTTONDOWN => shift ? _config.ShiftRightClick : _config.RightClick,
                        WM_MBUTTONDOWN => shift ? _config.ShiftMiddleClick : _config.MiddleClick,
                        _              => false,
                    };

                    if (trigger && !ShouldIgnore())
                    {
                        _dispatcher.TryEnqueue(() => MenuRequested?.Invoke());
                        return (IntPtr)1; // block click → prevents Start menu
                    }
                }
            }
        }
        return CallNextHookEx(_mouseHook, nCode, wParam, lParam);
    }

    // ── Start button rect resolution ──────────────────────────────────────

    private void RefreshStartButtonRect()
    {
        _startBtnRefreshTick = Environment.TickCount;
        _startBtnRect = ResolveStartButtonRect();
    }

    private static RECT ResolveStartButtonRect()
    {
        var tray = FindWindow("Shell_TrayWnd", null);
        if (tray == IntPtr.Zero) return default;

        // Windows 10 / ExplorerPatcher / StartAllBack: Start has its own HWND
        var startHwnd = FindWindowEx(tray, IntPtr.Zero, "Start", null);
        if (startHwnd != IntPtr.Zero)
        {
            GetWindowRect(startHwnd, out var r);
            if (r.right - r.left > 4) return r;
        }

        // Windows 11: Start button is inside a compositor bridge and has no
        // independent HWND. Probe the taskbar at the expected position instead.
        GetWindowRect(tray, out var trayRect);
        int taskbarH = trayRect.bottom - trayRect.top;
        int taskbarW = trayRect.right  - trayRect.left;

        // Try probing at left-aligned position (Win10 style / ExplorerPatcher)
        var probeLeft = new POINT { x = trayRect.left + taskbarH / 2, y = (trayRect.top + trayRect.bottom) / 2 };
        var hwndLeft  = WindowFromPoint(probeLeft);
        if (hwndLeft != tray && hwndLeft != IntPtr.Zero)
        {
            GetWindowRect(hwndLeft, out var lr);
            if (lr.right - lr.left > 4 && lr.bottom - lr.top > 4) return lr;
        }

        // Windows 11 center-aligned taskbar: probe at horizontal center
        var probeCenter = new POINT { x = trayRect.left + taskbarW / 2, y = (trayRect.top + trayRect.bottom) / 2 };
        var hwndCenter  = WindowFromPoint(probeCenter);
        if (hwndCenter != tray && hwndCenter != IntPtr.Zero)
        {
            GetWindowRect(hwndCenter, out var cr);
            if (cr.right - cr.left > 4 && cr.bottom - cr.top > 4) return cr;
        }

        // Last resort: treat the leftmost taskbarH × taskbarH square as the start zone
        return new RECT
        {
            left   = trayRect.left,
            top    = trayRect.top,
            right  = trayRect.left + taskbarH,
            bottom = trayRect.bottom,
        };
    }

    private static bool PointInRect(POINT pt, RECT r)
        => pt.x >= r.left && pt.x <= r.right && pt.y >= r.top && pt.y <= r.bottom;

    // ── Fullscreen suppression ────────────────────────────────────────────

    private bool ShouldIgnore()
    {
        if (!_config.IgnoreTriggersWhenFullscreen) return false;

        var fg = GetForegroundWindow();
        if (fg == IntPtr.Zero) return false;

        try
        {
            GetWindowThreadProcessId(fg, out var pid);
            using var proc = Process.GetProcessById((int)pid);
            var exe = Path.GetFileName(proc.MainModule?.FileName ?? "");
            if (_exclusions.Contains(exe)) return false; // whitelisted app
        }
        catch { }

        var hMon = MonitorFromWindow(fg, MONITOR_DEFAULTTONEAREST);
        var mi   = new MONITORINFO { cbSize = (uint)Marshal.SizeOf<MONITORINFO>() };
        GetMonitorInfo(hMon, ref mi);
        GetWindowRect(fg, out var wr);

        return wr.left  <= mi.rcMonitor.left  && wr.top    <= mi.rcMonitor.top &&
               wr.right >= mi.rcMonitor.right && wr.bottom >= mi.rcMonitor.bottom;
    }
}
