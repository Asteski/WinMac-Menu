using System.Runtime.InteropServices;
using Microsoft.UI.Windowing;
using Microsoft.UI.Xaml;
using Windows.Graphics;
using WinMacMenuWinUI3.Models;

namespace WinMacMenuWinUI3;

/// <summary>
/// Minimal hidden window that keeps the app alive and owns the tray icon HWND.
/// </summary>
public sealed partial class HostWindow : Window
{
    private const int GWL_EXSTYLE      = -20;
    private const int WS_EX_TOOLWINDOW = 0x00000080;
    private static readonly UIntPtr SubclassId = new(0x57554933);

    [DllImport("user32.dll")] private static extern int  GetWindowLong(IntPtr hWnd, int nIndex);
    [DllImport("user32.dll")] private static extern int  SetWindowLong(IntPtr hWnd, int nIndex, int dwNewLong);
    [DllImport("user32.dll")] private static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern bool SetWindowText(IntPtr hWnd, string text);
    [DllImport("comctl32.dll")] private static extern bool SetWindowSubclass(
        IntPtr hWnd, SubclassProc pfnSubclass, UIntPtr uIdSubclass, UIntPtr dwRefData);
    [DllImport("comctl32.dll")] private static extern bool RemoveWindowSubclass(
        IntPtr hWnd, SubclassProc pfnSubclass, UIntPtr uIdSubclass);
    [DllImport("comctl32.dll")] private static extern IntPtr DefSubclassProc(
        IntPtr hWnd, uint uMsg, IntPtr wParam, IntPtr lParam);

    private delegate IntPtr SubclassProc(IntPtr hWnd, uint uMsg, IntPtr wParam, IntPtr lParam,
                                          UIntPtr uIdSubclass, UIntPtr dwRefData);

    private const int SW_HIDE = 0;

    private readonly uint _showMessage;
    private readonly SubclassProc _subclassProc;

    public IntPtr Hwnd { get; }
    public event Action<MenuTriggerType>? ShowMenuRequested;

    public HostWindow(string? title = null, uint showMessage = 0)
    {
        _showMessage = showMessage;
        _subclassProc = SubclassProcImpl;

        InitializeComponent();

        Hwnd = WinRT.Interop.WindowNative.GetWindowHandle(this);
        if (!string.IsNullOrWhiteSpace(title))
            SetWindowText(Hwnd, title);

        var appWindow = GetAppWindowForCurrentWindow();
        appWindow.IsShownInSwitchers = false;

        // No chrome, no taskbar entry
        var exStyle = GetWindowLong(Hwnd, GWL_EXSTYLE);
        SetWindowLong(Hwnd, GWL_EXSTYLE, exStyle | WS_EX_TOOLWINDOW);

        var presenter = OverlappedPresenter.CreateForToolWindow();
        presenter.IsResizable   = false;
        presenter.IsMaximizable = false;
        presenter.IsMinimizable = false;
        presenter.SetBorderAndTitleBar(false, false);
        appWindow.SetPresenter(presenter);

        // Place it on-screen first (some WinUI3 builds reject off-screen-only windows)
        // then immediately hide it at the Win32 level so nothing is ever visible.
        appWindow.MoveAndResize(new RectInt32(0, 0, 1, 1));
        if (_showMessage != 0)
            SetWindowSubclass(Hwnd, _subclassProc, SubclassId, UIntPtr.Zero);
        ShowWindow(Hwnd, SW_HIDE);

        Closed += (_, _) =>
        {
            if (_showMessage != 0)
                RemoveWindowSubclass(Hwnd, _subclassProc, SubclassId);
        };
    }

    private AppWindow GetAppWindowForCurrentWindow()
    {
        var wndId = Microsoft.UI.Win32Interop.GetWindowIdFromWindow(Hwnd);
        return AppWindow.GetFromWindowId(wndId);
    }

    private IntPtr SubclassProcImpl(IntPtr hWnd, uint uMsg, IntPtr wParam, IntPtr lParam,
                                     UIntPtr uIdSubclass, UIntPtr dwRefData)
    {
        if (_showMessage != 0 && uMsg == _showMessage)
        {
            var triggerValue = wParam.ToInt32();
            var trigger = Enum.IsDefined(typeof(MenuTriggerType), triggerValue)
                ? (MenuTriggerType)triggerValue
                : MenuTriggerType.Other;
            ShowMenuRequested?.Invoke(trigger);
            return IntPtr.Zero;
        }

        return DefSubclassProc(hWnd, uMsg, wParam, lParam);
    }
}
