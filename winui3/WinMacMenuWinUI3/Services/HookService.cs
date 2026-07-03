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
    [DllImport("user32.dll")] private static extern uint   GetCurrentThreadId();
    [DllImport("user32.dll")] private static extern bool   PostThreadMessage(uint tid, uint msg, IntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll")] private static extern int    GetMessage(out MSG msg, IntPtr hWnd, uint min, uint max);
    [DllImport("user32.dll")] private static extern bool   TranslateMessage(ref MSG msg);
    [DllImport("user32.dll")] private static extern IntPtr DispatchMessage(ref MSG msg);

    [StructLayout(LayoutKind.Sequential)] private struct POINT { public int x, y; }
    [StructLayout(LayoutKind.Sequential)] private struct RECT  { public int left, top, right, bottom; }
    [StructLayout(LayoutKind.Sequential)] private struct MSG   { public IntPtr hWnd; public uint message; public IntPtr wParam, lParam; public uint time; public POINT pt; }
    [StructLayout(LayoutKind.Sequential)] private struct MONITORINFO { public uint cbSize; public RECT rcMonitor, rcWork; public uint dwFlags; }
    [StructLayout(LayoutKind.Sequential)] private struct KBDLLHOOKSTRUCT { public uint vkCode, scanCode, flags, time; public UIntPtr extra; }
    [StructLayout(LayoutKind.Sequential)] private struct MSLLHOOKSTRUCT { public POINT pt; public uint mouseData, flags, time; public UIntPtr extra; }

    private const int  WH_KEYBOARD_LL = 13;
    private const int  WH_MOUSE_LL    = 14;
    private const int  WM_KEYDOWN     = 0x0100;
    private const int  WM_KEYUP       = 0x0101;
    private const uint WM_QUIT        = 0x0012;
    private const int  WM_LBUTTONDOWN = 0x0201;
    private const int  WM_RBUTTONDOWN = 0x0204;
    private const int  WM_MBUTTONDOWN = 0x0207;
    private const int  VK_LWIN        = 0x5B;
    private const int  VK_RWIN        = 0x5C;
    private const int  VK_SHIFT       = 0x10;
    private const uint MONITOR_DEFAULTTONEAREST = 2;

    // ── Fields ────────────────────────────────────────────────────────────

    private readonly AppConfig       _config;
    private readonly HashSet<string> _exclusions;
    private readonly DispatcherQueue _ui;
    private readonly HookProc        _kbProc, _mouseProc; // must not be GC'd
    private IntPtr _kbHook, _mouseHook;
    private uint   _hookThreadId;
    private bool   _winKeyConsumed;
    private RECT   _startRect;
    private int    _startRectAge;

    public event Action? MenuRequested;

    // ── Constructor ───────────────────────────────────────────────────────

    public HookService(AppConfig config, DispatcherQueue ui)
    {
        _config = config;
        _ui     = ui;
        _exclusions = config.FullscreenExclusionList
            .Split(',', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries)
            .ToHashSet(StringComparer.OrdinalIgnoreCase);

        _kbProc    = KeyboardProc;  // stored as fields — prevents GC from collecting the delegates
        _mouseProc = MouseProc;

        bool needsKb    = config.WindowsKey || config.ShiftWindowsKey;
        bool needsMouse = config.LeftClick || config.RightClick || config.MiddleClick ||
                          config.ShiftLeftClick || config.ShiftRightClick || config.ShiftMiddleClick;

        if (!needsKb && !needsMouse) return;

        // Spin up a dedicated STA thread with its own Win32 message pump.
        // LL hooks require a message pump on the installing thread and must
        // return within ~300 ms — a tight dedicated loop guarantees this
        // regardless of UI thread load.
        var thread = new Thread(() =>
        {
            try
            {
                _hookThreadId = GetCurrentThreadId();
                _startRect    = ResolveStartButtonRect();

                // hMod = IntPtr.Zero is correct for in-process LL hooks
                if (needsKb)    _kbHook    = SetWindowsHookEx(WH_KEYBOARD_LL, _kbProc,    IntPtr.Zero, 0);
                if (needsMouse) _mouseHook = SetWindowsHookEx(WH_MOUSE_LL,    _mouseProc, IntPtr.Zero, 0);

                // Run a bare Win32 message loop — exits only on WM_QUIT
                while (GetMessage(out var msg, IntPtr.Zero, 0, 0) > 0)
                {
                    TranslateMessage(ref msg);
                    DispatchMessage(ref msg);
                }
            }
            finally
            {
                if (_kbHook    != IntPtr.Zero) UnhookWindowsHookEx(_kbHook);
                if (_mouseHook != IntPtr.Zero) UnhookWindowsHookEx(_mouseHook);
            }
        });
        thread.IsBackground = true;
        thread.SetApartmentState(ApartmentState.STA);
        thread.Start();
    }

    public void Dispose()
    {
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
                    _ui.TryEnqueue(() => MenuRequested?.Invoke());
                    return (IntPtr)1; // block — prevents system Start menu
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

                // Refresh start button rect every 5 s (handles taskbar restarts)
                if (unchecked(Environment.TickCount - _startRectAge) > 5000)
                {
                    _startRect    = ResolveStartButtonRect();
                    _startRectAge = Environment.TickCount;
                }

                if (PointInRect(ms.pt, _startRect))
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
                        _ui.TryEnqueue(() => MenuRequested?.Invoke());
                        return (IntPtr)1;
                    }
                }
            }
        }
        return CallNextHookEx(_mouseHook, nCode, wParam, lParam);
    }

    // ── Start button rect ─────────────────────────────────────────────────

    private static RECT ResolveStartButtonRect()
    {
        var tray = FindWindow("Shell_TrayWnd", null);
        if (tray == IntPtr.Zero) return default;

        // Windows 10 / ExplorerPatcher / StartAllBack
        var startHwnd = FindWindowEx(tray, IntPtr.Zero, "Start", null);
        if (startHwnd != IntPtr.Zero)
        {
            GetWindowRect(startHwnd, out var r);
            if (r.right - r.left > 4) return r;
        }

        GetWindowRect(tray, out var tr);
        int h = tr.bottom - tr.top;
        int w = tr.right  - tr.left;

        // Windows 11 — probe left edge first, then centre
        foreach (var probe in new[] {
            new POINT { x = tr.left + h / 2,     y = tr.top + h / 2 },
            new POINT { x = tr.left + w / 2,     y = tr.top + h / 2 },
        })
        {
            var hw = WindowFromPoint(probe);
            if (hw != IntPtr.Zero && hw != tray)
            {
                GetWindowRect(hw, out var r);
                if (r.right - r.left > 4) return r;
            }
        }

        // Fallback: leftmost square of taskbar
        return new RECT { left = tr.left, top = tr.top, right = tr.left + h, bottom = tr.bottom };
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

        return wr.left <= mi.rcMonitor.left && wr.top    <= mi.rcMonitor.top &&
               wr.right >= mi.rcMonitor.right && wr.bottom >= mi.rcMonitor.bottom;
    }
}
