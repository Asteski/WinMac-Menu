using System.Runtime.InteropServices;
using Microsoft.UI.Windowing;
using Microsoft.UI.Xaml;
using Windows.Graphics;

namespace WinMacMenuWinUI3;

/// <summary>
/// Minimal hidden window that keeps the app alive and owns the tray icon HWND.
/// </summary>
public sealed partial class HostWindow : Window
{
    private const int GWL_EXSTYLE      = -20;
    private const int WS_EX_TOOLWINDOW = 0x00000080;

    [DllImport("user32.dll")] private static extern int  GetWindowLong(IntPtr hWnd, int nIndex);
    [DllImport("user32.dll")] private static extern int  SetWindowLong(IntPtr hWnd, int nIndex, int dwNewLong);
    [DllImport("user32.dll")] private static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);
    private const int SW_HIDE = 0;

    public IntPtr Hwnd { get; }

    public HostWindow()
    {
        InitializeComponent();

        Hwnd = WinRT.Interop.WindowNative.GetWindowHandle(this);

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
        ShowWindow(Hwnd, SW_HIDE);
    }

    private AppWindow GetAppWindowForCurrentWindow()
    {
        var wndId = Microsoft.UI.Win32Interop.GetWindowIdFromWindow(Hwnd);
        return AppWindow.GetFromWindowId(wndId);
    }
}
