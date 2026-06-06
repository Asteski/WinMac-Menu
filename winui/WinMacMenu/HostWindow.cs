using Microsoft.UI;
using Microsoft.UI.Windowing;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Controls.Primitives;
using Microsoft.UI.Xaml.Media;
using Windows.Graphics;
using WinMacMenu.Interop;
using WinMacMenu.Menu;
using WinMacMenu.Models;
using WinMacMenu.Services;

namespace WinMacMenu;

/// <summary>
/// A near-invisible (1×1) borderless host window. The WinUI <see cref="MenuFlyout"/> is a
/// top-level popup, so anchoring it to this tiny window at the computed placement point makes
/// the menu appear there with the native Windows 11 styling, light-dismiss and edge-flipping.
/// </summary>
public sealed class HostWindow : Window
{
    // Parked well outside any monitor so the 1×1 window is invisible while staying "shown".
    private const int OffScreen = -32000;

    private readonly Grid _root;
    private readonly nint _hwnd;
    private MenuFlyout? _flyout;

    public bool IsMenuOpen { get; private set; }

    /// <summary>Raised after the menu closes (light-dismiss, selection or Escape).</summary>
    public event Action? MenuClosed;

    public HostWindow()
    {
        _root = new Grid { Background = new SolidColorBrush(Microsoft.UI.Colors.Transparent) };
        Content = _root;

        _hwnd = WinRT.Interop.WindowNative.GetWindowHandle(this);

        var presenter = OverlappedPresenter.Create();
        presenter.SetBorderAndTitleBar(false, false);
        presenter.IsResizable = false;
        presenter.IsMaximizable = false;
        presenter.IsMinimizable = false;
        presenter.IsAlwaysOnTop = true;
        AppWindow.SetPresenter(presenter);
        AppWindow.IsShownInSwitchers = false;

        // Keep a live (shown but off-screen, unfocused) window so the app stays alive in
        // background mode without anything visible. WinUI tears the app down if no window
        // is ever shown, so we never Hide(); we just park it off-screen between menus.
        AppWindow.MoveAndResize(new RectInt32(OffScreen, OffScreen, 1, 1));
        AppWindow.Show(activateWindow: false);
    }

    public void ShowMenu(Config cfg)
    {
        if (IsMenuOpen)
            return;

        var placement = PlacementCalculator.Compute(cfg);
        AppWindow.MoveAndResize(new RectInt32(placement.X, placement.Y, 1, 1));
        AppWindow.Show(activateWindow: true);
        NativeMethods.SetForegroundWindow(_hwnd);
        Activate();

        bool light = ThemeHelper.IsLightTheme();
        _root.RequestedTheme = light ? ElementTheme.Light : ElementTheme.Dark;

        var icons = new IconResolver(DispatcherQueue);
        _flyout = new MenuBuilder(cfg, icons, light).Build();
        _flyout.Placement = placement.Placement;
        _flyout.Closed += OnFlyoutClosed;

        IsMenuOpen = true;

        // Defer until the root has a XamlRoot (first activation) before anchoring the flyout.
        void Open()
        {
            if (_root.XamlRoot is null)
            {
                DispatcherQueue.TryEnqueue(Open);
                return;
            }
            _flyout.ShowAt(_root, new FlyoutShowOptions
            {
                Position = new Windows.Foundation.Point(0, 0),
                Placement = placement.Placement,
                ShowMode = FlyoutShowMode.Standard,
            });
        }
        Open();
    }

    public void CloseMenu() => _flyout?.Hide();

    private void OnFlyoutClosed(object? sender, object e)
    {
        if (_flyout is not null)
            _flyout.Closed -= OnFlyoutClosed;
        _flyout = null;
        IsMenuOpen = false;
        // Park off-screen again, but keep the window shown so the app stays alive.
        AppWindow.MoveAndResize(new RectInt32(OffScreen, OffScreen, 1, 1));
        MenuClosed?.Invoke();
    }
}
