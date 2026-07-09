using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text;
using Microsoft.UI.Dispatching;
using WinMacMenuWinUI3.Models;

namespace WinMacMenuWinUI3.Services;

public sealed class HookService : IDisposable
{
    // ── Win32 ─────────────────────────────────────────────────────────────

    private delegate IntPtr HookProc(int nCode, IntPtr wParam, IntPtr lParam);

    [DllImport("user32.dll", SetLastError = true)] private static extern IntPtr SetWindowsHookEx(int idHook, HookProc fn, IntPtr hMod, uint threadId);
    [DllImport("user32.dll")] private static extern bool   UnhookWindowsHookEx(IntPtr hhk);
    [DllImport("user32.dll")] private static extern IntPtr CallNextHookEx(IntPtr hhk, int nCode, IntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll")] private static extern short  GetAsyncKeyState(int vKey);
    [DllImport("user32.dll")] private static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] private static extern bool   GetWindowRect(IntPtr hWnd, out RECT r);
    [DllImport("user32.dll")] private static extern bool   IsIconic(IntPtr hWnd);
    [DllImport("user32.dll")] private static extern IntPtr MonitorFromWindow(IntPtr hWnd, uint flags);
    [DllImport("user32.dll")] private static extern bool   GetMonitorInfo(IntPtr hMon, ref MONITORINFO mi);
    [DllImport("user32.dll")] private static extern uint   GetWindowThreadProcessId(IntPtr hWnd, out uint pid);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern int GetClassName(IntPtr hWnd, StringBuilder className, int maxCount);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern IntPtr FindWindow(string? cls, string? wnd);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern IntPtr FindWindowEx(IntPtr parent, IntPtr after, string? cls, string? wnd);
    [DllImport("user32.dll")] private static extern IntPtr WindowFromPoint(POINT pt);
    [DllImport("kernel32.dll")] private static extern uint  GetCurrentThreadId();
    [DllImport("user32.dll")]  private static extern bool  PostThreadMessage(uint tid, uint msg, IntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll")] private static extern int    GetMessage(out MSG msg, IntPtr hWnd, uint min, uint max);
    [DllImport("user32.dll")] private static extern bool   TranslateMessage(ref MSG msg);
    [DllImport("user32.dll")] private static extern IntPtr DispatchMessage(ref MSG msg);
    [DllImport("user32.dll")] private static extern void   keybd_event(byte bVk, byte bScan, uint dwFlags, UIntPtr dwExtraInfo);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)] private static extern IntPtr GetModuleHandle(string? moduleName);

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
    private const int  WM_SYSKEYDOWN  = 0x0104;
    private const int  WM_SYSKEYUP    = 0x0105;
    private const uint WM_QUIT        = 0x0012;
    private const int  WM_LBUTTONDOWN = 0x0201;
    private const int  WM_LBUTTONUP   = 0x0202;
    private const int  WM_RBUTTONDOWN = 0x0204;
    private const int  WM_RBUTTONUP   = 0x0205;
    private const int  WM_MBUTTONDOWN = 0x0207;
    private const int  WM_MBUTTONUP   = 0x0208;
    private const int  VK_LWIN        = 0x5B;
    private const int  VK_RWIN        = 0x5C;
    private const int  VK_SHIFT       = 0x10;
    private const uint LLKHF_INJECTED = 0x10;
    private const uint KEYEVENTF_KEYUP = 0x0002;
    private const uint MONITOR_DEFAULTTONEAREST = 2;

    // ── Fields ────────────────────────────────────────────────────────────

    private readonly AppConfig       _config;
    private readonly HashSet<string> _exclusions;
    private readonly DispatcherQueue _ui;
    private readonly HookProc        _kbProc, _mouseProc; // must not be GC'd
    private IntPtr _kbHook, _mouseHook;
    private uint   _hookThreadId;
    private bool   _winKeyCaptureActive;
    private uint   _winKeyCapturedVk;
    private bool   _winKeyCaptureNeedsShift;
    private bool   _winKeyChordCancelled;
    private bool   _leftBlocked, _rightBlocked, _middleBlocked;
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

                var hModule = GetModuleHandle(null);
                if (needsKb)    _kbHook    = SetWindowsHookEx(WH_KEYBOARD_LL, _kbProc,    hModule, 0);
                if (needsMouse) _mouseHook = SetWindowsHookEx(WH_MOUSE_LL,    _mouseProc, hModule, 0);

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
            int msg = (int)wParam;

            if ((kb.flags & LLKHF_INJECTED) != 0)
                return CallNextHookEx(_kbHook, nCode, wParam, lParam);

            if (ShouldIgnore())
            {
                ClearWinKeyCapture();
                return CallNextHookEx(_kbHook, nCode, wParam, lParam);
            }

            if (msg is WM_KEYDOWN or WM_SYSKEYDOWN)
            {
                if (isWin)
                {
                    if (!_winKeyCaptureActive)
                    {
                        bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
                        bool wantsShiftMode = shift && _config.ShiftWindowsKey;
                        bool wantsNormalMode = !shift && _config.WindowsKey;

                        if (!wantsShiftMode && !wantsNormalMode)
                            return CallNextHookEx(_kbHook, nCode, wParam, lParam);

                        _winKeyCaptureActive = true;
                        _winKeyCapturedVk = kb.vkCode;
                        _winKeyCaptureNeedsShift = wantsShiftMode;
                        _winKeyChordCancelled = false;
                    }

                    return CallNextHookEx(_kbHook, nCode, wParam, lParam);
                }

                if (_winKeyCaptureActive)
                    _winKeyChordCancelled = true;
            }

            if (msg is WM_KEYUP or WM_SYSKEYUP)
            {
                if (isWin && _winKeyCaptureActive && kb.vkCode == _winKeyCapturedVk)
                {
                    bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
                    bool shiftMatch = _winKeyCaptureNeedsShift ? shift : !shift;
                    bool triggerMenu = !_winKeyChordCancelled && shiftMatch;
                    ClearWinKeyCapture();

                    if (triggerMenu)
                    {
                        ReleaseWinKeyForShell(kb.vkCode);
                        _ui.TryEnqueue(() => MenuRequested?.Invoke());
                        return (IntPtr)1;
                    }
                }
            }
        }
        return CallNextHookEx(_kbHook, nCode, wParam, lParam);
    }

    private void ClearWinKeyCapture()
    {
        _winKeyCaptureActive = false;
        _winKeyCapturedVk = 0;
        _winKeyCaptureNeedsShift = false;
        _winKeyChordCancelled = false;
    }

    private static void ReleaseWinKeyForShell(uint vkCode)
    {
        if (vkCode is VK_LWIN or VK_RWIN)
            keybd_event((byte)vkCode, 0, KEYEVENTF_KEYUP, UIntPtr.Zero);
    }

    // ── Mouse hook ────────────────────────────────────────────────────────

    private IntPtr MouseProc(int nCode, IntPtr wParam, IntPtr lParam)
    {
        if (nCode >= 0)
        {
            int msg = (int)wParam;

            // Block the UP event that matches a DOWN we already intercepted,
            // so the Start button never receives the full click and can't open
            // the Start menu or steal focus from our flyout.
            if (msg == WM_LBUTTONUP   && _leftBlocked)   { _leftBlocked   = false; return (IntPtr)1; }
            if (msg == WM_RBUTTONUP   && _rightBlocked)  { _rightBlocked  = false; return (IntPtr)1; }
            if (msg == WM_MBUTTONUP   && _middleBlocked) { _middleBlocked = false; return (IntPtr)1; }

            if (msg is WM_LBUTTONDOWN or WM_RBUTTONDOWN or WM_MBUTTONDOWN)
            {
                var ms = Marshal.PtrToStructure<MSLLHOOKSTRUCT>(lParam);

                if (unchecked(Environment.TickCount - _startRectAge) > 5000)
                {
                    _startRect    = ResolveStartButtonRect();
                    _startRectAge = Environment.TickCount;
                }

                if (IsStartTriggerPoint(ms.pt))
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
                        // Track which button was blocked so we can suppress its UP too
                        if (msg == WM_LBUTTONDOWN) _leftBlocked   = true;
                        if (msg == WM_RBUTTONDOWN) _rightBlocked  = true;
                        if (msg == WM_MBUTTONDOWN) _middleBlocked = true;

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

        GetWindowRect(tray, out var tr);

        // Windows 10 / ExplorerPatcher / StartAllBack
        var startHwnd = FindWindowEx(tray, IntPtr.Zero, "Start", null);
        if (startHwnd != IntPtr.Zero)
        {
            GetWindowRect(startHwnd, out var r);
            if (IsReasonableStartRect(r, tr)) return r;
        }

        int h = tr.bottom - tr.top;

        // Probe the left edge only. A broad centered fallback can mistake
        // ordinary taskbar buttons for Start when Explorer exposes XAML islands.
        foreach (var probe in new[] {
            new POINT { x = tr.left + h / 2,     y = tr.top + h / 2 },
        })
        {
            var hw = WindowFromPoint(probe);
            if (hw != IntPtr.Zero && hw != tray)
            {
                GetWindowRect(hw, out var r);
                if (IsReasonableStartRect(r, tr)) return r;
            }
        }

        return default;
    }

    private static bool IsReasonableStartRect(RECT rect, RECT trayRect)
    {
        int width = rect.right - rect.left;
        int height = rect.bottom - rect.top;
        int taskbarHeight = trayRect.bottom - trayRect.top;
        int taskbarWidth = trayRect.right - trayRect.left;

        if (width < 8 || height < 8 || taskbarHeight <= 0 || taskbarWidth <= 0)
            return false;

        int maxWidth = Math.Max(80, taskbarHeight * 2);
        int maxHeight = Math.Max(80, taskbarHeight + 24);
        if (width > maxWidth || height > maxHeight)
            return false;

        return rect.right >= trayRect.left &&
               rect.left <= trayRect.right &&
               rect.bottom >= trayRect.top &&
               rect.top <= trayRect.bottom;
    }

    private static bool IsNonEmptyRect(RECT r)
        => r.right > r.left && r.bottom > r.top;

    private static bool IsStartTriggerPoint(POINT pt)
    {
        var tray = FindWindow("Shell_TrayWnd", null);
        if (tray == IntPtr.Zero) return false;

        GetWindowRect(tray, out var trayRect);
        if (!PointInRect(pt, trayRect)) return false;

        var startRect = ResolveStartButtonRect();
        if (IsNonEmptyRect(startRect) && PointInRect(pt, startRect)) return true;

        int taskbarHeight = trayRect.bottom - trayRect.top;
        int taskbarWidth = trayRect.right - trayRect.left;
        if (taskbarHeight <= 0 || taskbarWidth <= 0) return false;

        // Keep the fallback narrow. The centered taskbar area is full of normal
        // buttons, so accepting it as Start creates false triggers.
        int zone = Math.Max(56, taskbarHeight + 16);
        var leftStartZone = new RECT
        {
            left = trayRect.left,
            top = trayRect.top,
            right = trayRect.left + zone,
            bottom = trayRect.bottom
        };

        return PointInRect(pt, leftStartZone);
    }

    private static bool PointInRect(POINT pt, RECT r)
        => pt.x >= r.left && pt.x <= r.right && pt.y >= r.top && pt.y <= r.bottom;

    // ── Fullscreen guard ──────────────────────────────────────────────────

    private bool ShouldIgnore()
    {
        if (!_config.IgnoreTriggersWhenFullscreen) return false;

        var fg = GetForegroundWindow();
        if (fg == IntPtr.Zero) return false;

        GetWindowThreadProcessId(fg, out var pid);
        if (pid == 0 || pid == Environment.ProcessId) return false;
        if (IsIconic(fg)) return false;
        if (IsExplorerShellSurface(fg)) return false;
        if (!IsWindowFullscreen(fg, out var windowRect, out var monitorRect)) return false;

        var exe = "";
        try
        {
            using var proc = Process.GetProcessById((int)pid);
            exe = Path.GetFileName(proc.MainModule?.FileName ?? "");
            if (_exclusions.Contains(exe)) return false;
        }
        catch { }

        return true;
    }

    private static bool IsExplorerShellSurface(IntPtr hwnd)
    {
        var className = GetWindowClassName(hwnd);
        return className is "Progman" or "WorkerW" or "Shell_TrayWnd" or "Shell_SecondaryTrayWnd";
    }

    private static string GetWindowClassName(IntPtr hwnd)
    {
        var sb = new StringBuilder(256);
        return GetClassName(hwnd, sb, sb.Capacity) > 0 ? sb.ToString() : "";
    }

    private static bool IsWindowFullscreen(IntPtr hwnd, out RECT windowRect, out RECT monitorRect)
    {
        windowRect = default;
        monitorRect = default;
        var hMon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
        var mi   = new MONITORINFO { cbSize = (uint)Marshal.SizeOf<MONITORINFO>() };
        if (hMon == IntPtr.Zero || !GetMonitorInfo(hMon, ref mi)) return false;
        if (!GetWindowRect(hwnd, out windowRect)) return false;

        monitorRect = mi.rcMonitor;
        return Math.Abs(windowRect.left - monitorRect.left) <= 2 &&
               Math.Abs(windowRect.top - monitorRect.top) <= 2 &&
               Math.Abs(windowRect.right - monitorRect.right) <= 2 &&
               Math.Abs(windowRect.bottom - monitorRect.bottom) <= 2;
    }
}
