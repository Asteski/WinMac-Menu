using Microsoft.UI.Xaml.Controls.Primitives;
using WinMacMenu.Interop;
using WinMacMenu.Models;

namespace WinMacMenu.Services;

/// <summary>Screen point (physical pixels) plus a flyout direction hint for the popup.</summary>
public readonly record struct PlacementResult(int X, int Y, FlyoutPlacementMode Placement);

/// <summary>Computes where the menu should appear, mirroring the [Placement] rules in main.c.</summary>
public static class PlacementCalculator
{
    public static PlacementResult Compute(Config cfg)
    {
        NativeMethods.GetCursorPos(out var cursor);

        // The monitor under the cursor (work area excludes the taskbar).
        var mon = NativeMethods.MonitorFromPoint(cursor, NativeMethods.MONITOR_DEFAULTTONEAREST);
        var mi = new NativeMethods.MONITORINFO { cbSize = System.Runtime.InteropServices.Marshal.SizeOf<NativeMethods.MONITORINFO>() };
        NativeMethods.GetMonitorInfo(mon, ref mi);
        var work = mi.rcWork;

        if (cfg.PointerRelative)
        {
            int hx = cfg.IgnoreHOffsetWhenRelative ? 0 : cfg.HOffset;
            int vy = cfg.IgnoreVOffsetWhenRelative ? 0 : cfg.VOffset;
            return new PlacementResult(cursor.X + hx, cursor.Y + vy, FlyoutPlacementMode.BottomEdgeAlignedRight);
        }

        bool ignoreH = cfg.HPlacement == HorizontalPlacement.Center && cfg.IgnoreHOffsetWhenCentered;
        bool ignoreV = cfg.VPlacement == VerticalPlacement.Center && cfg.IgnoreVOffsetWhenCentered;
        int hOff = ignoreH ? 0 : cfg.HOffset;
        int vOff = ignoreV ? 0 : cfg.VOffset;

        int x = cfg.HPlacement switch
        {
            HorizontalPlacement.Center => (work.Left + work.Right) / 2,
            HorizontalPlacement.Right => work.Right - hOff,
            _ => work.Left + hOff,
        };
        int y = cfg.VPlacement switch
        {
            VerticalPlacement.Center => (work.Top + work.Bottom) / 2,
            VerticalPlacement.Bottom => work.Bottom - vOff,
            _ => work.Top + vOff,
        };

        // Open the menu away from the anchored edge so it stays on-screen.
        var placement = cfg.VPlacement == VerticalPlacement.Bottom
            ? FlyoutPlacementMode.TopEdgeAlignedLeft
            : FlyoutPlacementMode.BottomEdgeAlignedLeft;

        return new PlacementResult(x, y, placement);
    }
}
