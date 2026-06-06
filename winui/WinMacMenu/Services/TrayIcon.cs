using WinMacMenu.Interop;
using WinMacMenu.Models;

namespace WinMacMenu.Services;

/// <summary>System-tray icon with a native context menu, mirroring the C app's tray.</summary>
public sealed class TrayIcon : IDisposable
{
    private const uint CallbackMessage = NativeMethods.WM_APP + 1;
    private const uint CmdShowMenu = 1;
    private const uint CmdStartOnLogin = 2;
    private const uint CmdReload = 3;
    private const uint CmdExit = 9;

    private readonly MessageWindow _window;
    private readonly Config _cfg;
    private NativeMethods.NOTIFYICONDATA _data;
    private nint _hIcon;
    private bool _added;

    public event Action? ShowMenuRequested;
    public event Action? ExitRequested;
    public event Action? ReloadRequested;
    public event Action<bool>? StartOnLoginToggled;

    public TrayIcon(MessageWindow window, Config cfg)
    {
        _window = window;
        _cfg = cfg;
        _window.OnMessage += HandleMessage;
    }

    public void Show()
    {
        _hIcon = LoadTrayIcon();
        var tip = BuildTip();
        _data = new NativeMethods.NOTIFYICONDATA
        {
            cbSize = System.Runtime.InteropServices.Marshal.SizeOf<NativeMethods.NOTIFYICONDATA>(),
            hWnd = _window.Handle,
            uID = 1,
            uFlags = NativeMethods.NIF_MESSAGE | NativeMethods.NIF_ICON | NativeMethods.NIF_TIP,
            uCallbackMessage = CallbackMessage,
            hIcon = _hIcon,
            szTip = tip,
        };
        _added = NativeMethods.Shell_NotifyIcon(NativeMethods.NIM_ADD, ref _data);
    }

    private bool HandleMessage(uint msg, nint wParam, nint lParam)
    {
        if (msg == CallbackMessage)
        {
            uint mouse = (uint)(lParam.ToInt64() & 0xFFFF);
            switch (mouse)
            {
                case NativeMethods.WM_LBUTTONUP:
                case NativeMethods.WM_LBUTTONDBLCLK:
                    ShowMenuRequested?.Invoke();
                    return true;
                case NativeMethods.WM_RBUTTONUP:
                case NativeMethods.WM_CONTEXTMENU:
                    ShowContextMenu();
                    return true;
            }
        }
        return false;
    }

    private void ShowContextMenu()
    {
        nint menu = NativeMethods.CreatePopupMenu();
        if (menu == 0) return;
        try
        {
            NativeMethods.AppendMenuW(menu, NativeMethods.MF_STRING, CmdShowMenu, "WinMac Menu");
            NativeMethods.AppendMenuW(menu, NativeMethods.MF_STRING, CmdReload, "Reload config");
            NativeMethods.AppendMenuW(menu, NativeMethods.MF_SEPARATOR, 0, null);
            uint loginFlags = NativeMethods.MF_STRING | (StartupRegistry.IsEnabled() ? NativeMethods.MF_CHECKED : 0);
            NativeMethods.AppendMenuW(menu, loginFlags, CmdStartOnLogin, "Start on login");
            NativeMethods.AppendMenuW(menu, NativeMethods.MF_SEPARATOR, 0, null);
            NativeMethods.AppendMenuW(menu, NativeMethods.MF_STRING, CmdExit, "Exit");

            NativeMethods.GetCursorPos(out var pt);
            // Required so the menu dismisses correctly when the user clicks elsewhere.
            NativeMethods.SetForegroundWindow(_window.Handle);
            uint cmd = NativeMethods.TrackPopupMenu(menu,
                NativeMethods.TPM_RETURNCMD | NativeMethods.TPM_RIGHTBUTTON, pt.X, pt.Y, 0, _window.Handle, 0);

            switch (cmd)
            {
                case CmdShowMenu: ShowMenuRequested?.Invoke(); break;
                case CmdReload: ReloadRequested?.Invoke(); break;
                case CmdStartOnLogin: StartOnLoginToggled?.Invoke(!StartupRegistry.IsEnabled()); break;
                case CmdExit: ExitRequested?.Invoke(); break;
            }
        }
        finally
        {
            NativeMethods.DestroyMenu(menu);
        }
    }

    private string BuildTip()
    {
        var iniName = Path.GetFileName(_cfg.IniPath);
        return iniName.Equals("config.ini", StringComparison.OrdinalIgnoreCase)
            ? "WinMac Menu"
            : $"WinMac Menu - {iniName}";
    }

    private nint LoadTrayIcon()
    {
        const uint LR_LOADFROMFILE = 0x00000010;
        // Prefer themed monochrome tray icons when configured; otherwise fall back to app.ico.
        bool light = ThemeHelper.IsLightTheme();
        string? path = null;
        if (_cfg.MonochromeTrayIcon)
            path = light ? FirstExisting(_cfg.TrayIconPathLight, _cfg.TrayIconPath)
                         : FirstExisting(_cfg.TrayIconPathDark, _cfg.TrayIconPath);
        path ??= FirstExisting(
            System.IO.Path.Combine(AppContext.BaseDirectory, "Assets", light ? "tray_light.ico" : "tray_dark.ico"),
            System.IO.Path.Combine(AppContext.BaseDirectory, "Assets", "app.ico"));

        if (path is not null)
        {
            nint name = System.Runtime.InteropServices.Marshal.StringToHGlobalUni(path);
            try
            {
                nint h = NativeMethods.LoadImage(0, name, NativeMethods.IMAGE_ICON, 16, 16,
                    LR_LOADFROMFILE | NativeMethods.LR_DEFAULTCOLOR);
                if (h != 0) return h;
            }
            finally
            {
                System.Runtime.InteropServices.Marshal.FreeHGlobal(name);
            }
        }
        // Last resort: a generic application icon.
        return NativeMethods.LoadIconW(0, 32512 /* IDI_APPLICATION */);
    }

    private static string? FirstExisting(params string[] paths)
        => paths.FirstOrDefault(p => !string.IsNullOrEmpty(p) && File.Exists(p));

    public void Dispose()
    {
        _window.OnMessage -= HandleMessage;
        if (_added)
        {
            NativeMethods.Shell_NotifyIcon(NativeMethods.NIM_DELETE, ref _data);
            _added = false;
        }
        if (_hIcon != 0)
        {
            NativeMethods.DestroyIcon(_hIcon);
            _hIcon = 0;
        }
    }
}
