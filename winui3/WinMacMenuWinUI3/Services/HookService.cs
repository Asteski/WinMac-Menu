using System.Diagnostics;
using System.Runtime.InteropServices;
using Microsoft.UI.Dispatching;
using WinMacMenuWinUI3.Models;

namespace WinMacMenuWinUI3.Services;

/// <summary>
/// Installs WH_KEYBOARD_LL and WH_MOUSE_LL hooks on a dedicated background
/// thread that runs its own Win32 message loop. This guarantees hook callbacks
/// are serviced well within Windows' ~300 ms hook timeout, regardless of what
/// the WinUI3 UI thread is doing.
/// </summary>
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
    [DllImport("user32.dll")] private static extern uint   GetCurrentThreadId();
    [DllImport("user32.dll")] private static extern bool   PostThreadMessage(uint tid, uint msg, IntPtr wParam, IntPtr lParam);

    [StructLayout(LayoutKind.Sequential)] private struct MSG   { public IntPtr hWnd; public uint message; public IntPtr wParam, lParam; public uint time; public POINT pt; }
    [StructLayout(LayoutKind.Sequential)] private struct POINT { public int x, y; }
    [StructLayout(LayoutKind.Sequential)] private struct RECT  { public int left, top, right, bottom; }
    [StructLayout(LayoutKind.Sequential)] private struct MONITORINFO { public uint cbSize; public RECT rcMonitor, rcWork; public uint dwFlags; }
    [StructLayout(LayoutKind.Sequential)] private struct KBDLLHOOKSTRUCT { public uint vkCode, scanCode, flags, time; public UIntPtr extra; }
    [StructLayout(LayoutKind.Sequential)] private struct MSLLHOOKSTRUCT { public POINT pt; public uint mouseData, flags, time; public UIntPtr extra; }

    [DllImport("user32.dll")] private static extern int  GetMessage(out MSG msg, IntPtr hWnd, uint min, uint max);
    [DllImport("user32.dll")] private static extern bool TranslateMessage(ref MSG msg);
    [DllImport("user32.dll")] private static extern IntPtr DispatchMessage(ref MSG msg);

    private const int WH_KEYBOARD_LL = 13;
    private const int WH_MOUSE_LL    = 14;
    private const int WM_KEYDOWN     = 0x0100;
    private const int WM_KEYUP       = 0x0101;
    private const uint WM_QUIT       = 0x0012;
    private const int WM_LBUTTONDOWN = 0x0201;
    private const int WM_RBUTTONDOWN = 0x0204;
    private const int WM_MBUTTONDOWN = 0x0207;
    private const int VK_LWIN  = 0x5B;
    private const int VK_RWIN  = 0x5C;
    private const int VK_SHIFT = 0x10;
    private const uint MONITOR_DEFAULTTONEAREST = 2;

    // ── Fields ────────────────────────────────────────────────────────────

    private readonly AppConfig       _config;
    private readonly HashSet<string> _exclusions;
    private readonly DispatcherQueue _uiDispatcher; // UI thread dispatcher for safe callbacks
    private readonly HookProc        _kbProc, _mouseProc; // must stay rooted — no GC
    private IntPtr _kbHook, _mouseHook;
    private uint   _hookThreadId;
    private bool   _winKeyConsumed;
    private RECT   _startBtnRect;
    private int    _startBtnRefreshTick;

    public event Action? MenuRequested;

    // ── Constructor / Dispose ─────────────────────────────────────────────

    public HookService(AppConfig config, DispatcherQueue uiDispatcher)
    {
        _config        = config;
        _uiDispatcher  = uiDispatcher;
        _exclusions    = config.FullscreenExclusionList
            .Split(',', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries)
            .ToHashSet(StringComparer.OrdinalIgnoreCase);

        // Delegate instances pinned as fields so the GC never collects them
        _kbProc    = KeyboardProc;
        _mouseProc = MouseProc;

        bool needsKb    = config.WindowsKey || config.ShiftWindowsKey;
        bool needsMouse = config.LeftClick || config.RightClick || config.MiddleClick ||
                          config.ShiftLeftClick || config.ShiftRightClick || config.ShiftMiddleClick;

        if (!needsKb && !needsMouse) return;

        RefreshStartButtonRect();

        // Run hooks on a dedicated STA thread with its own Win32 message loop.
        // This is the only reliable way to guarantee hook callbacks are serviced
        // within the ~300 ms timeout imposed by Windows for LL hooks.
        var ready = new ManualResetEventSlim(false);
        var thread = new Thread(() =>
        {
            _hookThreadId = GetCurrentThreadId();

            // For LL hooks hMod must be NULL (they run in the installing thread's context)
            if (needsKb)    _kbHook    = SetWindowsHookEx(WH_KEYBOARD_LL, _kbProc,    IntPtr.Zero, 0);
            if (needsMouse) _mouseHook = SetWindowsHookEx(WH_MOUSE_LL,    _mouseProc, IntPtr.Zero, 0);

            ready.Set(); // signal that hooks are installed

            // Tight Win32 message loop — keeps the thread alive and services hooks
            while (GetMessage(out var msg, IntPtr.Zero, 0, 0) > 0)
            {
                TranslateMessage(ref msg);
                DispatchMessage(ref msg);
            }

            // WM_QUIT received — clean up
            if (_kbHook    != IntPtr.Zero) UnhookWindowsHookEx(_kbHook);
            if (_mouseHook != IntPtr.Zero) UnhookWindowsHookEx(_mouseHook);
        });
        thread.IsBackground = true;
        thread.SetApartmentState(ApartmentState.STA);
        thread.Start();

        ready.Wait(); // don't return until hooks are actually installed
    }

    public void Dispose()
    {
        // Send WM_QUIT to the hook thread's message loop to trigger cleanup
        if (_hookThreadId != 0)
            PostThreadMessage(_hookThreadId, WM_QUIT, IntPtr.Zero, IntPtr.Zero);
    }

    // ── Keyboard hook ─────────────────────────────────────────────────────

    private IntPtr KeyboardProc(int nCode, IntPtr wParam, IntPtr lParam)
    {
        if (nCode >= 0)
        {
            var kb    = Marshal.PtrToStructure<KBDLLHOOKSTRUCT>(lParam);
            bool isWin = kb.vkCode == VK_LWIN || kb.vkCode == VK_RWIN;

            if (isWin && (int)wParam == WM_KEYDOWN && !_winKeyConsumed)
            {
                bool shift   = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
                bool trigger = (!shift && _config.WindowsKey) ||
                               ( shift && _config.ShiftWindowsKey);

                if (trigger && !ShouldIgnore())
                {
                    _winKeyConsumed = true;
                    // Return immediately — dispatch the heavy work to the UI thread
                    _uiDispatcher.TryEnqueue(() => MenuRequested?.Invoke());
                    return (IntPtr)1;
                }
            }

            if (isWin && (int)wParam == WM_KEYUP && _winKeyConsumed)
            {
                _winKeyConsumed = false;
                return (IntPtr)1;
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

                if (Environment.TickCount - _startBtnRefreshTick > 5000)
                    RefreshStartButtonRect();

                if (PointInRect(ms.pt, _startBtnRect))
                {
                    bool shift   = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
                    bool trigger = msg switch
                    {
                        WM_LBUTTONDOWN => shift ? _config.ShiftLeftClick   : _config.LeftClick,
                        WM_RBUTTONDOWN => shift ? _config.ShiftRightClick  : _config.RightClick,
                        WM_MBUTTONDOWN => shift ? _config.ShiftMiddleClick : _config.MiddleClick,
                        _              => false,
                    };

                    if (trigger && !ShouldIgnore())
                    {
                        _uiDispatcher.TryEnqueue(() => MenuRequested?.Invoke());
                        return (IntPtr)1;
                    }
                }
            }
        }
        return CallNextHookEx(_mouseHook, nCode, wParam, lParam);
    }

    // ── Start button rect ─────────────────────────────────────────────────

    private void RefreshStartButtonRect()
    {
        _startBtnRefreshTick = Environment.TickCount;
        _startBtnRect = ResolveStartButtonRect();
    }

    private static RECT ResolveStartButtonRect()
    {
        var tray = FindWindow("Shell_TrayWnd", null);
        if (tray == IntPtr.Zero) return default;

        // Windows 10 / ExplorerPatcher / StartAllBack: dedicated Start HWND
        var startHwnd = FindWindowEx(tray, IntPtr.Zero, "Start", null);
        if (startHwnd != IntPtr.Zero)
        {
            GetWindowRect(startHwnd, out var r);
            if (r.right - r.left > 4) return r;
        }

        GetWindowRect(tray, out var tray_r);
        int h = tray_r.bottom - tray_r.top;
        int w = tray_r.right  - tray_r.left;

        // Probe left-edge (Win10 layout)
        var hwndL = WindowFromPoint(new POINT { x = tray_r.left + h / 2, y = tray_r.top + h / 2 });
        if (hwndL != tray && hwndL != IntPtr.Zero)
        {
            GetWindowRect(hwndL, out var lr);
            if (lr.right - lr.left > 4) return lr;
        }

        // Probe centre (Win11 centred taskbar)
        var hwndC = WindowFromPoint(new POINT { x = tray_r.left + w / 2, y = tray_r.top + h / 2 });
        if (hwndC != tray && hwndC != IntPtr.Zero)
        {
            GetWindowRect(hwndC, out var cr);
            if (cr.right - cr.left > 4) return cr;
        }

        // Fallback: leftmost square of taskbar
        return new RECT { left = tray_r.left, top = tray_r.top, right = tray_r.left + h, bottom = tray_r.bottom };
    }

    private static bool PointInRect(POINT pt, RECT r)
        => pt.x >= r.left && pt.x <= r.right && pt.y >= r.top && pt.y <= r.bottom;

    // ── Fullscreen guard ──────────────────────────────────────────────────

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
            if (_exclusions.Contains(exe)) return false;
        }
        catch { }

        var hMon = MonitorFromWindow(fg, MONITOR_DEFAULTTONEAREST);
        var mi   = new MONITORINFO { cbSize = (uint)Marshal.SizeOf<MONITORINFO>() };
        GetMonitorInfo(hMon, ref mi);
        GetWindowRect(fg, out var wr);

        return wr.left  <= mi.rcMonitor.left && wr.top    <= mi.rcMonitor.top &&
               wr.right >= mi.rcMonitor.right && wr.bottom >= mi.rcMonitor.bottom;
    }
}
