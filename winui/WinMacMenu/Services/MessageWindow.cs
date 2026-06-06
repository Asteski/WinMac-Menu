using WinMacMenu.Interop;

namespace WinMacMenu.Services;

/// <summary>A hidden message-only window that owns the tray icon's callback message loop.</summary>
public sealed class MessageWindow : IDisposable
{
    private readonly NativeMethods.WndProc _wndProc; // kept alive to avoid GC.
    private readonly string _className;

    public nint Handle { get; }

    /// <summary>Raised for messages forwarded by the window proc (tray callbacks, WM_COMMAND).</summary>
    public event Func<uint, nint, nint, bool>? OnMessage;

    public MessageWindow()
    {
        _wndProc = WndProc;
        _className = "WinMacMenuMsgWindow_" + Guid.NewGuid().ToString("N");
        var hInstance = NativeMethods.GetModuleHandle(null);

        var wc = new NativeMethods.WNDCLASSEX
        {
            cbSize = (uint)System.Runtime.InteropServices.Marshal.SizeOf<NativeMethods.WNDCLASSEX>(),
            lpfnWndProc = _wndProc,
            hInstance = hInstance,
            lpszClassName = _className,
        };
        NativeMethods.RegisterClassEx(ref wc);

        Handle = NativeMethods.CreateWindowEx(0, _className, "WinMacMenu", 0, 0, 0, 0, 0,
            NativeMethods.HWND_MESSAGE, 0, hInstance, 0);
    }

    private nint WndProc(nint hWnd, uint msg, nint wParam, nint lParam)
    {
        if (OnMessage?.Invoke(msg, wParam, lParam) == true)
            return 0;
        return NativeMethods.DefWindowProc(hWnd, msg, wParam, lParam);
    }

    public void Dispose()
    {
        if (Handle != 0)
            NativeMethods.DestroyWindow(Handle);
    }
}
