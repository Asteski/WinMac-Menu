using WinMacMenu.Interop;
using WinMacMenu.Models;

namespace WinMacMenu.Services;

/// <summary>
/// Optional global Windows-key trigger via a low-level keyboard hook. A "clean" tap of the
/// Windows key (no other key pressed in between) toggles the menu and is swallowed so the
/// system Start menu does not also open. Honours [Controls] WindowsKey / ShiftWindowsKey.
/// </summary>
public sealed class KeyboardTrigger : IDisposable
{
    private readonly Config _cfg;
    private readonly Action _onToggle;
    private readonly NativeMethods.LowLevelKeyboardProc _proc; // kept alive to avoid GC.
    private nint _hook;

    private bool _winDown;
    private bool _otherKeyWhileWin;
    private bool _shiftAtPress;

    public KeyboardTrigger(Config cfg, Action onToggle)
    {
        _cfg = cfg;
        _onToggle = onToggle;
        _proc = HookProc;
    }

    public bool ShouldInstall => _cfg.WindowsKeyTrigger || _cfg.ShiftWindowsKeyTrigger;

    public void Install()
    {
        if (!ShouldInstall || _hook != 0) return;
        // hmod = 0 is fine for a managed delegate in the current process for WH_KEYBOARD_LL.
        _hook = NativeMethods.SetWindowsHookExW(NativeMethods.WH_KEYBOARD_LL, _proc, 0, 0);
    }

    private nint HookProc(int nCode, nint wParam, nint lParam)
    {
        if (nCode >= 0)
        {
            var data = System.Runtime.InteropServices.Marshal.PtrToStructure<NativeMethods.KBDLLHOOKSTRUCT>(lParam);
            int msg = (int)wParam;
            bool isWin = data.vkCode is NativeMethods.VK_LWIN or NativeMethods.VK_RWIN;
            bool isKeyDown = msg is NativeMethods.WM_KEYDOWN or NativeMethods.WM_SYSKEYDOWN;
            bool isKeyUp = !isKeyDown;

            if (isWin && isKeyDown)
            {
                if (!_winDown)
                {
                    _winDown = true;
                    _otherKeyWhileWin = false;
                    _shiftAtPress = (NativeMethods.GetAsyncKeyState(NativeMethods.VK_SHIFT) & 0x8000) != 0;
                }
            }
            else if (isWin && isKeyUp && _winDown)
            {
                _winDown = false;
                if (!_otherKeyWhileWin && TryToggle(_shiftAtPress))
                    return 1; // swallow so Start menu doesn't open.
            }
            else if (isKeyDown && _winDown)
            {
                _otherKeyWhileWin = true; // Win is acting as a modifier — leave it alone.
            }
        }
        return NativeMethods.CallNextHookEx(_hook, nCode, wParam, lParam);
    }

    private bool TryToggle(bool shift)
    {
        bool fire = shift ? _cfg.ShiftWindowsKeyTrigger : _cfg.WindowsKeyTrigger;
        if (!fire) return false;
        _onToggle();
        return true;
    }

    public void Dispose()
    {
        if (_hook != 0)
        {
            NativeMethods.UnhookWindowsHookEx(_hook);
            _hook = 0;
        }
    }
}
