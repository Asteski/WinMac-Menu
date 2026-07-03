using System.Diagnostics;
using System.Runtime.InteropServices;
using WinMacMenuWinUI3.Models;

namespace WinMacMenuWinUI3.Services;

/// <summary>
/// Installs low-level keyboard and mouse hooks to trigger the menu
/// from Windows key presses and Start button clicks, per [Controls] config.
/// </summary>
public sealed class HookService : IDisposable
{
    // ── Win32 ─────────────────────────────────────────────────────────────

    private delegate IntPtr HookProc(int nCode, IntPtr wParam, IntPtr lParam);

    [DllImport("user32.dll")] private static extern IntPtr SetWindowsHookEx(int idHook, HookProc lpfn, IntPtr hMod, uint dwThreadId);
    [DllImport("user32.dll")] private static extern bool   UnhookWindowsHookEx(IntPtr hhk);
    [DllImport("user32.dll")] private static extern IntPtr CallNextHookEx(IntPtr hhk, int nCode, IntPtr wParam, IntPtr lParam);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode)] private static extern IntPtr GetModuleHandle(string? lpModuleName);
    [DllImport("user32.dll")] private static extern short  GetAsyncKeyState(int vKey);
    [DllImport("user32.dll")] private static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] private static extern bool   GetWindowRect(IntPtr hWnd, out RECT lpRect);
    [DllImport("user32.dll")] private static extern IntPtr MonitorFromWindow(IntPtr hWnd, uint dwFlags);
    [DllImport("user32.dll")] private static extern bool   GetMonitorInfo(IntPtr hMonitor, ref MONITORINFO lpmi);
    [DllImport("user32.dll")] private static extern uint   GetWindowThreadProcessId(IntPtr hWnd, out uint lpdwProcessId);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern IntPtr FindWindow(string? cls, string? wnd);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern IntPtr FindWindowEx(IntPtr parent, IntPtr after, string? cls, string? wnd);
    [DllImport("user32.dll")] private static extern IntPtr WindowFromPoint(POINT pt);
    [DllImport("user32.dll")] private static extern IntPtr GetAncestor(IntPtr hWnd, uint gaFlags);

    [StructLayout(LayoutKind.Sequential)] private struct POINT  { public int x, y; }
    [StructLayout(LayoutKind.Sequential)] private struct RECT   { public int left, top, right, bottom; }

    [StructLayout(LayoutKind.Sequential)]
    private struct MONITORINFO
    {
        public uint cbSize;
        public RECT rcMonitor;
        public RECT rcWork;
        public uint dwFlags;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct KBDLLHOOKSTRUCT
    {
        public uint vkCode, scanCode, flags, time;
        public UIntPtr dwExtraInfo;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct MSLLHOOKSTRUCT
    {
        public POINT pt;
        public uint  mouseData, flags, time;
        public UIntPtr dwExtraInfo;
    }

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
    private const uint GA_ROOT = 2;
    private const uint MONITOR_DEFAULTTONEAREST = 2;

    // ── Fields ────────────────────────────────────────────────────────────

    private readonly AppConfig _config;
    private readonly HashSet<string> _exclusions;
    private IntPtr _kbHook, _mouseHook;
    private readonly HookProc _kbProc, _mouseProc; // keep delegates alive
    private IntPtr _startButtonHwnd;
    private bool _winKeyDown; // tracks if we already consumed the WinKey press

    public event Action? MenuRequested;

    // ── Constructor / Dispose ─────────────────────────────────────────────

    public HookService(AppConfig config)
    {
        _config = config;
        _exclusions = config.FullscreenExclusionList
            .Split(',', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries)
            .ToHashSet(StringComparer.OrdinalIgnoreCase);

        _kbProc    = KeyboardProc;
        _mouseProc = MouseProc;

        _startButtonHwnd = FindStartButton();

        var hMod = GetModuleHandle(null);

        bool needsKeyboard = config.WindowsKey || config.ShiftWindowsKey;
        bool needsMouse    = config.LeftClick || config.RightClick || config.MiddleClick ||
                             config.ShiftLeftClick || config.ShiftRightClick || config.ShiftMiddleClick;

        if (needsKeyboard)
            _kbHook = SetWindowsHookEx(WH_KEYBOARD_LL, _kbProc, hMod, 0);

        if (needsMouse)
            _mouseHook = SetWindowsHookEx(WH_MOUSE_LL, _mouseProc, hMod, 0);
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
            var msg = (int)wParam;
            var isWin = kb.vkCode == VK_LWIN || kb.vkCode == VK_RWIN;

            if (isWin && msg == WM_KEYDOWN && !_winKeyDown)
            {
                bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;

                bool trigger = (!shift && _config.WindowsKey) ||
                               ( shift && _config.ShiftWindowsKey);

                if (trigger && !ShouldIgnore())
                {
                    _winKeyDown = true;
                    MenuRequested?.Invoke();
                    return (IntPtr)1; // block key — prevents Start menu opening
                }
            }

            if (isWin && msg == WM_KEYUP)
            {
                if (_winKeyDown)
                {
                    _winKeyDown = false;
                    return (IntPtr)1; // block keyup too
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
            var msg = (int)wParam;
            if (msg is WM_LBUTTONDOWN or WM_RBUTTONDOWN or WM_MBUTTONDOWN)
            {
                var ms    = Marshal.PtrToStructure<MSLLHOOKSTRUCT>(lParam);
                var hwnd  = WindowFromPoint(ms.pt);
                var root  = GetAncestor(hwnd, GA_ROOT);

                // Refresh Start button handle periodically (taskbar can restart)
                if (_startButtonHwnd == IntPtr.Zero)
                    _startButtonHwnd = FindStartButton();

                if (root == _startButtonHwnd || IsChildOfStartButton(hwnd))
                {
                    bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;

                    bool trigger = msg switch
                    {
                        WM_LBUTTONDOWN => shift ? _config.ShiftLeftClick  : _config.LeftClick,
                        WM_RBUTTONDOWN => shift ? _config.ShiftRightClick : _config.RightClick,
                        WM_MBUTTONDOWN => shift ? _config.ShiftMiddleClick: _config.MiddleClick,
                        _              => false,
                    };

                    if (trigger && !ShouldIgnore())
                    {
                        MenuRequested?.Invoke();
                        return (IntPtr)1; // block click — prevents Start menu opening
                    }
                }
            }
        }

        return CallNextHookEx(_mouseHook, nCode, wParam, lParam);
    }

    // ── Helpers ───────────────────────────────────────────────────────────

    // Finds the Start button HWND across Windows 10 and 11 taskbar structures
    private static IntPtr FindStartButton()
    {
        var tray = FindWindow("Shell_TrayWnd", null);
        if (tray == IntPtr.Zero) return IntPtr.Zero;

        // Windows 10 / 11 — class name "Start"
        var btn = FindWindowEx(tray, IntPtr.Zero, "Start", null);
        if (btn != IntPtr.Zero) return btn;

        // Windows 11 22H2+ secondary taskbar
        btn = FindWindowEx(tray, IntPtr.Zero, "Windows.UI.Input.InputSite.WindowClass", null);
        if (btn != IntPtr.Zero) return tray; // treat whole tray as anchor

        return tray; // fallback: trigger on any taskbar click in the start area
    }

    private bool IsChildOfStartButton(IntPtr hWnd)
    {
        var current = hWnd;
        for (int i = 0; i < 8 && current != IntPtr.Zero; i++)
        {
            if (current == _startButtonHwnd) return true;
            current = GetAncestor(current, GA_ROOT);
            if (current == hWnd) break;
        }
        return false;
    }

    // Returns true when triggers should be suppressed (fullscreen app is focused)
    private bool ShouldIgnore()
    {
        if (!_config.IgnoreTriggersWhenFullscreen) return false;

        var fg = GetForegroundWindow();
        if (fg == IntPtr.Zero) return false;

        // Check exclusion list
        try
        {
            GetWindowThreadProcessId(fg, out var pid);
            using var proc = Process.GetProcessById((int)pid);
            var exe = Path.GetFileName(proc.MainModule?.FileName ?? "");
            if (_exclusions.Contains(exe)) return false; // excluded — don't suppress
        }
        catch { }

        // Check if the foreground window fills the monitor
        var hMon  = MonitorFromWindow(fg, MONITOR_DEFAULTTONEAREST);
        var info  = new MONITORINFO { cbSize = (uint)Marshal.SizeOf<MONITORINFO>() };
        GetMonitorInfo(hMon, ref info);
        GetWindowRect(fg, out var wr);

        return wr.left   <= info.rcMonitor.left  &&
               wr.top    <= info.rcMonitor.top    &&
               wr.right  >= info.rcMonitor.right  &&
               wr.bottom >= info.rcMonitor.bottom;
    }
}
