using System.Runtime.InteropServices;
using Microsoft.UI;
using Microsoft.UI.Windowing;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Controls.Primitives;
using Windows.Foundation;
using Windows.Graphics;
using WinMacMenuWinUI3.Models;
using WinMacMenuWinUI3.Services;

namespace WinMacMenuWinUI3;

public sealed partial class MenuWindow : Window
{
    private const int GWL_EXSTYLE      = -20;
    private const int WS_EX_TOOLWINDOW = 0x00000080;
    private const int WS_EX_TOPMOST    = 0x00000008;
    private const int WS_EX_LAYERED    = 0x00080000;
    private const uint LWA_ALPHA       = 0x00000002;
    private static readonly IntPtr HWND_TOPMOST = new(-1);
    private const uint SWP_NOSIZE       = 0x0001;
    private const uint SWP_NOMOVE       = 0x0002;
    private const uint SWP_NOACTIVATE   = 0x0010;
    private const uint SWP_SHOWWINDOW   = 0x0040;
    private const int DWMWA_WINDOW_CORNER_PREFERENCE = 33;
    private const int DWMWCP_DONOTROUND = 1;

    [DllImport("user32.dll")] private static extern int  GetWindowLong(IntPtr hWnd, int nIndex);
    [DllImport("user32.dll")] private static extern int  SetWindowLong(IntPtr hWnd, int nIndex, int dwNewLong);
    [DllImport("user32.dll")] private static extern bool SetWindowPos(IntPtr hWnd, IntPtr hWndInsertAfter, int x, int y, int cx, int cy, uint flags);
    [DllImport("user32.dll")] private static extern bool SetLayeredWindowAttributes(IntPtr hwnd, uint crKey, byte bAlpha, uint dwFlags);
    [DllImport("user32.dll")] private static extern int SetWindowRgn(IntPtr hWnd, IntPtr hRgn, bool bRedraw);
    [DllImport("dwmapi.dll")] private static extern int DwmSetWindowAttribute(IntPtr hwnd, int dwAttribute, ref int pvAttribute, int cbAttribute);
    [DllImport("gdi32.dll")] private static extern IntPtr CreateRectRgn(int x1, int y1, int x2, int y2);
    [DllImport("gdi32.dll")] private static extern bool DeleteObject(IntPtr hObject);
    [DllImport("user32.dll")] private static extern IntPtr SetWindowsHookEx(int idHook, MouseHookProc lpfn, IntPtr hMod, uint dwThreadId);
    [DllImport("user32.dll")] private static extern bool UnhookWindowsHookEx(IntPtr hhk);
    [DllImport("user32.dll")] private static extern IntPtr CallNextHookEx(IntPtr hhk, int nCode, IntPtr wParam, IntPtr lParam);

    [StructLayout(LayoutKind.Sequential)]
    private struct POINT { public int x; public int y; }
    [StructLayout(LayoutKind.Sequential)]
    private struct MSLLHOOKSTRUCT { public POINT pt; public uint mouseData, flags, time; public UIntPtr extraInfo; }

    [DllImport("user32.dll")] private static extern bool GetCursorPos(out POINT lpPoint);
    [DllImport("user32.dll")] private static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] private static extern IntPtr WindowFromPoint(POINT point);
    [DllImport("user32.dll")] private static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint processId);
    [DllImport("user32.dll")] private static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern IntPtr FindWindow(string? className, string? windowName);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern IntPtr FindWindowEx(IntPtr parent, IntPtr after, string? className, string? windowName);

    private delegate IntPtr MouseHookProc(int nCode, IntPtr wParam, IntPtr lParam);
    private const int WH_MOUSE_LL = 14;
    private const int WM_LBUTTONDOWN = 0x0201;
    private const int WM_RBUTTONDOWN = 0x0204;
    private const int WM_MBUTTONDOWN = 0x0207;
    private const int WM_XBUTTONDOWN = 0x020B;

    private readonly AppConfig _config;
    private readonly string _iniPath;
    private readonly MenuTriggerType _trigger;
    private readonly int? _parentPid;
    private readonly MouseHookProc _outsideClickProc;
    private IntPtr _outsideClickHook;
    private MenuFlyout? _flyout;
    private Point _flyoutAnchorPoint;
    private bool _isClosing;
    private int _ignoreOutsideClicksUntil;

    private const double DefaultMenuItemHeight = 40;
    private const double DefaultMenuSeparatorHeight = 9;
    private const double CompactMenuItemMinHeight = 32;
    private const double CompactMenuSeparatorHeight = 8;
    private const double SubmenuMaxWidthScreenFraction = 0.25;
    private const double SubmenuMinMaxWidth = 280;
    private const double SubmenuTextReservedWidth = 104;
    private const int FolderCacheLimit = 256;

    private static readonly object FolderCacheLock = new();
    private static readonly Dictionary<FolderCacheKey, FolderCacheEntry> FolderCache = new();
    private static readonly object TaskKillCacheLock = new();
    private static List<ProcessEntry>? TaskKillCache;
    private static string TaskKillCacheKey = "";
    private static DateTime TaskKillCacheTimeUtc = DateTime.MinValue;
    private static bool TaskKillRefreshInProgress;

    public MenuWindow(AppConfig config, string iniPath, MenuTriggerType trigger, int? parentPid)
    {
        _config = config;
        _iniPath = iniPath;
        _trigger = trigger;
        _parentPid = parentPid;
        _outsideClickProc = OutsideClickProc;

        InitializeComponent();

        ConfigureWindowChrome();

        RootGrid.Loaded += (_, _) => ShowFlyout();
        Closed += (_, _) => RemoveOutsideClickHook();
    }

    private void ShowFlyout()
    {
        try
        {
            _flyout = new MenuFlyout
            {
                Placement = FlyoutPlacementMode.BottomEdgeAlignedLeft
            };

            BuildItems(_flyout.Items, _config.Items);
            if (!_config.Items.Any(i => i.Type == ConfigItemType.Settings) && ShouldShowAutomaticSettingsItem())
            {
                if (_flyout.Items.Count > 0 && _flyout.Items[^1] is not MenuFlyoutSeparator)
                    _flyout.Items.Add(new MenuFlyoutSeparator());
                AddItem(_flyout.Items, new ConfigItem
                {
                    Label = "WinMac Menu Settings",
                    Type = ConfigItemType.Settings
                });
            }
            NormalizeFlyoutMetrics(_flyout.Items);

            _flyout.Closed += (_, _) => CloseMenuWindow();

            _ignoreOutsideClicksUntil = Environment.TickCount + 250;
            InstallOutsideClickHook();
            BringAnchorWindowToTop();
            _flyout.ShowAt(RootGrid, _flyoutAnchorPoint);
        }
        catch (Exception ex)
        {
            App.LogException("ShowFlyout", ex);
            CloseMenuWindow();
        }
    }

    private bool ShouldShowAutomaticSettingsItem() => _config.ShowSettingsItem switch
    {
        SettingsItemMode.Shift => _trigger == MenuTriggerType.Shift,
        SettingsItemMode.WinX => _trigger == MenuTriggerType.WinX,
        SettingsItemMode.RightClick => _trigger == MenuTriggerType.RightClick,
        SettingsItemMode.MiddleClick => _trigger == MenuTriggerType.MiddleClick,
        SettingsItemMode.Always => true,
        _ => false,
    };

    private void NormalizeFlyoutMetrics(IList<MenuFlyoutItemBase> items, int depth = 0)
    {
        var compact = !string.Equals(_config.WinUI3Size, "Large", StringComparison.OrdinalIgnoreCase);
        var separatorHeight = compact ? CompactMenuSeparatorHeight : DefaultMenuSeparatorHeight;
        var submenuMaxWidth = depth > 0 ? GetSubmenuMaxWidth() : double.PositiveInfinity;

        foreach (var item in items)
        {
            if (item is FrameworkElement element)
            {
                element.MaxWidth = submenuMaxWidth;

                if (item is MenuFlyoutSeparator)
                {
                    element.Height = separatorHeight;
                    element.MinHeight = separatorHeight;
                }
                else if (compact)
                {
                    element.Height = double.NaN;
                    element.MinHeight = CompactMenuItemMinHeight;
                }
                else
                {
                    element.Height = DefaultMenuItemHeight;
                    element.MinHeight = DefaultMenuItemHeight;
                }
            }

            if (compact && item is MenuFlyoutItem menuItem)
            {
                menuItem.Padding = new Thickness(11, 4, 11, 5);
                menuItem.VerticalContentAlignment = VerticalAlignment.Center;
            }

            if (compact && item is MenuFlyoutSubItem submenuItem)
            {
                submenuItem.Padding = new Thickness(11, 4, 11, 5);
                submenuItem.VerticalContentAlignment = VerticalAlignment.Center;
            }

            if (depth > 0 && item is MenuFlyoutItemBase textItem)
                TrimSubmenuText(textItem, submenuMaxWidth);

            if (item is MenuFlyoutSubItem submenu)
                NormalizeFlyoutMetrics(submenu.Items, depth + 1);
        }
    }

    private static void TrimSubmenuText(MenuFlyoutItemBase item, double maxWidth)
    {
        if (double.IsInfinity(maxWidth) || maxWidth <= 0)
            return;

        var original = item switch
        {
            MenuFlyoutItem menuItem => menuItem.Text,
            MenuFlyoutSubItem submenuItem => submenuItem.Text,
            _ => null
        };

        if (string.IsNullOrEmpty(original))
            return;

        var availableTextWidth = Math.Max(64, maxWidth - SubmenuTextReservedWidth);
        var trimmed = TrimTextWithEllipsis(original, availableTextWidth);
        if (trimmed == original)
            return;

        switch (item)
        {
            case MenuFlyoutItem menuItem:
                menuItem.Text = trimmed;
                break;
            case MenuFlyoutSubItem submenuItem:
                submenuItem.Text = trimmed;
                break;
        }

        ToolTipService.SetToolTip(item, original);
    }

    private static string TrimTextWithEllipsis(string text, double maxWidth)
    {
        const string ellipsis = "...";
        if (MeasureMenuText(text) <= maxWidth)
            return text;

        var low = 0;
        var high = text.Length;
        while (low < high)
        {
            var mid = (low + high + 1) / 2;
            if (MeasureMenuText(text[..mid] + ellipsis) <= maxWidth)
                low = mid;
            else
                high = mid - 1;
        }

        return low <= 0 ? ellipsis : text[..low].TrimEnd() + ellipsis;
    }

    private static double MeasureMenuText(string text)
    {
        using var bitmap = new System.Drawing.Bitmap(1, 1);
        using var graphics = System.Drawing.Graphics.FromImage(bitmap);
        using var font = new System.Drawing.Font("Segoe UI", 10.5f, System.Drawing.FontStyle.Regular, System.Drawing.GraphicsUnit.Point);
        return graphics.MeasureString(text, font, int.MaxValue, System.Drawing.StringFormat.GenericTypographic).Width;
    }

    private double GetSubmenuMaxWidth()
    {
        try
        {
            DisplayArea display;
            if (_config.PointerRelative && GetCursorPos(out var cursor))
                display = DisplayArea.GetFromPoint(new PointInt32(cursor.x, cursor.y), DisplayAreaFallback.Nearest);
            else
                display = DisplayArea.Primary;

            var dpi = (int)GetDpiForWindow(WinRT.Interop.WindowNative.GetWindowHandle(this));
            if (dpi <= 0) dpi = 96;

            var maxWidth = display.WorkArea.Width * SubmenuMaxWidthScreenFraction * 96.0 / dpi;
            return Math.Max(SubmenuMinMaxWidth, maxWidth);
        }
        catch
        {
            return SubmenuMinMaxWidth;
        }
    }

    private void CloseMenuWindow()
    {
        if (_isClosing) return;
        _isClosing = true;
        RemoveOutsideClickHook();
        Close();
    }

    private void InstallOutsideClickHook()
    {
        if (_outsideClickHook != IntPtr.Zero) return;
        _outsideClickHook = SetWindowsHookEx(WH_MOUSE_LL, _outsideClickProc, IntPtr.Zero, 0);
    }

    private void RemoveOutsideClickHook()
    {
        if (_outsideClickHook == IntPtr.Zero) return;
        UnhookWindowsHookEx(_outsideClickHook);
        _outsideClickHook = IntPtr.Zero;
    }

    private IntPtr OutsideClickProc(int nCode, IntPtr wParam, IntPtr lParam)
    {
        if (nCode >= 0 && (int)wParam is WM_LBUTTONDOWN or WM_RBUTTONDOWN or WM_MBUTTONDOWN or WM_XBUTTONDOWN)
        {
            if (unchecked(Environment.TickCount - _ignoreOutsideClicksUntil) < 0)
                return CallNextHookEx(_outsideClickHook, nCode, wParam, lParam);

            var mouse = Marshal.PtrToStructure<MSLLHOOKSTRUCT>(lParam);
            if (IsStartTriggerPoint(mouse.pt))
                return CallNextHookEx(_outsideClickHook, nCode, wParam, lParam);

            var hwndAtPoint = WindowFromPoint(mouse.pt);
            GetWindowThreadProcessId(hwndAtPoint, out var pidAtPoint);

            if (pidAtPoint != Environment.ProcessId)
            {
                DispatcherQueue.TryEnqueue(() =>
                {
                    _flyout?.Hide();
                    CloseMenuWindow();
                });
            }
        }

        return CallNextHookEx(_outsideClickHook, nCode, wParam, lParam);
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct RECT { public int left, top, right, bottom; }

    private static RECT ResolveStartButtonRect()
    {
        var tray = FindWindow("Shell_TrayWnd", null);
        if (tray == IntPtr.Zero) return default;

        GetWindowRect(tray, out var trayRect);

        var startHwnd = FindWindowEx(tray, IntPtr.Zero, "Start", null);
        if (startHwnd != IntPtr.Zero)
        {
            GetWindowRect(startHwnd, out var startRect);
            if (IsReasonableStartRect(startRect, trayRect)) return startRect;
        }

        int h = trayRect.bottom - trayRect.top;

        foreach (var probe in new[]
        {
            new POINT { x = trayRect.left + h / 2, y = trayRect.top + h / 2 },
        })
        {
            var hwnd = WindowFromPoint(probe);
            if (hwnd != IntPtr.Zero && hwnd != tray)
            {
                GetWindowRect(hwnd, out var rect);
                if (IsReasonableStartRect(rect, trayRect)) return rect;
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

    private static bool IsNonEmptyRect(RECT rect)
        => rect.right > rect.left && rect.bottom > rect.top;

    private static bool PointInRect(POINT pt, RECT rect)
        => pt.x >= rect.left && pt.x <= rect.right && pt.y >= rect.top && pt.y <= rect.bottom;

    private static bool IsStartTriggerPoint(POINT pt)
    {
        var tray = FindWindow("Shell_TrayWnd", null);
        if (tray == IntPtr.Zero) return false;

        GetWindowRect(tray, out var trayRect);
        if (!PointInRect(pt, trayRect)) return false;

        var startRect = ResolveStartButtonRect();
        if (IsNonEmptyRect(startRect) && PointInRect(pt, startRect)) return true;

        int taskbarHeight = trayRect.bottom - trayRect.top;
        if (taskbarHeight <= 0) return false;

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

    // inSubmenu = true  →  respect ShowFolderIcons / ShowFileIcons
    // inSubmenu = false →  respect ShowIcons (root menu)
    private void BuildItems(IList<MenuFlyoutItemBase> target, IEnumerable<ConfigItem> items,
                            bool inSubmenu = false)
    {
        foreach (var item in items)
        {
            try { AddItem(target, item, inSubmenu); }
            catch { /* skip broken items so the rest of the menu still shows */ }
        }
    }

    private void AddItem(IList<MenuFlyoutItemBase> target, ConfigItem item, bool inSubmenu = false)
    {
        if ((item.Type == ConfigItemType.Folder || item.Type == ConfigItemType.FolderSubmenu) && item.InlineExpand)
        {
            AddInlineFolder(target, item, inSubmenu);
            return;
        }

        target.Add(CreateFlyoutItem(item, inSubmenu));
    }

    private void AddInlineFolder(IList<MenuFlyoutItemBase> target, ConfigItem item, bool inSubmenu)
    {
        if (!item.InlineNoHeader)
        {
            if (item.InlineOpen)
            {
                var header = new MenuFlyoutItem { Text = item.Label };
                ApplyIcon(header, item, inSubmenu);
                var path = item.Path;
                header.Click += (_, _) => { CloseMenuWindow(); CommandExecutor.ShellOpen(path); };
                target.Add(header);
            }
            else
            {
                var header = new MenuFlyoutItem { Text = item.Label, IsEnabled = false };
                ApplyIcon(header, item, inSubmenu);
                target.Add(header);
            }
        }

        AddFolderEntries(target, item.Path, depth: 1);
    }

    private MenuFlyoutItemBase CreateFlyoutItem(ConfigItem item, bool inSubmenu = false)
    {
        if (item.IsSeparator)
            return new MenuFlyoutSeparator();

        if (item.IsCategory)
            return new MenuFlyoutItem { Text = item.Label, IsEnabled = false };

        switch (item.Type)
        {
            // ── Power menu ───────────────────────────────────────────────
            case ConfigItemType.PowerMenu:
            {
                var sub = new MenuFlyoutSubItem
                {
                    Text = string.IsNullOrWhiteSpace(item.Label) ? "Shut down or sign out" : item.Label
                };
                ApplyIcon(sub, item, inSubmenu);
                foreach (var pi in BuildPowerItems()) sub.Items.Add(pi);
                return sub;
            }

            // ── Task kill ────────────────────────────────────────────────
            case ConfigItemType.TaskKill:
            {
                return CreateTaskKillSubmenu(item, inSubmenu);
            }

            // ── Folder submenu ───────────────────────────────────────────
            case ConfigItemType.FolderSubmenu:
            case ConfigItemType.Folder when item.Submenu:
            {
                var sub = new MenuFlyoutSubItem { Text = item.Label };
                ApplyIcon(sub, item, inSubmenu);
                AddFolderEntries(sub.Items, item.Path, depth: 1);
                return sub;
            }

            // ── Recent items ─────────────────────────────────────────────
            case ConfigItemType.Recent:
            case ConfigItemType.RecentSubmenu:
            {
                var sub = new MenuFlyoutSubItem { Text = item.Label };
                ApplyIcon(sub, item, inSubmenu);
                AddRecentItems(sub.Items);
                return sub;
            }

            // ── This PC (drives) ─────────────────────────────────────────
            case ConfigItemType.ThisPC:
            {
                if (!_config.ThisPCAsSubmenu)
                {
                    // Plain clickable item — opens File Explorer at This PC
                    var fi = new MenuFlyoutItem { Text = item.Label };
                    ApplyIcon(fi, item, inSubmenu);
                    fi.Click += (_, _) => { CloseMenuWindow(); CommandExecutor.ShellOpen("shell:MyComputerFolder"); };
                    return fi;
                }
                var sub = new MenuFlyoutSubItem { Text = item.Label };
                ApplyIcon(sub, item, inSubmenu);
                AddThisPCItems(sub.Items);
                return sub;
            }

            // ── Home folder ──────────────────────────────────────────────
            case ConfigItemType.Home:
            {
                var homePath = Environment.GetFolderPath(Environment.SpecialFolder.UserProfile);
                if (!_config.HomeAsSubmenu)
                {
                    var fi = new MenuFlyoutItem { Text = item.Label };
                    ApplyIcon(fi, item, inSubmenu);
                    fi.Click += (_, _) => { CloseMenuWindow(); CommandExecutor.ShellOpen(homePath); };
                    return fi;
                }
                var sub = new MenuFlyoutSubItem { Text = item.Label };
                ApplyIcon(sub, item, inSubmenu);
                AddHomeFolderItems(sub.Items, homePath);
                return sub;
            }

            case ConfigItemType.Settings:
            {
                var settings = new MenuFlyoutItem { Text = string.IsNullOrWhiteSpace(item.Label) ? "WinMac Menu Settings" : item.Label };
                ApplyIcon(settings, item, inSubmenu);
                settings.Click += (_, _) =>
                {
                    CloseMenuWindow();
                    CommandExecutor.OpenSettings(_parentPid, _iniPath);
                };
                return settings;
            }

            // ── Regular item ─────────────────────────────────────────────
            default:
            {
                var fi = new MenuFlyoutItem { Text = item.Label };
                ApplyIcon(fi, item, inSubmenu);
                var captured = item;
                fi.Click += (_, _) => { CloseMenuWindow(); CommandExecutor.Execute(captured); };
                return fi;
            }
        }
    }

    private MenuFlyoutSubItem CreateTaskKillSubmenu(ConfigItem item, bool inSubmenu)
    {
        var sub = new MenuFlyoutSubItem { Text = item.Label };
        ApplyIcon(sub, item, inSubmenu);
        RenderTaskKillItems(sub.Items, GetTaskKillSnapshot());
        RefreshTaskKillSnapshotAsync(sub);

        sub.PointerEntered += (_, _) => RefreshTaskKillSnapshotAsync(sub);
        sub.GotFocus += (_, _) => RefreshTaskKillSnapshotAsync(sub);
        return sub;
    }

    private string GetTaskKillCacheKey()
        => string.Join("|",
            _config.TaskKillMax,
            _config.TaskKillIgnoreSystem,
            _config.TaskKillListWindows,
            _config.TaskKillAllDesktops,
            _config.TaskKillExcludes);

    private IReadOnlyList<ProcessEntry> GetTaskKillSnapshot()
    {
        var key = GetTaskKillCacheKey();
        lock (TaskKillCacheLock)
        {
            if (TaskKillCache != null && TaskKillCacheKey == key)
                return TaskKillCache.ToList();
        }

        return Array.Empty<ProcessEntry>();
    }

    private void RefreshTaskKillSnapshotAsync(MenuFlyoutSubItem? sub)
    {
        var key = GetTaskKillCacheKey();
        lock (TaskKillCacheLock)
        {
            if (TaskKillRefreshInProgress) return;
            if (TaskKillCache != null &&
                TaskKillCacheKey == key &&
                DateTime.UtcNow - TaskKillCacheTimeUtc < TimeSpan.FromSeconds(2))
                return;
            TaskKillRefreshInProgress = true;
        }

        var config = _config;
        _ = System.Threading.Tasks.Task.Run(() =>
            {
                try { return CommandExecutor.GetRunningProcesses(config); }
                catch { return new List<ProcessEntry>(); }
            })
            .ContinueWith(task =>
            {
                if (!DispatcherQueue.TryEnqueue(() =>
                {
                    var fresh = task.Status == System.Threading.Tasks.TaskStatus.RanToCompletion
                        ? task.Result
                        : new List<ProcessEntry>();

                    lock (TaskKillCacheLock)
                    {
                        TaskKillRefreshInProgress = false;
                        TaskKillCache = fresh;
                        TaskKillCacheKey = key;
                        TaskKillCacheTimeUtc = DateTime.UtcNow;
                    }

                    if (!_isClosing && sub != null)
                        RenderTaskKillItems(sub.Items, fresh);
                }))
                {
                    lock (TaskKillCacheLock)
                    {
                        TaskKillRefreshInProgress = false;
                    }
                }
            });
    }

    private void RenderTaskKillItems(IList<MenuFlyoutItemBase> target, IReadOnlyList<ProcessEntry> processes)
    {
        target.Clear();

        foreach (var p in processes)
        {
            var pi  = new MenuFlyoutItem { Text = $"{p.Name}  (PID {p.Pid})" };
            var pid = p.Pid;
            pi.Click += (_, _) =>
            {
                CloseMenuWindow();
                CommandExecutor.KillProcess(pid);
                RefreshTaskKillSnapshotAsync(null);
            };
            if (_config.TaskKillShowIcons)
            {
                try
                {
                    using var proc = System.Diagnostics.Process.GetProcessById(pid);
                    var exePath = proc.MainModule?.FileName;
                    if (exePath != null) pi.Icon = IconLoader.Load(exePath);
                }
                catch { }
            }
            target.Add(pi);
        }

        if (target.Count == 0)
            target.Add(new MenuFlyoutItem { Text = "(None)", IsEnabled = false });

        NormalizeFlyoutMetrics(target);
    }

    private void AddFolderEntries(IList<MenuFlyoutItemBase> target, string folderPath, int depth = 1)
    {
        var itemCount = 0;
        var maxDepth = Math.Max(1, _config.FolderSubmenuDepth);

        foreach (var entry in GetCachedFolderContents(folderPath))
        {
            var e = entry;
            if (e.IsDirectory && depth < maxDepth)
            {
                target.Add(CreateLazyFolderSubmenu(e, depth + 1));
            }
            else
            {
                var fi = new MenuFlyoutItem { Text = e.Name };
                fi.Click += (_, _) => { CloseMenuWindow(); CommandExecutor.ShellOpen(e.FullPath); };
                if (e.IsDirectory ? _config.ShowFolderIcons : _config.ShowFileIcons)
                    fi.Icon = LoadSubmenuPathIcon(e.FullPath);
                target.Add(fi);
            }

            itemCount++;
        }

        if (itemCount == 0)
            target.Add(new MenuFlyoutItem { Text = "(None)", IsEnabled = false });
    }

    private MenuFlyoutSubItem CreateLazyFolderSubmenu(FolderEntry entry, int depth)
    {
        var sub = new MenuFlyoutSubItem { Text = entry.Name };
        if (_config.ShowFolderIcons) sub.Icon = LoadSubmenuPathIcon(entry.FullPath);

        var loaded = false;
        void Populate()
        {
            if (loaded) return;
            loaded = true;
            sub.Items.Clear();
            AddFolderEntries(sub.Items, entry.FullPath, depth);
            NormalizeFlyoutMetrics(sub.Items);
        }

        if (TryGetCachedFolderContents(entry.FullPath, out var cachedEntries))
        {
            loaded = true;
            AddFolderEntriesFromList(sub.Items, cachedEntries, depth);
            NormalizeFlyoutMetrics(sub.Items);
        }
        else
        {
            sub.Items.Add(new MenuFlyoutItem { Text = "(Loading...)", IsEnabled = false });
        }

        sub.PointerEntered += (_, _) => Populate();
        sub.GotFocus += (_, _) => Populate();

        return sub;
    }

    private void AddFolderEntriesFromList(IList<MenuFlyoutItemBase> target, IEnumerable<FolderEntry> entries, int depth)
    {
        var itemCount = 0;
        var maxDepth = Math.Max(1, _config.FolderSubmenuDepth);

        foreach (var entry in entries)
        {
            if (entry.IsDirectory && depth < maxDepth)
            {
                target.Add(CreateLazyFolderSubmenu(entry, depth + 1));
            }
            else
            {
                var fi = new MenuFlyoutItem { Text = entry.Name };
                fi.Click += (_, _) => { CloseMenuWindow(); CommandExecutor.ShellOpen(entry.FullPath); };
                if (entry.IsDirectory ? _config.ShowFolderIcons : _config.ShowFileIcons)
                    fi.Icon = LoadSubmenuPathIcon(entry.FullPath);
                target.Add(fi);
            }

            itemCount++;
        }

        if (itemCount == 0)
            target.Add(new MenuFlyoutItem { Text = "(None)", IsEnabled = false });
    }

    private List<FolderEntry> GetCachedFolderContents(string folderPath)
    {
        if (TryGetCachedFolderContents(folderPath, out var cached))
            return cached;

        var entries = CommandExecutor.GetFolderContents(folderPath, _config);
        StoreFolderContents(folderPath, entries);
        return entries;
    }

    private bool TryGetCachedFolderContents(string folderPath, out List<FolderEntry> entries)
    {
        entries = new List<FolderEntry>();
        var key = CreateFolderCacheKey(folderPath);
        var stamp = GetFolderStamp(folderPath);
        if (stamp == null) return false;

        lock (FolderCacheLock)
        {
            if (!FolderCache.TryGetValue(key, out var cached) || cached.LastWriteUtc != stamp.Value)
            {
                FolderCache.Remove(key);
                return false;
            }

            cached.LastAccessUtc = DateTime.UtcNow;
            entries = cached.Items.Select(i => i with { }).ToList();
            return true;
        }
    }

    private void StoreFolderContents(string folderPath, List<FolderEntry> entries)
    {
        var stamp = GetFolderStamp(folderPath);
        if (stamp == null) return;

        lock (FolderCacheLock)
        {
            FolderCache[CreateFolderCacheKey(folderPath)] = new FolderCacheEntry(
                entries.Select(i => i with { }).ToList(),
                stamp.Value,
                DateTime.UtcNow);

            if (FolderCache.Count > FolderCacheLimit)
            {
                foreach (var key in FolderCache.OrderBy(kvp => kvp.Value.LastAccessUtc)
                                               .Take(FolderCache.Count - FolderCacheLimit)
                                               .Select(kvp => kvp.Key)
                                               .ToList())
                {
                    FolderCache.Remove(key);
                }
            }
        }
    }

    private FolderCacheKey CreateFolderCacheKey(string folderPath)
        => new(
            Path.GetFullPath(folderPath).TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar).ToUpperInvariant(),
            _config.ShowHidden,
            _config.ShowFileExtensions,
            _config.MaxItems,
            _config.SortBy,
            _config.SortDirection,
            _config.FoldersFirst);

    private static DateTime? GetFolderStamp(string folderPath)
    {
        try { return Directory.GetLastWriteTimeUtc(folderPath); }
        catch { return null; }
    }

    private sealed record FolderCacheKey(
        string Path,
        bool ShowHidden,
        bool ShowFileExtensions,
        int MaxItems,
        string SortBy,
        string SortDirection,
        bool FoldersFirst);

    private sealed record FolderCacheEntry(
        List<FolderEntry> Items,
        DateTime LastWriteUtc,
        DateTime LastAccessUtc)
    {
        public DateTime LastAccessUtc { get; set; } = LastAccessUtc;
    }

    private static string? ResolveLnkTarget(string lnkPath)
    {
        try
        {
            var shellType = Type.GetTypeFromProgID("WScript.Shell");
            if (shellType == null) return null;
            dynamic shell    = Activator.CreateInstance(shellType)!;
            dynamic shortcut = shell.CreateShortcut(lnkPath);
            string  target   = shortcut.TargetPath;
            return string.IsNullOrEmpty(target) ? null : target;
        }
        catch { return null; }
    }

    private void AddRecentItems(IList<MenuFlyoutItemBase> target)
    {
        try
        {
            var recentDir = Environment.GetFolderPath(Environment.SpecialFolder.Recent);
            var lnkFiles  = Directory.GetFiles(recentDir, "*.lnk")
                                     .OrderByDescending(File.GetLastWriteTime)
                                     .Take(_config.RecentMax)
                                     .ToList();
            var recentItems = new List<RecentMenuEntry>();

            foreach (var lnk in lnkFiles)
            {
                var targetPath = ResolveLnkTarget(lnk);
                recentItems.Add(new RecentMenuEntry(
                    BuildRecentDisplayName(lnk, targetPath),
                    targetPath ?? lnk,
                    targetPath != null && Directory.Exists(targetPath)));
            }

            if (recentItems.Count == 0)
            {
                target.Add(new MenuFlyoutItem { Text = "(None)", IsEnabled = false });
            }
            else if (_config.RecentSeparateItems)
            {
                AddRecentGroup(target, "Files", recentItems.Where(i => !i.IsFolder));

                var folders = recentItems.Where(i => i.IsFolder).ToList();
                if (folders.Count > 0)
                {
                    target.Add(new MenuFlyoutSeparator());
                    AddRecentGroup(target, "Folders", folders);
                }
            }
            else
            {
                foreach (var item in recentItems)
                    target.Add(CreateRecentMenuItem(item));
            }

            if (_config.RecentShowCleanItems)
            {
                target.Add(new MenuFlyoutSeparator());
                var clear = new MenuFlyoutItem { Text = "Clear Recent Items list" };
                clear.Click += (_, _) =>
                {
                    CloseMenuWindow();
                    foreach (var f in Directory.GetFiles(recentDir))
                        try { File.Delete(f); } catch { }
                };
                target.Add(clear);
            }
        }
        catch { }
    }

    private sealed record RecentMenuEntry(string DisplayName, string Path, bool IsFolder);

    private string BuildRecentDisplayName(string lnk, string? targetPath)
    {
        if (_config.RecentLabel == "fullpath")
            return targetPath ?? Path.GetFileNameWithoutExtension(lnk);

        var baseName = targetPath != null
            ? Path.GetFileName(targetPath)
            : Path.GetFileNameWithoutExtension(lnk);

        return _config.RecentShowExtensions
            ? baseName
            : Path.GetFileNameWithoutExtension(baseName);
    }

    private void AddRecentGroup(IList<MenuFlyoutItemBase> target, string label, IEnumerable<RecentMenuEntry> items)
    {
        var materialized = items.ToList();
        if (materialized.Count == 0) return;

        target.Add(new MenuFlyoutItem { Text = label, IsEnabled = false });
        foreach (var item in materialized)
            target.Add(CreateRecentMenuItem(item));
    }

    private MenuFlyoutItem CreateRecentMenuItem(RecentMenuEntry item)
    {
        var fi = new MenuFlyoutItem { Text = item.DisplayName };
        fi.Click += (_, _) => { CloseMenuWindow(); CommandExecutor.ShellOpen(item.Path); };
        if (_config.RecentShowIcons) fi.Icon = LoadSubmenuPathIcon(item.Path);
        return fi;
    }

    private void AddThisPCItems(IList<MenuFlyoutItemBase> target)
    {
        foreach (var drive in DriveInfo.GetDrives().Where(d => d.IsReady))
        {
            var label = string.IsNullOrEmpty(drive.VolumeLabel)
                ? drive.Name
                : $"{drive.VolumeLabel} ({drive.Name.TrimEnd('\\')})";

            var root = drive.RootDirectory.FullName;

            if (_config.ThisPCItemsAsSubmenus)
            {
                var sub = new MenuFlyoutSubItem { Text = label };
                if (_config.ThisPCShowIcons) sub.Icon = LoadSubmenuPathIcon(root);
                AddFolderEntries(sub.Items, root, depth: 1);
                target.Add(sub);
            }
            else
            {
                var fi = new MenuFlyoutItem { Text = label };
                if (_config.ThisPCShowIcons) fi.Icon = LoadSubmenuPathIcon(root);
                fi.Click += (_, _) => { CloseMenuWindow(); CommandExecutor.ShellOpen(root); };
                target.Add(fi);
            }
        }
    }

    private void AddHomeFolderItems(IList<MenuFlyoutItemBase> target, string homePath)
    {
        foreach (var entry in CommandExecutor.GetFolderContents(homePath, _config))
        {
            var e = entry;

            if (_config.HomeItemsAsSubmenus && e.IsDirectory)
            {
                var sub = new MenuFlyoutSubItem { Text = e.Name };
                if (_config.HomeShowIcons) sub.Icon = LoadSubmenuPathIcon(e.FullPath);
                AddFolderEntries(sub.Items, e.FullPath, depth: 1);
                target.Add(sub);
            }
            else
            {
                var fi = new MenuFlyoutItem { Text = e.Name };
                if (_config.HomeShowIcons) fi.Icon = LoadSubmenuPathIcon(e.FullPath);
                fi.Click += (_, _) => { CloseMenuWindow(); CommandExecutor.ShellOpen(e.FullPath); };
                target.Add(fi);
            }
        }
    }

    private IconElement? LoadSubmenuPathIcon(string path)
        => IconLoader.Load(path, _config.WinUIAlwaysShowIcons);

    /// <summary>
    /// Resolves the best icon for this item and applies it, honouring the
    /// ShowIcons (root) / ShowFolderIcons+ShowFileIcons (submenu) settings.
    /// Priority: per-item path (theme-aware) → item generic → config default.
    /// </summary>
    private void ApplyIcon(MenuFlyoutItemBase target, ConfigItem item, bool inSubmenu)
    {
        bool showIcons = inSubmenu
            ? (_config.ShowFolderIcons || _config.ShowFileIcons)
            : _config.ShowIcons;

        if (!showIcons) return;

        // Pick theme-aware path if available, fall back to generic then default.
        // The Settings command intentionally mirrors the tray icon selection.
        var isDark   = Application.Current.RequestedTheme == ApplicationTheme.Dark;
        var iconPath = item.Type == ConfigItemType.Settings ? ResolveSettingsTrayIconPath(isDark)
                     : isDark && !string.IsNullOrEmpty(item.IconPathDark)  ? item.IconPathDark
                     : !isDark && !string.IsNullOrEmpty(item.IconPathLight) ? item.IconPathLight
                     : !string.IsNullOrEmpty(item.IconPath)                 ? item.IconPath
                     : _config.DefaultIconPath;

        if (string.IsNullOrEmpty(iconPath)) return;

        var icon = IconLoader.Load(iconPath);
        if (icon == null) return;

        if (target is MenuFlyoutItem fi)        fi.Icon  = icon;
        else if (target is MenuFlyoutSubItem si) si.Icon  = icon;
    }

    private string ResolveSettingsTrayIconPath(bool isDark)
    {
        if (_config.MonochromeTrayIcon)
        {
            var configured = isDark ? _config.TrayIconPathDark : _config.TrayIconPathLight;
            if (!string.IsNullOrWhiteSpace(configured) && File.Exists(configured)) return configured;
            if (!string.IsNullOrWhiteSpace(_config.TrayIconPath) && File.Exists(_config.TrayIconPath))
                return _config.TrayIconPath;
        }

        string? nativeExe = null;
        try
        {
            if (_parentPid is > 0)
            {
                using var parent = System.Diagnostics.Process.GetProcessById(_parentPid.Value);
                nativeExe = parent.MainModule?.FileName;
            }
        }
        catch { }

        if (string.IsNullOrWhiteSpace(nativeExe))
        {
            var root = Directory.GetParent(AppContext.BaseDirectory.TrimEnd(Path.DirectorySeparatorChar))?.FullName;
            if (root != null) nativeExe = Path.Combine(root, "WinMacMenu.exe");
        }

        if (string.IsNullOrWhiteSpace(nativeExe) || !File.Exists(nativeExe)) return "";
        var resourceId = _config.MonochromeTrayIcon ? (isDark ? 103 : 102) : 105;
        return $"{nativeExe},-{resourceId}";
    }

    private List<MenuFlyoutItemBase> BuildPowerItems()
    {
        var list = new List<MenuFlyoutItemBase>();
        void Add(string label, ConfigItemType type)
        {
            var ci = new ConfigItem { Label = label, Type = type };
            var fi = new MenuFlyoutItem { Text = label };
            fi.Click += (_, _) => { CloseMenuWindow(); CommandExecutor.Execute(ci); };
            list.Add(fi);
        }
        if (_config.PowerSleep)     Add("Sleep",     ConfigItemType.PowerSleep);
        if (_config.PowerRestart)   Add("Restart",   ConfigItemType.PowerRestart);
        if (_config.PowerShutdown)  Add("Shut Down", ConfigItemType.PowerShutdown);
        if (_config.PowerHibernate) Add("Hibernate", ConfigItemType.PowerHibernate);
        if (_config.PowerLock)      Add("Lock",      ConfigItemType.PowerLock);
        if (_config.PowerLogoff)    Add("Sign Out",  ConfigItemType.PowerLogoff);
        return list;
    }

    private void ConfigureWindowChrome()
    {
        ExtendsContentIntoTitleBar = true;
        SetTitleBar(null);

        var hwnd      = WinRT.Interop.WindowNative.GetWindowHandle(this);
        var appWindow = GetAppWindowForCurrentWindow();
        appWindow.IsShownInSwitchers = false;

        var exStyle = GetWindowLong(hwnd, GWL_EXSTYLE);
        exStyle |= WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_LAYERED;
        SetWindowLong(hwnd, GWL_EXSTYLE, exStyle);
        SetLayeredWindowAttributes(hwnd, 0, 0, LWA_ALPHA);
        DisableAnchorWindowRoundedCorners(hwnd);

        var presenter = OverlappedPresenter.CreateForToolWindow();
        presenter.IsResizable    = false;
        presenter.IsMaximizable  = false;
        presenter.IsMinimizable  = false;
        presenter.SetBorderAndTitleBar(false, false);
        appWindow.SetPresenter(presenter);

        // Position the 1x1 anchor window at the cursor or configured fixed point.
        // Outside-click dismissal is handled by a low-level mouse hook while the
        // flyout is open, avoiding any visible full-screen overlay.
        var anchorPt = _config.PointerRelative
            ? GetCursorAnchor()
            : GetFixedAnchor();

        appWindow.MoveAndResize(new RectInt32(anchorPt.X, anchorPt.Y, 1, 1));
        _flyoutAnchorPoint = new Point(0, 0);
        HideAnchorWindowPixels(hwnd);

        // Bring to front so the flyout receives input.
        BringAnchorWindowToTop();
        SetForegroundWindow(hwnd);
    }

    private static void DisableAnchorWindowRoundedCorners(IntPtr hwnd)
    {
        var cornerPreference = DWMWCP_DONOTROUND;
        _ = DwmSetWindowAttribute(
            hwnd,
            DWMWA_WINDOW_CORNER_PREFERENCE,
            ref cornerPreference,
            Marshal.SizeOf<int>());
    }

    private static void HideAnchorWindowPixels(IntPtr hwnd)
    {
        var emptyRegion = CreateRectRgn(0, 0, 0, 0);
        if (emptyRegion == IntPtr.Zero) return;

        if (SetWindowRgn(hwnd, emptyRegion, true) == 0)
            DeleteObject(emptyRegion);
    }

    private void BringAnchorWindowToTop()
    {
        var hwnd = WinRT.Interop.WindowNative.GetWindowHandle(this);
        SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }

    [DllImport("user32.dll")] private static extern uint GetDpiForWindow(IntPtr hWnd);

    private Point PhysicalToDips(int physX, int physY)
    {
        var hwnd = WinRT.Interop.WindowNative.GetWindowHandle(this);
        var dpi = (int)GetDpiForWindow(hwnd);
        if (dpi <= 0) dpi = 96;
        return new Point(physX * 96.0 / dpi, physY * 96.0 / dpi);
    }

    // Converts physical screen pixels → logical pixels WinUI3 uses for window placement.
    // At 100% scaling these are identical; at 150% a physical 1500px becomes 1000 logical px.
    private PointInt32 ToLogical(int physX, int physY)
    {
        var hwnd = WinRT.Interop.WindowNative.GetWindowHandle(this);
        var dpi  = (int)GetDpiForWindow(hwnd);
        if (dpi <= 0) dpi = 96;
        return new PointInt32(physX * 96 / dpi, physY * 96 / dpi);
    }

    // Anchor = cursor position (pointer-relative mode)
    private PointInt32 GetCursorAnchor()
    {
        GetCursorPos(out var pt);
        if (!_config.IgnoreHOffsetWhenRelative) pt.x += _config.HOffset;
        if (!_config.IgnoreVOffsetWhenRelative) pt.y += _config.VOffset;
        // Snap to nearest integer logical pixel to avoid sub-pixel layout jitter
        return ToLogical(pt.x, pt.y);
    }

    // Anchor = fixed point derived from Horizontal/Vertical alignment + offsets
    private PointInt32 GetFixedAnchor()
    {
        // Use the display that contains the primary taskbar
        var display = DisplayArea.Primary;
        var work    = display.WorkArea;

        // Horizontal: left/center/right edge of work area + HOffset
        int x = _config.Horizontal.ToLowerInvariant() switch
        {
            "right"  => work.X + work.Width  + _config.HOffset,
            "center" => work.X + work.Width  / 2 + _config.HOffset,
            _        => work.X + _config.HOffset,           // "left" (default)
        };

        // Vertical: top/center/bottom edge of work area + VOffset
        int y = _config.Vertical.ToLowerInvariant() switch
        {
            "bottom" => work.Y + work.Height + _config.VOffset,
            "center" => work.Y + work.Height / 2 + _config.VOffset,
            _        => work.Y + _config.VOffset,           // "top" (default)
        };

        // Clamp to work area so the anchor is always on-screen
        x = Math.Clamp(x, work.X, work.X + work.Width  - 1);
        y = Math.Clamp(y, work.Y, work.Y + work.Height - 1);

        return new PointInt32(x, y);
    }

    private AppWindow GetAppWindowForCurrentWindow()
    {
        var hwnd  = WinRT.Interop.WindowNative.GetWindowHandle(this);
        var wndId = Win32Interop.GetWindowIdFromWindow(hwnd);
        return AppWindow.GetFromWindowId(wndId);
    }
}
