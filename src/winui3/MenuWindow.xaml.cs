using System.Runtime.InteropServices;
using Microsoft.UI;
using Microsoft.UI.Windowing;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Controls.Primitives;
using Microsoft.UI.Xaml.Input;
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
    private const uint SWP_SHOWWINDOW   = 0x0040;
    private const int DWMWA_WINDOW_CORNER_PREFERENCE = 33;
    private const int DWMWCP_DONOTROUND = 1;
    private const int SW_HIDE = 0;

    [DllImport("user32.dll")] private static extern int  GetWindowLong(IntPtr hWnd, int nIndex);
    [DllImport("user32.dll")] private static extern int  SetWindowLong(IntPtr hWnd, int nIndex, int dwNewLong);
    [DllImport("user32.dll")] private static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);
    [DllImport("user32.dll")] private static extern bool SetWindowPos(IntPtr hWnd, IntPtr hWndInsertAfter, int x, int y, int cx, int cy, uint flags);
    [DllImport("user32.dll")] private static extern bool SetLayeredWindowAttributes(IntPtr hwnd, uint crKey, byte bAlpha, uint dwFlags);
    [DllImport("user32.dll")] private static extern int SetWindowRgn(IntPtr hWnd, IntPtr hRgn, bool bRedraw);
    [DllImport("dwmapi.dll")] private static extern int DwmSetWindowAttribute(IntPtr hwnd, int dwAttribute, ref int pvAttribute, int cbAttribute);
    [DllImport("gdi32.dll")] private static extern IntPtr CreateRectRgn(int x1, int y1, int x2, int y2);
    [DllImport("gdi32.dll")] private static extern bool DeleteObject(IntPtr hObject);
    [DllImport("user32.dll", EntryPoint = "SetWindowsHookExW")] private static extern IntPtr SetMouseHookEx(int idHook, MouseHookProc lpfn, IntPtr hMod, uint dwThreadId);
    [DllImport("user32.dll", EntryPoint = "SetWindowsHookExW")] private static extern IntPtr SetKeyboardHookEx(int idHook, KeyboardHookProc lpfn, IntPtr hMod, uint dwThreadId);
    [DllImport("user32.dll")] private static extern bool UnhookWindowsHookEx(IntPtr hhk);
    [DllImport("user32.dll")] private static extern IntPtr CallNextHookEx(IntPtr hhk, int nCode, IntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll")] private static extern short GetAsyncKeyState(int vKey);

    [StructLayout(LayoutKind.Sequential)]
    private struct POINT { public int x; public int y; }
    [StructLayout(LayoutKind.Sequential)]
    private struct MSLLHOOKSTRUCT { public POINT pt; public uint mouseData, flags, time; public UIntPtr extraInfo; }
    [StructLayout(LayoutKind.Sequential)]
    private struct KBDLLHOOKSTRUCT { public uint vkCode, scanCode, flags, time; public UIntPtr dwExtraInfo; }

    [DllImport("user32.dll")] private static extern bool GetCursorPos(out POINT lpPoint);
    [DllImport("user32.dll")] private static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] private static extern IntPtr WindowFromPoint(POINT point);
    [DllImport("user32.dll")] private static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint processId);
    [DllImport("user32.dll")] private static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);
    [DllImport("shell32.dll", CharSet = CharSet.Unicode)]
    private static extern int SHGetKnownFolderPath(ref Guid rfid, uint dwFlags, IntPtr hToken, out IntPtr ppszPath);
    [DllImport("ole32.dll")] private static extern void CoTaskMemFree(IntPtr pv);

    private delegate IntPtr MouseHookProc(int nCode, IntPtr wParam, IntPtr lParam);
    private delegate IntPtr KeyboardHookProc(int nCode, IntPtr wParam, IntPtr lParam);
    private const int WH_MOUSE_LL = 14;
    private const int WH_KEYBOARD_LL = 13;
    private const int WM_LBUTTONDOWN = 0x0201;
    private const int WM_RBUTTONDOWN = 0x0204;
    private const int WM_MBUTTONDOWN = 0x0207;
    private const int WM_XBUTTONDOWN = 0x020B;
    private const int WM_KEYDOWN = 0x0100;
    private const int WM_SYSKEYDOWN = 0x0104;
    private const int VK_ESCAPE = 0x1B;
    private const int VK_D = 0x44;
    private const int VK_LWIN = 0x5B;
    private const int VK_RWIN = 0x5C;

    private AppConfig _config;
    private readonly string _iniPath;
    private MenuTriggerType _trigger;
    private readonly int? _parentPid;
    private readonly MouseHookProc _outsideClickProc;
    private readonly KeyboardHookProc _keyboardProc;
    private IntPtr _outsideClickHook;
    private IntPtr _keyboardHook;
    private MenuFlyout? _flyout;
    private Point _flyoutAnchorPoint;
    private bool _isClosing;
    private bool _isOpen;
    private bool _isLoaded;
    private bool _showOnLoad;
    private bool _shellContextMenuOpen;
    private int _ignoreOutsideClicksUntil;
    private CancellationTokenSource? _iconLoadCancel;

    public event Action? Dismissed;

    private const double DefaultMenuItemHeight = 40;
    private const double DefaultMenuSeparatorHeight = 9;
    private const double CompactMenuItemMinHeight = 32;
    private const double CompactMenuSeparatorHeight = 8;
    private const double RootMenuMaxWidthScreenFraction = 0.50;
    private const double SubmenuMaxWidthScreenFraction = 0.33;
    private const double SubmenuMinMaxWidth = 280;
    private const double SubmenuTextReservedWidth = 104;
    private const int FolderCacheLimit = 256;
    private const int FixedLeftEdgeFlyoutGap = 4;
    private const int FixedTopEdgeFlyoutGap = 5;
    private const int DeferredIconLoadTimeoutMs = 1200;
    private static readonly TimeSpan RecentCacheTtl = TimeSpan.FromSeconds(10);
    private static readonly TimeSpan TaskKillCacheTtl = TimeSpan.FromSeconds(2);
    private static readonly TimeSpan ThisPCCacheTtl = TimeSpan.FromSeconds(10);

    private static readonly object FolderCacheLock = new();
    private static readonly Dictionary<FolderCacheKey, FolderCacheEntry> FolderCache = new();
    private static readonly object SpecialFolderCacheLock = new();
    private static readonly Dictionary<SpecialFolderRoot, SpecialFolderCacheState> SpecialFolderCaches = new();
    private static readonly object TaskKillCacheLock = new();
    private static List<ProcessEntry>? TaskKillCache;
    private static string TaskKillCacheKey = "";
    private static DateTime TaskKillCacheTimeUtc = DateTime.MinValue;
    private static bool TaskKillRefreshInProgress;
    private static long TaskKillCacheGeneration;
    private static long TaskKillNextRequest;
    private static long TaskKillCommittedRequest;
    private static readonly object RecentCacheLock = new();
    private static List<RecentMenuEntry>? RecentCache;
    private static string RecentCacheKey = "";
    private static DateTime RecentCacheTimeUtc = DateTime.MinValue;
    private static long RecentCacheGeneration;
    private static bool RecentRefreshInProgress;
    private static readonly object ThisPCCacheLock = new();
    private static List<FolderEntry>? ThisPCCache;
    private static string ThisPCCacheKey = "";
    private static DateTime ThisPCCacheTimeUtc = DateTime.MinValue;
    private static readonly SemaphoreSlim DeferredIconLoadGate = new(4, 4);

    public MenuWindow(AppConfig config, string iniPath, MenuTriggerType trigger, int? parentPid, bool showOnLoad = true)
    {
        _config = config;
        _iniPath = iniPath;
        _trigger = trigger;
        _parentPid = parentPid;
        _showOnLoad = showOnLoad;
        _outsideClickProc = OutsideClickProc;
        _keyboardProc = KeyboardProc;

        InitializeComponent();

        ConfigureWindowChrome();

        RootGrid.Loaded += (_, _) =>
        {
            _isLoaded = true;
            if (_showOnLoad)
                ShowFlyout();
        };
        Closed += (_, _) => RemoveHooksAndShellContextMenu();
    }

    public void Open(AppConfig config, MenuTriggerType trigger)
    {
        _config = config;
        _trigger = trigger;
        _isClosing = false;
        _showOnLoad = true;

        if (_isLoaded)
            ShowFlyout();
    }

    public static void WarmDynamicCaches(AppConfig config)
    {
        if (config.Items.Any(i => i.Type is ConfigItemType.Recent or ConfigItemType.RecentSubmenu))
        {
            try { RefreshRecentCache(config); } catch { }
        }
        if (config.Items.Any(i => i.Type == ConfigItemType.ThisPC && !config.ThisPCAsSubmenu))
        {
            try { StoreThisPCCache(config, BuildThisPCEntries(config)); } catch { }
        }
        if (config.Items.Any(i => i.Type == ConfigItemType.TaskKill))
        {
            try { WarmTaskKillCache(config); } catch { }
        }
        try { WarmConfiguredFolderCaches(config); } catch { }
    }

    public static void InvalidateCachedData()
    {
        lock (FolderCacheLock)
            FolderCache.Clear();
        lock (SpecialFolderCacheLock)
            SpecialFolderCaches.Clear();
        lock (TaskKillCacheLock)
        {
            ++TaskKillCacheGeneration;
            TaskKillCache = null;
            TaskKillCacheKey = "";
            TaskKillCacheTimeUtc = DateTime.MinValue;
            TaskKillRefreshInProgress = false;
        }
        lock (RecentCacheLock)
        {
            ++RecentCacheGeneration;
            RecentCache = null;
            RecentCacheKey = "";
            RecentCacheTimeUtc = DateTime.MinValue;
            RecentRefreshInProgress = false;
        }
        lock (ThisPCCacheLock)
        {
            ThisPCCache = null;
            ThisPCCacheKey = "";
            ThisPCCacheTimeUtc = DateTime.MinValue;
        }
    }

    public static void RefreshOpenedSpecialFolderCaches(AppConfig config)
    {
        foreach (var root in Enum.GetValues<SpecialFolderRoot>())
        {
            if (!HasConfiguredSpecialItem(config, root) || !WasSpecialRootOpened(root))
                continue;
            _ = WarmSpecialFolderRootAsync(config, root, openedByUser: false, force: true, warmTreeInline: false);
        }
    }

    private static void WarmTaskKillCache(AppConfig config)
    {
        var key = GetTaskKillCacheKey(config);
        long generation, request;
        lock (TaskKillCacheLock)
        {
            generation = TaskKillCacheGeneration;
            request = ++TaskKillNextRequest;
        }
        var processes = CommandExecutor.GetRunningProcesses(config);
        lock (TaskKillCacheLock)
        {
            if (generation != TaskKillCacheGeneration || request < TaskKillCommittedRequest) return;
            TaskKillCommittedRequest = request;
            TaskKillCache = processes;
            TaskKillCacheKey = key;
            TaskKillCacheTimeUtc = DateTime.UtcNow;
            if (request == TaskKillNextRequest) TaskKillRefreshInProgress = false;
        }
    }

    private static void WarmConfiguredFolderCaches(AppConfig config)
    {
        foreach (var item in config.Items)
            WarmConfigItemCaches(config, item);
    }

    private static void WarmConfigItemCaches(AppConfig config, ConfigItem item)
    {
        try
        {
            if ((item.Type == ConfigItemType.FolderSubmenu ||
                 (item.Type == ConfigItemType.Folder && (item.Submenu || item.InlineExpand))) &&
                !string.IsNullOrWhiteSpace(item.Path))
            {
                WarmFolderCache(config, item.Path, depth: 1);
            }

        }
        catch { }
    }

    private static void WarmFolderCache(AppConfig config, string folderPath, int depth)
    {
        if (!CanReuseFolderMetadataAtDepth(config, depth))
            return;

        var entries = CommandExecutor.GetFolderContents(folderPath, config);
        StoreFolderContents(config, folderPath, entries, depth);
    }

    private enum SpecialFolderRoot
    {
        Home,
        ThisPC
    }

    private sealed class SpecialFolderCacheState
    {
        public string ConfigKey { get; set; } = "";
        public bool WasOpened { get; set; }
        public bool IsReady { get; set; }
        public bool IsTreeReady { get; set; }
        public DateTime LastSuccessfulWarmUtc { get; set; } = DateTime.MinValue;
        public Task<bool>? WarmTask { get; set; }
    }

    public static async Task WarmSpecialFoldersForMenuOpenAsync(AppConfig config)
    {
        var tasks = new List<Task<bool>>();
        if (ShouldPreloadSpecialFolderOnMenuOpen(config, SpecialFolderRoot.Home))
            tasks.Add(WarmSpecialFolderRootAsync(config, SpecialFolderRoot.Home, openedByUser: true, warmTreeInline: false));
        if (ShouldPreloadSpecialFolderOnMenuOpen(config, SpecialFolderRoot.ThisPC))
            tasks.Add(WarmSpecialFolderRootAsync(config, SpecialFolderRoot.ThisPC, openedByUser: true, warmTreeInline: false));

        if (tasks.Count == 0)
            return;

        try { await Task.WhenAll(tasks).ConfigureAwait(false); }
        catch { }
    }

    private static bool HasConfiguredSpecialItem(AppConfig config, SpecialFolderRoot root)
        => root switch
        {
            SpecialFolderRoot.Home => config.Items.Any(i => i.Type == ConfigItemType.Home),
            SpecialFolderRoot.ThisPC => config.Items.Any(i => i.Type == ConfigItemType.ThisPC),
            _ => false
        };

    private static bool ShouldPreloadSpecialFolderOnMenuOpen(AppConfig config, SpecialFolderRoot root)
        => root switch
        {
            SpecialFolderRoot.Home => config.HomeItemsAsSubmenus &&
                                      HasConfiguredSpecialItem(config, root),
            SpecialFolderRoot.ThisPC => config.ThisPCItemsAsSubmenus &&
                                        HasConfiguredSpecialItem(config, root),
            _ => false
        };

    private static string GetSpecialFolderCacheKey(AppConfig config, SpecialFolderRoot root)
        => string.Join("|",
            root,
            config.FolderSubmenuDepth,
            config.ShowHidden,
            config.ShowDotfiles,
            config.ShowFileExtensions,
            config.MaxItems,
            config.SortBy,
            config.SortDirection,
            config.FoldersFirst,
            config.HomeItemsAsSubmenus,
            config.ThisPCItemsAsSubmenus);

    private static bool WasSpecialRootOpened(SpecialFolderRoot root)
    {
        lock (SpecialFolderCacheLock)
            return SpecialFolderCaches.TryGetValue(root, out var state) && state.WasOpened;
    }

    private static bool IsSpecialFolderRootReady(AppConfig config, SpecialFolderRoot root)
    {
        var key = GetSpecialFolderCacheKey(config, root);
        lock (SpecialFolderCacheLock)
        {
            return SpecialFolderCaches.TryGetValue(root, out var state) &&
                   state.IsReady && state.ConfigKey == key &&
                   (!RequiresSpecialFolderTree(config, root) || state.IsTreeReady);
        }
    }

    private static bool RequiresSpecialFolderTree(AppConfig config, SpecialFolderRoot root)
        => root switch
        {
            // Descendants are loaded on demand, never required to show a root.
            SpecialFolderRoot.Home => false,
            SpecialFolderRoot.ThisPC => false,
            _ => false
        };

    private static Task<bool> WarmSpecialFolderRootAsync(
        AppConfig config,
        SpecialFolderRoot root,
        bool openedByUser,
        bool force = false,
        bool warmTreeInline = false)
    {
        var key = GetSpecialFolderCacheKey(config, root);
        lock (SpecialFolderCacheLock)
        {
            if (!SpecialFolderCaches.TryGetValue(root, out var state))
            {
                state = new SpecialFolderCacheState();
                SpecialFolderCaches[root] = state;
            }

            if (openedByUser)
                state.WasOpened = true;

            if (!force && state.IsReady && state.ConfigKey == key &&
                (!RequiresSpecialFolderTree(config, root) || state.IsTreeReady))
                return Task.FromResult(true);

            if (state.ConfigKey == key && state.WarmTask is { IsCompleted: false })
                return state.WarmTask;

            var hadReadySnapshot = force && state.IsReady && state.ConfigKey == key;
            var hadTreeSnapshot = hadReadySnapshot && state.IsTreeReady;
            state.ConfigKey = key;
            state.IsReady = hadReadySnapshot;
            state.IsTreeReady = hadTreeSnapshot;
            var task = Task.Run(() => WarmSpecialFolderRoot(config, root, warmTreeInline));
            state.WarmTask = task;
            _ = task.ContinueWith(completed =>
            {
                lock (SpecialFolderCacheLock)
                {
                    if (!ReferenceEquals(state.WarmTask, completed)) return;
                    state.WarmTask = null;
                    var succeeded = completed.Status == TaskStatus.RanToCompletion && completed.Result;
                    state.IsReady = hadReadySnapshot || succeeded;
                    state.IsTreeReady = hadTreeSnapshot || (succeeded && warmTreeInline);
                    if (succeeded)
                        state.LastSuccessfulWarmUtc = DateTime.UtcNow;
                }
            }, TaskScheduler.Default);
            return task;
        }
    }

    private static bool WarmSpecialFolderRoot(AppConfig config, SpecialFolderRoot root, bool warmTreeInline)
    {
        try
        {
            if (root == SpecialFolderRoot.Home)
            {
                var homePath = Environment.GetFolderPath(Environment.SpecialFolder.UserProfile);
                if (string.IsNullOrWhiteSpace(homePath)) return false;

                var entries = GetHomeFolderContents(config, homePath);
                if (!StoreSpecialFolderContents(config, homePath, entries, depth: 1))
                    return false;
                if (warmTreeInline && config.HomeItemsAsSubmenus && config.FolderSubmenuDepth > 1)
                {
                    foreach (var entry in entries.Where(entry => entry.IsDirectory))
                        WarmSpecialFolderTree(config, entry.FullPath, depth: 2);
                }
                return true;
            }

            var drives = BuildThisPCEntries(config);
            StoreThisPCCache(config, drives);
            if (warmTreeInline && config.ThisPCItemsAsSubmenus && config.FolderSubmenuDepth > 1)
            {
                foreach (var drive in drives)
                    WarmSpecialFolderTree(config, drive.FullPath, depth: 1);
            }
            return true;
        }
        catch
        {
            return false;
        }
    }

    private static void WarmSpecialFolderTree(AppConfig config, string folderPath, int depth)
    {
        var entries = CommandExecutor.GetFolderContents(folderPath, config);
        StoreSpecialFolderContents(config, folderPath, entries, depth);

        if (depth >= Math.Max(1, config.FolderSubmenuDepth))
            return;

        foreach (var entry in entries.Where(entry => entry.IsDirectory))
            WarmSpecialFolderTree(config, entry.FullPath, depth + 1);
    }

    private void ShowFlyout()
    {
        if (_isOpen) return;

        try
        {
            _isOpen = true;
            _iconLoadCancel = new CancellationTokenSource();
            PositionAnchorWindow();
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
                    Label = "Menu Settings",
                    Type = ConfigItemType.Settings
                });
            }
            if (_flyout.Items.Count == 0)
                _flyout.Items.Add(new MenuFlyoutItem { Text = "Empty...", IsEnabled = false });
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


    private int SubmenuMetadataThreshold
        => ParseSubmenuMetadataThreshold(_config);

    private static int ParseSubmenuMetadataThreshold(AppConfig config)
    {
        var raw = config.SubmenuMetadataThreshold?.Trim();
        if (string.Equals(raw, "dynamic", StringComparison.OrdinalIgnoreCase) ||
            string.Equals(raw, "d", StringComparison.OrdinalIgnoreCase))
            return -1;
        return int.TryParse(raw, out var value)
            ? Math.Clamp(value, 0, 64)
            : 4;
    }

    private bool CanReuseFolderMetadataAtDepth(int depth)
        => CanReuseFolderMetadataAtDepth(_config, depth);

    private static bool CanReuseFolderMetadataAtDepth(AppConfig config, int depth)
    {
        var threshold = ParseSubmenuMetadataThreshold(config);
        return threshold < 0 || (threshold > 0 && depth <= threshold);
    }

    private void NormalizeFlyoutMetrics(IList<MenuFlyoutItemBase> items, int depth = 0)
    {
        var compact = !string.Equals(_config.WinUI3Size, "Large", StringComparison.OrdinalIgnoreCase);
        var separatorHeight = compact ? CompactMenuSeparatorHeight : DefaultMenuSeparatorHeight;
        var maxWidth = depth > 0 ? GetSubmenuMaxWidth() : GetRootMenuMaxWidth();

        foreach (var item in items)
        {
            // MenuFlyout focuses its first item even for mouse invocations.
            // Keep focus/keyboard navigation, but never draw the system outline.
            item.UseSystemFocusVisuals = false;

            if (item is FrameworkElement element)
            {
                element.MaxWidth = maxWidth;

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
                TrimSubmenuText(textItem, maxWidth);

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
        => GetMenuMaxWidth(SubmenuMaxWidthScreenFraction, SubmenuMinMaxWidth);

    private double GetRootMenuMaxWidth()
        => GetMenuMaxWidth(RootMenuMaxWidthScreenFraction, 0);

    private double GetMenuMaxWidth(double screenFraction, double minimumWidth)
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

            var maxWidth = display.WorkArea.Width * screenFraction * 96.0 / dpi;
            return Math.Max(minimumWidth, maxWidth);
        }
        catch
        {
            return minimumWidth;
        }
    }

    private void CloseMenuWindow()
    {
        if (_isClosing || !_isOpen) return;
        _isClosing = true;
        _isOpen = false;
        _showOnLoad = false;
        _iconLoadCancel?.Cancel();
        _iconLoadCancel?.Dispose();
        _iconLoadCancel = null;
        CancelShellContextMenu();
        RemoveHooks();
        var flyout = _flyout;
        _flyout = null;
        try { flyout?.Hide(); } catch { }
        if (flyout != null)
            ClearFlyoutItems(flyout.Items);

        var hwnd = WinRT.Interop.WindowNative.GetWindowHandle(this);
        ShowWindow(hwnd, SW_HIDE);
        _isClosing = false;
        Dismissed?.Invoke();
    }

    public void Dismiss() => CloseMenuWindow();

    public void KeepHidden()
    {
        var hwnd = WinRT.Interop.WindowNative.GetWindowHandle(this);
        ShowWindow(hwnd, SW_HIDE);
    }

    private void CleanupFlyoutUi()
    {
        try
        {
            if (_flyout == null) return;
            ClearFlyoutItems(_flyout.Items);
            _flyout = null;
        }
        catch { }
    }

    private static void ClearFlyoutItems(IList<MenuFlyoutItemBase> items)
    {
        foreach (var item in items.OfType<MenuFlyoutSubItem>())
            ClearFlyoutItems(item.Items);
        items.Clear();
    }

    private void InstallOutsideClickHook()
    {
        if (_outsideClickHook == IntPtr.Zero)
            _outsideClickHook = SetMouseHookEx(WH_MOUSE_LL, _outsideClickProc, IntPtr.Zero, 0);
        if (_keyboardHook == IntPtr.Zero)
            _keyboardHook = SetKeyboardHookEx(WH_KEYBOARD_LL, _keyboardProc, IntPtr.Zero, 0);
    }

    private void RemoveHooksAndShellContextMenu()
    {
        _iconLoadCancel?.Cancel();
        CancelShellContextMenu();
        RemoveHooks();
    }

    private void RemoveHooks()
    {
        if (_outsideClickHook != IntPtr.Zero)
        {
            UnhookWindowsHookEx(_outsideClickHook);
            _outsideClickHook = IntPtr.Zero;
        }

        if (_keyboardHook != IntPtr.Zero)
        {
            UnhookWindowsHookEx(_keyboardHook);
            _keyboardHook = IntPtr.Zero;
        }
    }

    private void CancelShellContextMenu()
    {
        if (!_shellContextMenuOpen) return;
        var hwnd = WinRT.Interop.WindowNative.GetWindowHandle(this);
        CommandExecutor.CancelShellContextMenus(hwnd);
    }


    private IntPtr KeyboardProc(int nCode, IntPtr wParam, IntPtr lParam)
    {
        if (nCode >= 0 && ((int)wParam == WM_KEYDOWN || (int)wParam == WM_SYSKEYDOWN))
        {
            var key = Marshal.PtrToStructure<KBDLLHOOKSTRUCT>(lParam);
            if (_shellContextMenuOpen && ShouldCancelShellContextMenuForKey((int)key.vkCode))
            {
                CancelShellContextMenu();
                return new IntPtr(1);
            }
        }

        return CallNextHookEx(_keyboardHook, nCode, wParam, lParam);
    }

    private static bool ShouldCancelShellContextMenuForKey(int vkCode)
    {
        if (vkCode == VK_ESCAPE)
            return true;

        if (vkCode == VK_D)
        {
            var winHeld = (GetAsyncKeyState(VK_LWIN) & 0x8000) != 0 ||
                          (GetAsyncKeyState(VK_RWIN) & 0x8000) != 0;
            return winHeld;
        }

        return false;
    }
    private IntPtr OutsideClickProc(int nCode, IntPtr wParam, IntPtr lParam)
    {
        // Let the input hook consume configured Start clicks before considering dismissal.
        // Its actual hit test includes the hidden INI rectangle; do not guess a second zone.
        var nextResult = CallNextHookEx(_outsideClickHook, nCode, wParam, lParam);
        if (nextResult != IntPtr.Zero) return nextResult;
        if (nCode >= 0 && (int)wParam is WM_LBUTTONDOWN or WM_RBUTTONDOWN or WM_MBUTTONDOWN or WM_XBUTTONDOWN)
        {
            if (_shellContextMenuOpen)
                return nextResult;

            if (unchecked(Environment.TickCount - _ignoreOutsideClicksUntil) < 0)
                return nextResult;

            var mouse = Marshal.PtrToStructure<MSLLHOOKSTRUCT>(lParam);

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

        return nextResult;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct RECT { public int left, top, right, bottom; }

    // Root and submenu items use the same icon policy; only the root-hide flag differs.
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

        if (item.Type == ConfigItemType.ThisPC && !_config.ThisPCAsSubmenu)
        {
            AddInlineSpecialFolder(target, item, inSubmenu, "shell:MyComputerFolder",
                items => AddThisPCItems(items, itemsInRootMenu: !inSubmenu, useSpecialFolderCache: true));
            return;
        }

        if (item.Type == ConfigItemType.Home && !_config.HomeAsSubmenu)
        {
            var homePath = Environment.GetFolderPath(Environment.SpecialFolder.UserProfile);
            AddInlineSpecialFolder(target, item, inSubmenu, homePath,
                items => AddHomeFolderItems(items, homePath, itemsInRootMenu: !inSubmenu, useSpecialFolderCache: true));
            return;
        }

        target.Add(CreateFlyoutItem(item, inSubmenu));
    }

    private void AddInlineSpecialFolder(
        IList<MenuFlyoutItemBase> target,
        ConfigItem item,
        bool inSubmenu,
        string openPath,
        Action<IList<MenuFlyoutItemBase>> populateItems)
    {
        if (!item.InlineNoHeader && !string.IsNullOrWhiteSpace(item.Label))
        {
            if (item.InlineOpen)
            {
                var header = new MenuFlyoutItem { Text = item.Label };
                header.Click += (_, _) => { CloseMenuWindow(); CommandExecutor.ShellOpen(openPath); };
                target.Add(header);
            }
            else
            {
                target.Add(new MenuFlyoutItem { Text = item.Label, IsEnabled = false });
            }
        }

        PopulateInlineSpecialFolderAsync(target, item.Type == ConfigItemType.Home
            ? SpecialFolderRoot.Home : SpecialFolderRoot.ThisPC, populateItems);
    }

    private async void PopulateInlineSpecialFolderAsync(
        IList<MenuFlyoutItemBase> target, SpecialFolderRoot root,
        Action<IList<MenuFlyoutItemBase>> populate)
    {
        if (IsSpecialFolderRootReady(_config, root)) { populate(target); return; }
        var marker = new MenuFlyoutItem { Text = "(Loading...)", IsEnabled = false };
        target.Add(marker);
        var token = _iconLoadCancel?.Token ?? CancellationToken.None;
        try
        {
            var succeeded = await WarmSpecialFolderRootAsync(_config, root, openedByUser: true);
            if (token.IsCancellationRequested || !_isOpen) return;
            var index = target.IndexOf(marker);
            if (index < 0) return;
            if (!succeeded) { marker.Text = "(Unavailable)"; return; }
            var loaded = new List<MenuFlyoutItemBase>();
            populate(loaded);
            target.RemoveAt(index);
            foreach (var entry in loaded) target.Insert(index++, entry);
            NormalizeFlyoutMetrics(target);
        }
        catch (Exception ex) { App.LogException("Populate special folder", ex); }
    }

    private void AddInlineFolder(IList<MenuFlyoutItemBase> target, ConfigItem item, bool inSubmenu)
    {
        if (!item.InlineNoHeader)
        {
            if (item.InlineOpen)
            {
                var header = new MenuFlyoutItem { Text = item.Label };
                UnderlineMenuItemText(header);
                var path = item.Path;
                header.Click += (_, _) => { CloseMenuWindow(); CommandExecutor.ShellOpen(path); };
                target.Add(header);
            }
            else
            {
                var header = new MenuFlyoutItem { Text = item.Label, IsEnabled = false };
                target.Add(header);
            }
        }

        AddFolderEntries(target, item.Path, depth: 1, itemsInRootMenu: !inSubmenu);
    }

    private static void UnderlineMenuItemText(MenuFlyoutItem item)
    {
        item.Loaded += (_, _) => item.DispatcherQueue.TryEnqueue(() =>
        {
            UnderlineTextDescendant(item, item.Text);
        });
    }

    private static bool UnderlineTextDescendant(DependencyObject parent, string text)
    {
        var childCount = Microsoft.UI.Xaml.Media.VisualTreeHelper.GetChildrenCount(parent);
        for (var i = 0; i < childCount; i++)
        {
            var child = Microsoft.UI.Xaml.Media.VisualTreeHelper.GetChild(parent, i);
            if (child is TextBlock textBlock && textBlock.Text == text)
            {
                textBlock.TextDecorations = Windows.UI.Text.TextDecorations.Underline;
                return true;
            }
            if (UnderlineTextDescendant(child, text)) return true;
        }
        return false;
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
                var sub = CreateConfiguredSubmenu(item, inSubmenu,
                    items => AddCachedFolderEntriesOrPlaceholder(items, item.Path, depth: 1));
                AttachFolderSubmenuInteractions(sub, item.Path);
                return sub;
            }

            // A plain FOLDER opens its target path. Unlike generic items it
            // falls back to the shell's folder icon when no custom icon exists.
            case ConfigItemType.Folder:
            {
                var fi = new MenuFlyoutItem { Text = item.Label };
                ApplyFolderItemIcon(fi, item, inSubmenu);
                var path = item.Path;
                fi.Click += (_, _) => { CloseMenuWindow(); CommandExecutor.ShellOpen(path); };
                AttachPathContextMenu(fi, path);
                return fi;
            }

            // ── Recent items ─────────────────────────────────────────────
            case ConfigItemType.Recent:
            case ConfigItemType.RecentSubmenu:
            {
                return CreateConfiguredSubmenu(item, inSubmenu, AddCachedRecentItemsOrPlaceholder);
            }

            // ── This PC (drives) ─────────────────────────────────────────
            case ConfigItemType.ThisPC:
            {
                return CreateSpecialFolderSubmenu(item, inSubmenu, SpecialFolderRoot.ThisPC);
            }

            // ── Home folder ──────────────────────────────────────────────
            case ConfigItemType.Home:
            {
                return CreateSpecialFolderSubmenu(item, inSubmenu, SpecialFolderRoot.Home);
            }

            case ConfigItemType.Settings:
            {
                var settings = new MenuFlyoutItem { Text = string.IsNullOrWhiteSpace(item.Label) ? "Menu Settings" : item.Label };
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
                fi.Click += (_, _) => { CloseMenuWindow(); CommandExecutor.Execute(captured, _config); };
                return fi;
            }
        }
    }

    private MenuFlyoutSubItem CreateConfiguredSubmenu(
        ConfigItem item,
        bool inSubmenu,
        Action<IList<MenuFlyoutItemBase>> populateItems)
    {
        var sub = new MenuFlyoutSubItem { Text = item.Label };
        if (item.Type is ConfigItemType.Folder or ConfigItemType.FolderSubmenu)
            ApplyFolderItemIcon(sub, item, inSubmenu);
        else
            ApplyIcon(sub, item, inSubmenu);
        populateItems(sub.Items);
        return sub;
    }

    private MenuFlyoutSubItem CreateSpecialFolderSubmenu(
        ConfigItem item,
        bool inSubmenu,
        SpecialFolderRoot root)
    {
        var sub = new MenuFlyoutSubItem { Text = item.Label };
        ApplyIcon(sub, item, inSubmenu);

        void RenderCachedContents()
        {
            sub.Items.Clear();
            if (root == SpecialFolderRoot.Home)
            {
                var homePath = Environment.GetFolderPath(Environment.SpecialFolder.UserProfile);
                AddCachedHomeFolderItemsOrPlaceholder(sub.Items, homePath);
            }
            else
            {
                AddThisPCItems(sub.Items, useSpecialFolderCache: true);
            }
            NormalizeFlyoutMetrics(sub.Items);
        }

        void ShowState(string text)
        {
            sub.Items.Clear();
            sub.Items.Add(new MenuFlyoutItem { Text = text, IsEnabled = false });
            NormalizeFlyoutMetrics(sub.Items);
        }

        async void WarmAndRender()
        {
            if (IsSpecialFolderRootReady(_config, root))
            {
                RenderCachedContents();
                return;
            }

            ShowState("(Loading...)");
            var token = _iconLoadCancel?.Token ?? CancellationToken.None;
            var loaded = await WarmSpecialFolderRootAsync(_config, root, openedByUser: true, warmTreeInline: false);
            if (token.IsCancellationRequested || !_isOpen || _isClosing) return;

            if (loaded)
                RenderCachedContents();
            else
                ShowState("(Unavailable)");
        }

        if (IsSpecialFolderRootReady(_config, root))
            RenderCachedContents();
        else
        {
            ShowState("(Loading...)");
            WarmAndRender();
        }
        return sub;
    }

    private MenuFlyoutSubItem CreateTaskKillSubmenu(ConfigItem item, bool inSubmenu)
    {
        var sub = new MenuFlyoutSubItem { Text = item.Label };
        ApplyIcon(sub, item, inSubmenu);
        var snapshot = GetTaskKillSnapshot(allowStale: true);
        if (snapshot == null)
            sub.Items.Add(new MenuFlyoutItem { Text = "(Loading...)", IsEnabled = false });
        else
            RenderTaskKillItems(sub.Items, snapshot);
        RefreshTaskKillSnapshotAsync(sub);
        return sub;
    }

    private string GetTaskKillCacheKey()
        => GetTaskKillCacheKey(_config);

    private static string GetTaskKillCacheKey(AppConfig config)
        => string.Join("|",
            config.TaskKillMax,
            config.TaskKillIgnoreSystem,
            config.TaskKillListWindows,
            config.TaskKillAllDesktops,
            config.TaskKillExcludes);

    private IReadOnlyList<ProcessEntry>? GetTaskKillSnapshot(bool allowStale = false)
    {
        var key = GetTaskKillCacheKey();
        lock (TaskKillCacheLock)
        {
            if (TaskKillCache != null &&
                TaskKillCacheKey == key &&
                (allowStale || DateTime.UtcNow - TaskKillCacheTimeUtc < TaskKillCacheTtl))
                return TaskKillCache.ToList();
        }

        return null;
    }

    private void RefreshTaskKillSnapshotAsync(MenuFlyoutSubItem? sub)
    {
        var key = GetTaskKillCacheKey();
        var dispatcher = DispatcherQueue;
        var token = _iconLoadCancel?.Token ?? CancellationToken.None;
        long generation, request;
        lock (TaskKillCacheLock)
        {
            if (TaskKillRefreshInProgress && sub == null) return;
            if (TaskKillCache != null &&
                TaskKillCacheKey == key &&
                DateTime.UtcNow - TaskKillCacheTimeUtc < TaskKillCacheTtl)
                return;
            TaskKillRefreshInProgress = true;
            generation = TaskKillCacheGeneration;
            request = ++TaskKillNextRequest;
        }

        var config = _config;
        _ = System.Threading.Tasks.Task.Run(() =>
            {
                try { return CommandExecutor.GetRunningProcesses(config); }
                catch { return new List<ProcessEntry>(); }
            })
            .ContinueWith(task =>
            {
                if (!dispatcher.TryEnqueue(() =>
                {
                    var fresh = task.Status == System.Threading.Tasks.TaskStatus.RanToCompletion
                        ? task.Result
                        : new List<ProcessEntry>();

                    lock (TaskKillCacheLock)
                    {
                        if (generation != TaskKillCacheGeneration) return;
                        if (request == TaskKillNextRequest) TaskKillRefreshInProgress = false;
                        if (request >= TaskKillCommittedRequest)
                        {
                            TaskKillCommittedRequest = request;
                            TaskKillCache = fresh;
                            TaskKillCacheKey = key;
                            TaskKillCacheTimeUtc = DateTime.UtcNow;
                        }
                        else if (TaskKillCache != null && TaskKillCacheKey == key)
                            fresh = TaskKillCache.ToList();
                    }

                    if (!token.IsCancellationRequested && !_isClosing && sub != null)
                        RenderTaskKillItems(sub.Items, fresh);
                }))
                {
                    lock (TaskKillCacheLock)
                    {
                        if (generation == TaskKillCacheGeneration && request == TaskKillNextRequest)
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
            var pi  = new MenuFlyoutItem { Text = _config.TaskKillShowProcessIds ? $"{p.Name}  (PID {p.Pid})" : p.Name };
            var pid = p.Pid;
            pi.Click += (_, _) =>
            {
                CloseMenuWindow();
                CommandExecutor.KillProcess(pid);
                RefreshTaskKillSnapshotAsync(null);
            };
            if (ShouldShowItemIcon(isFolder: false))
            {
                try
                {
                    var isFrameHost = string.Equals(Path.GetFileName(p.ExePath),
                        "ApplicationFrameHost.exe", StringComparison.OrdinalIgnoreCase);

                    if (p.PreferWindowIdentity)
                        pi.Icon = IconLoader.LoadAppUserModelIcon(p.AppUserModelId);
                    if (pi.Icon == null && p.PreferWindowIdentity)
                        pi.Icon = IconLoader.LoadKnownHostedAppIcon(p.Name);
                    if (pi.Icon == null && p.PreferWindowIdentity)
                        pi.Icon = IconLoader.LoadWindowIcon(p.Hwnd);
                    if (pi.Icon == null && isFrameHost)
                        pi.Icon = IconLoader.LoadAppUserModelIcon(p.AppUserModelId);
                    if (pi.Icon == null && isFrameHost)
                        pi.Icon = IconLoader.LoadKnownHostedAppIcon(p.Name);
                    if (pi.Icon == null && isFrameHost)
                        pi.Icon = IconLoader.LoadWindowIcon(p.Hwnd);
                    if (pi.Icon == null)
                        pi.Icon = IconLoader.LoadKnownHostedAppIcon(p.Name);
                    if (pi.Icon == null && p.ExePath != null)
                        pi.Icon = IconLoader.Load(p.ExePath);
                    if (pi.Icon == null)
                        pi.Icon = IconLoader.LoadWindowIcon(p.Hwnd);
                }
                catch { }
            }
            target.Add(pi);
        }

        if (target.Count == 0)
            target.Add(new MenuFlyoutItem { Text = "(None)", IsEnabled = false });

        NormalizeFlyoutMetrics(target);
    }

    private void AddFolderEntries(
        IList<MenuFlyoutItemBase> target,
        string folderPath,
        int depth = 1,
        bool prepopulateSmallSubmenus = false,
        bool itemsInRootMenu = false,
        bool useSpecialFolderCache = false)
    {
        RenderFolderEntries(target, GetCachedFolderContents(folderPath, depth), depth, prepopulateSmallSubmenus, itemsInRootMenu, useSpecialFolderCache);
    }

    private void AddCachedFolderEntriesOrPlaceholder(
        IList<MenuFlyoutItemBase> target,
        string folderPath,
        int depth = 1,
        bool prepopulateSmallSubmenus = false,
        bool itemsInRootMenu = false,
        bool useSpecialFolderCache = false)
    {
        if (TryGetCachedFolderContents(folderPath, out var entries))
        {
            RenderFolderEntries(target, entries, depth, prepopulateSmallSubmenus, itemsInRootMenu, useSpecialFolderCache);
            return;
        }

        target.Add(new MenuFlyoutItem { Text = "(Loading...)", IsEnabled = false });
    }

    private void RenderFolderEntries(
        IList<MenuFlyoutItemBase> target,
        IReadOnlyList<FolderEntry> entries,
        int depth,
        bool prepopulateSmallSubmenus,
        bool itemsInRootMenu,
        bool useSpecialFolderCache = false)
    {
        var maxDepth = Math.Max(1, _config.FolderSubmenuDepth);

        foreach (var entry in entries)
            AddFolderEntry(target, entry, depth, maxDepth, prepopulateSmallSubmenus, itemsInRootMenu, useSpecialFolderCache);

        if (entries.Count == 0)
            target.Add(new MenuFlyoutItem { Text = "(None)", IsEnabled = false });
    }

    private void AddFolderEntry(
        IList<MenuFlyoutItemBase> target,
        FolderEntry e,
        int depth,
        int maxDepth,
        bool prepopulateSmallSubmenus,
        bool itemsInRootMenu,
        bool useSpecialFolderCache = false)
    {
        if (e.IsDirectory && depth < maxDepth)
        {
            target.Add(CreateLazyFolderSubmenu(
                e,
                depth + 1,
                showIcon: ShouldShowFileSystemObjectIcon(isFolder: true, isRoot: itemsInRootMenu),
                allowInitialPopulation: prepopulateSmallSubmenus,
                useSpecialFolderCache: useSpecialFolderCache));
            return;
        }

        var fi = new MenuFlyoutItem { Text = e.Name };
        fi.Click += (_, _) => { CloseMenuWindow(); CommandExecutor.ShellOpen(e.FullPath); };
        AttachPathContextMenu(fi, e.FullPath);
        if (e.IsDirectory)
        {
            if (ShouldShowFileSystemObjectIcon(isFolder: true, isRoot: itemsInRootMenu))
                DeferFolderPathIcon(fi, e.FullPath);
        }
        else if (ShouldShowFileSystemObjectIcon(isFolder: false, isRoot: itemsInRootMenu))
        {
            DeferFilePathIcon(fi, e.FullPath);
        }
        target.Add(fi);
    }

    private MenuFlyoutSubItem CreateLazyFolderSubmenu(
        FolderEntry entry,
        int depth,
        bool? showIcon = null,
        bool allowInitialPopulation = true,
        bool useSpecialFolderCache = false)
    {
        var sub = new MenuFlyoutSubItem { Text = entry.Name };
        if (showIcon ?? ShouldShowItemIcon(isFolder: true)) DeferFolderPathIcon(sub, entry.FullPath);
        AttachFolderSubmenuInteractions(sub, entry.FullPath);

        var loaded = false;
        var loading = false;

        void RenderEntries(IReadOnlyList<FolderEntry> entries)
        {
            sub.Items.Clear();
            RenderFolderEntries(sub.Items, entries, depth, prepopulateSmallSubmenus: false, itemsInRootMenu: false, useSpecialFolderCache: useSpecialFolderCache);
            NormalizeFlyoutMetrics(sub.Items);
        }

        void ShowLoading()
        {
            sub.Items.Clear();
            sub.Items.Add(new MenuFlyoutItem { Text = "(Loading...)", IsEnabled = false });
            NormalizeFlyoutMetrics(sub.Items);
        }

        async void Populate()
        {
            if (loaded || loading) return;

            if (TryGetCachedFolderContents(entry.FullPath, out var cached, out var isSpecialSnapshot) &&
                (isSpecialSnapshot || CanReuseFolderMetadataAtDepth(depth)))
            {
                loaded = true;
                RenderEntries(cached);
                return;
            }

            loading = true;
            ShowLoading();

            var config = _config;
            var token = _iconLoadCancel?.Token ?? CancellationToken.None;
            List<FolderEntry> entries;
            try
            {
                entries = await Task.Run(() => CommandExecutor.GetFolderContents(entry.FullPath, config));
            }
            catch
            {
                entries = new List<FolderEntry>();
            }

            if (token.IsCancellationRequested || !_isOpen || _isClosing)
                return;

            if (useSpecialFolderCache)
                StoreSpecialFolderContents(config, entry.FullPath, entries, depth);
            else
                StoreFolderContents(config, entry.FullPath, entries, depth);

            loading = false;
            loaded = true;
            RenderEntries(entries);
        }

        if (TryGetCachedFolderContents(entry.FullPath, out var initialEntries, out var initialIsSpecialSnapshot) &&
            (initialIsSpecialSnapshot || CanReuseFolderMetadataAtDepth(depth)))
        {
            loaded = true;
            RenderEntries(initialEntries);
        }
        else
        {
            ShowLoading();
        }

        sub.AddHandler(UIElement.PointerEnteredEvent, new PointerEventHandler((_, _) => Populate()), true);
        sub.GotFocus += (_, _) => Populate();

        return sub;
    }

    private void AttachFolderSubmenuInteractions(MenuFlyoutSubItem item, string path)
    {
        item.AddHandler(UIElement.PointerPressedEvent, new PointerEventHandler((_, args) =>
        {
            var properties = args.GetCurrentPoint(item).Properties;
            if (properties.IsRightButtonPressed)
            {
                args.Handled = true;
                ShowShellContextMenu(path);
            }
            else if (properties.IsLeftButtonPressed)
            {
                args.Handled = true;
                CloseMenuWindow();
                CommandExecutor.ShellOpen(path);
            }
        }), true);

        item.AddHandler(UIElement.RightTappedEvent, new RightTappedEventHandler((_, args) =>
        {
            args.Handled = true;
            ShowShellContextMenu(path);
        }), true);
    }

    private void AttachPathContextMenu(MenuFlyoutItemBase item, string path)
    {
        item.RightTapped += (_, args) =>
        {
            args.Handled = true;
            ShowShellContextMenu(path);
        };
    }

    private void ShowShellContextMenu(string path)
    {
        var hwnd = WinRT.Interop.WindowNative.GetWindowHandle(this);
        var invoked = false;
        _shellContextMenuOpen = true;
        SetFlyoutHitTesting(false);

        try
        {
            invoked = CommandExecutor.ShowShellContextMenu(path, hwnd);
        }
        finally
        {
            _shellContextMenuOpen = false;
            if (!_isClosing && (!invoked || _config.KeepMenuOpenAfterContextAction))
                SetFlyoutHitTesting(true);
        }

        if (invoked && !_config.KeepMenuOpenAfterContextAction)
            CloseMenuWindow();
    }

    private void SetFlyoutHitTesting(bool enabled)
    {
        if (_flyout == null) return;
        SetFlyoutItemsHitTesting(_flyout.Items, enabled);
    }

    private static void SetFlyoutItemsHitTesting(IList<MenuFlyoutItemBase> items, bool enabled)
    {
        foreach (var item in items)
        {
            if (item is FrameworkElement element)
                element.IsHitTestVisible = enabled;
            if (item is MenuFlyoutSubItem submenu)
                SetFlyoutItemsHitTesting(submenu.Items, enabled);
        }
    }

    private bool ShouldEagerPopulateFolderSubmenu(string folderPath, int depth)
    {
        if (IsDriveRoot(folderPath))
            return true;

        if (ShouldUseCachedFolderMetadata(folderPath, depth))
            return true;

        return false;
    }

    private bool ShouldUseCachedFolderMetadata(string folderPath, int depth)
    {
        if (!TryGetCachedFolderContents(folderPath, out _, out var isSpecialSnapshot))
            return false;
        return isSpecialSnapshot || CanReuseFolderMetadataAtDepth(depth);
    }

    private static bool IsDriveRoot(string folderPath)
    {
        try
        {
            var full = Path.GetFullPath(folderPath);
            var root = Path.GetPathRoot(full);
            return !string.IsNullOrEmpty(root) &&
                   string.Equals(
                       full.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar),
                       root.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar),
                       StringComparison.OrdinalIgnoreCase);
        }
        catch
        {
            return false;
        }
    }

    private static bool IsKnownHeavySystemPath(string folderPath)
    {
        try
        {
            var full = NormalizeFolderPath(folderPath);
            var windows = Environment.GetFolderPath(Environment.SpecialFolder.Windows);
            var system = Environment.GetFolderPath(Environment.SpecialFolder.System);
            var programData = Environment.GetFolderPath(Environment.SpecialFolder.CommonApplicationData);

            var heavyExact = new[]
            {
                windows,
                system,
                Path.Combine(windows, "SysWOW64"),
                Path.Combine(windows, "SoftwareDistribution"),
                Path.Combine(windows, "SystemApps"),
                programData
            };

            if (heavyExact.Any(path => PathEquals(full, path)))
                return true;

            var heavyTrees = new[]
            {
                Path.Combine(windows, "WinSxS"),
                Path.Combine(windows, "Installer")
            };

            return heavyTrees.Any(path => PathEquals(full, path) || IsPathUnder(full, path));
        }
        catch
        {
            return false;
        }
    }

    private static string NormalizeFolderPath(string path)
        => Path.GetFullPath(path)
               .TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);

    private static bool PathEquals(string normalizedPath, string path)
        => !string.IsNullOrWhiteSpace(path) &&
           string.Equals(normalizedPath, NormalizeFolderPath(path), StringComparison.OrdinalIgnoreCase);

    private static bool IsPathUnder(string normalizedPath, string parentPath)
    {
        if (string.IsNullOrWhiteSpace(parentPath))
            return false;

        var parent = NormalizeFolderPath(parentPath) + Path.DirectorySeparatorChar;
        return normalizedPath.StartsWith(parent, StringComparison.OrdinalIgnoreCase);
    }

    private List<FolderEntry> GetCachedFolderContents(string folderPath, int depth)
    {
        if (TryGetCachedFolderContents(folderPath, out var cached, out var isSpecialSnapshot) &&
            (isSpecialSnapshot || CanReuseFolderMetadataAtDepth(depth)))
            return cached;

        var entries = CommandExecutor.GetFolderContents(folderPath, _config);
        StoreFolderContents(folderPath, entries, depth);
        return entries;
    }

    private bool TryGetCachedFolderContents(string folderPath, out List<FolderEntry> entries)
        => TryGetCachedFolderContents(folderPath, out entries, out _);

    private bool TryGetCachedFolderContents(string folderPath, out List<FolderEntry> entries, out bool isSpecialSnapshot)
    {
        entries = new List<FolderEntry>();
        isSpecialSnapshot = false;
        var key = CreateFolderCacheKey(folderPath);

        lock (FolderCacheLock)
        {
            if (!FolderCache.TryGetValue(key, out var cached))
                return false;

            if (cached.IsSpecialSnapshot)
            {
                cached.LastAccessUtc = DateTime.UtcNow;
                entries = cached.Items.Select(i => i with { }).ToList();
                isSpecialSnapshot = true;
                return true;
            }
        }

        var stamp = GetFolderStamp(folderPath);
        lock (FolderCacheLock)
        {
            if (!FolderCache.TryGetValue(key, out var cached))
                return false;

            if (!cached.IsSpecialSnapshot && (stamp == null || cached.LastWriteUtc != stamp.Value))
            {
                FolderCache.Remove(key);
                return false;
            }

            cached.LastAccessUtc = DateTime.UtcNow;
            entries = cached.Items.Select(i => i with { }).ToList();
            isSpecialSnapshot = cached.IsSpecialSnapshot;
            return true;
        }
    }

    private void StoreFolderContents(string folderPath, List<FolderEntry> entries, int depth)
        => StoreFolderContents(_config, folderPath, entries, depth);

    private static void StoreFolderContents(AppConfig config, string folderPath, List<FolderEntry> entries, int depth)
    {
        if (!CanReuseFolderMetadataAtDepth(config, depth))
        {
            lock (FolderCacheLock)
            {
                var cacheKey = CreateFolderCacheKey(config, folderPath);
                if (FolderCache.TryGetValue(cacheKey, out var cached) && !cached.IsSpecialSnapshot)
                    FolderCache.Remove(cacheKey);
            }
            return;
        }

        var stamp = GetFolderStamp(folderPath);
        if (stamp == null) return;
        var key = CreateFolderCacheKey(config, folderPath);

        lock (FolderCacheLock)
        {
            FolderCache[key] = new FolderCacheEntry(
                entries.Select(i => i with { }).ToList(),
                stamp.Value,
                DateTime.UtcNow,
                IsSpecialSnapshot: false);

            var standardEntries = FolderCache.Where(kvp => !kvp.Value.IsSpecialSnapshot).ToList();
            if (standardEntries.Count > FolderCacheLimit)
            {
                foreach (var cacheKey in standardEntries.OrderBy(kvp => kvp.Value.LastAccessUtc)
                                                    .Take(standardEntries.Count - FolderCacheLimit)
                                                    .Select(kvp => kvp.Key)
                                                    .ToList())
                {
                    FolderCache.Remove(cacheKey);
                }
            }
        }
    }

    private static bool StoreSpecialFolderContents(AppConfig config, string folderPath, List<FolderEntry> entries, int depth)
    {
        var stamp = GetFolderStamp(folderPath);
        if (stamp == null) return false;

        var key = CreateFolderCacheKey(config, folderPath);
        lock (FolderCacheLock)
        {
            FolderCache[key] = new FolderCacheEntry(
                entries.Select(i => i with { }).ToList(),
                stamp.Value,
                DateTime.UtcNow,
                IsSpecialSnapshot: true);
        }
        return true;
    }

    private FolderCacheKey CreateFolderCacheKey(string folderPath)
        => CreateFolderCacheKey(_config, folderPath);

    private static FolderCacheKey CreateFolderCacheKey(AppConfig config, string folderPath)
        => new(
            Path.GetFullPath(folderPath).TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar).ToUpperInvariant(),
            config.ShowHidden,
            config.ShowDotfiles,
            config.ShowFileExtensions,
            config.MaxItems,
            config.SortBy,
            config.SortDirection,
            config.FoldersFirst);

    private static DateTime? GetFolderStamp(string folderPath)
    {
        try { return Directory.GetLastWriteTimeUtc(folderPath); }
        catch { return null; }
    }

    private sealed record FolderCacheKey(
        string Path,
        bool ShowHidden,
        bool ShowDotfiles,
        bool ShowFileExtensions,
        int MaxItems,
        string SortBy,
        string SortDirection,
        bool FoldersFirst);

    private sealed record FolderCacheEntry(
        List<FolderEntry> Items,
        DateTime LastWriteUtc,
        DateTime LastAccessUtc,
        bool IsSpecialSnapshot)
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

    private static bool IsProtocolShortcutTarget(string? targetPath)
    {
        if (string.IsNullOrWhiteSpace(targetPath))
            return false;

        var value = targetPath.Trim();
        var colon = value.IndexOf(':');
        if (colon <= 0)
            return false;

        if (colon == 1 && value.Length > 2 && char.IsLetter(value[0]) &&
            (value[2] == '\\' || value[2] == '/'))
            return false;

        if (!char.IsLetter(value[0]))
            return false;

        for (var i = 1; i < colon; i++)
        {
            var ch = value[i];
            if (!char.IsLetterOrDigit(ch) && ch != '+' && ch != '-' && ch != '.')
                return false;
        }

        return true;
    }

    private static bool IsExistingFileSystemTarget(string? targetPath)
    {
        return !string.IsNullOrWhiteSpace(targetPath) &&
               (File.Exists(targetPath) || Directory.Exists(targetPath));
    }

    private void AddRecentItems(IList<MenuFlyoutItemBase> target)
    {
        RenderRecentItems(target, BuildRecentEntries(_config));
    }

    private void AddCachedRecentItemsOrPlaceholder(IList<MenuFlyoutItemBase> target)
    {
        var key = GetRecentCacheKey(_config);
        lock (RecentCacheLock)
        {
            if (RecentCache != null && RecentCacheKey == key)
            {
                RenderRecentItems(target, RecentCache);
                if (DateTime.UtcNow - RecentCacheTimeUtc >= RecentCacheTtl)
                    RefreshRecentItemsAsync(target);
                return;
            }
        }

        target.Add(new MenuFlyoutItem { Text = "(Loading...)", IsEnabled = false });
        RefreshRecentItemsAsync(target);
    }

    private void RefreshRecentItemsAsync(IList<MenuFlyoutItemBase> target)
    {
        var key = GetRecentCacheKey(_config);
        var config = _config;
        var dispatcher = DispatcherQueue;
        var token = _iconLoadCancel?.Token ?? CancellationToken.None;
        long generation;

        lock (RecentCacheLock)
        {
            if (RecentRefreshInProgress && target.Count > 0 &&
                target[0] is not MenuFlyoutItem { IsEnabled: false, Text: "(Loading...)" }) return;
            if (RecentCache != null &&
                RecentCacheKey == key &&
                DateTime.UtcNow - RecentCacheTimeUtc < RecentCacheTtl)
                return;

            RecentRefreshInProgress = true;
            generation = RecentCacheGeneration;
        }

        _ = System.Threading.Tasks.Task.Run(() => BuildRecentEntries(config))
            .ContinueWith(task =>
            {
                if (!dispatcher.TryEnqueue(() =>
                {
                    var fresh = task.Status == System.Threading.Tasks.TaskStatus.RanToCompletion
                        ? task.Result
                        : new List<RecentMenuEntry>();
                    var shouldRender = false;

                    lock (RecentCacheLock)
                    {
                        RecentRefreshInProgress = false;
                        if (generation == RecentCacheGeneration)
                        {
                            StoreRecentCache(config, fresh);
                            shouldRender = true;
                        }
                    }

                    if (shouldRender && !token.IsCancellationRequested && !_isClosing)
                    {
                        target.Clear();
                        RenderRecentItems(target, fresh);
                        NormalizeFlyoutMetrics(target);
                    }
                }))
                {
                    lock (RecentCacheLock)
                    {
                        RecentRefreshInProgress = false;
                    }
                }
            });
    }

    private void RenderRecentItems(IList<MenuFlyoutItemBase> target, IReadOnlyList<RecentMenuEntry> recentItems)
    {
        try
        {
            var recentDir = Environment.GetFolderPath(Environment.SpecialFolder.Recent);

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
                    ClearRecentItems(recentDir);
                };
                target.Add(clear);
            }
        }
        catch { }
    }

    private sealed record RecentMenuEntry(string DisplayName, string Path, bool IsFolder);

    private static List<RecentMenuEntry> BuildRecentEntries(AppConfig config)
    {
        var recentItems = new List<RecentMenuEntry>();

        try
        {
            var recentDir = Environment.GetFolderPath(Environment.SpecialFolder.Recent);
            var maxRecentItems = config.RecentMax > 0 ? config.RecentMax : 12;

            foreach (var lnk in Directory.EnumerateFiles(recentDir, "*.lnk")
                                         .OrderByDescending(File.GetLastWriteTime))
            {
                var targetPath = ResolveLnkTarget(lnk);
                if (config.RecentHideProtocolShortcuts && !IsExistingFileSystemTarget(targetPath))
                    continue;

                recentItems.Add(new RecentMenuEntry(
                    BuildRecentDisplayName(config, lnk, targetPath),
                    targetPath ?? lnk,
                    targetPath != null && Directory.Exists(targetPath)));

                if (recentItems.Count >= maxRecentItems)
                    break;
            }
        }
        catch { }

        return recentItems;
    }

    private static string GetRecentCacheKey(AppConfig config)
        => string.Join("|",
            config.RecentMax,
            config.RecentLabel,
            config.RecentHideProtocolShortcuts,
            config.RecentSeparateItems,
            config.ShowFileExtensions);

    private void ClearRecentItems(string recentDir)
    {
        try
        {
            foreach (var file in Directory.EnumerateFiles(recentDir, "*.lnk"))
                try { File.Delete(file); } catch { }
        }
        catch { /* Missing/inaccessible directory: still refresh the displayed snapshot. */ }
        finally
        {
            // Preserve any shortcuts that could not be deleted. Publish before the
            // user can reopen the menu, and reject scans started before this clear.
            lock (RecentCacheLock)
            {
                ++RecentCacheGeneration;
                StoreRecentCache(_config, BuildRecentEntries(_config));
            }
        }
    }

    private static void RefreshRecentCache(AppConfig config)
    {
        long generation;
        lock (RecentCacheLock) generation = RecentCacheGeneration;
        var entries = BuildRecentEntries(config);
        lock (RecentCacheLock)
        {
            if (generation == RecentCacheGeneration)
                StoreRecentCache(config, entries);
        }
    }

    private static void StoreRecentCache(AppConfig config, List<RecentMenuEntry> entries)
    {
        lock (RecentCacheLock)
        {
            RecentCache = entries.ToList();
            RecentCacheKey = GetRecentCacheKey(config);
            RecentCacheTimeUtc = DateTime.UtcNow;
        }
    }

    private static string BuildRecentDisplayName(AppConfig config, string lnk, string? targetPath)
    {
        if (config.RecentLabel == "fullpath")
            return targetPath ?? Path.GetFileNameWithoutExtension(lnk);

        var baseName = targetPath != null
            ? GetRecentLeafName(targetPath)
            : Path.GetFileNameWithoutExtension(lnk);

        if (config.ShowFileExtensions || IsDriveRoot(baseName))
            return baseName;

        return Path.GetFileNameWithoutExtension(baseName);
    }

    private static string GetRecentLeafName(string targetPath)
    {
        var root = Path.GetPathRoot(targetPath);
        if (!string.IsNullOrWhiteSpace(root) &&
            string.Equals(targetPath.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar),
                          root.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar),
                          StringComparison.OrdinalIgnoreCase))
            return root;

        return Path.GetFileName(targetPath.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar));
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
        AttachPathContextMenu(fi, item.Path);
        if (ShouldShowItemIcon(item.IsFolder))
        {
            if (item.IsFolder) DeferFolderPathIcon(fi, item.Path);
            else DeferFilePathIcon(fi, item.Path);
        }
        return fi;
    }

    private void AddThisPCItems(
        IList<MenuFlyoutItemBase> target,
        bool itemsInRootMenu = false,
        bool useSpecialFolderCache = false,
        bool refreshIfStale = true)
    {
        var key = GetThisPCCacheKey(_config);
        List<FolderEntry>? entries = null;
        var stale = true;
        lock (ThisPCCacheLock)
        {
            if (ThisPCCache != null &&
                ThisPCCacheKey == key)
            {
                entries = ThisPCCache.Select(i => i with { }).ToList();
                stale = DateTime.UtcNow - ThisPCCacheTimeUtc >= ThisPCCacheTtl;
            }
        }

        var firstItem = target.Count;
        if (entries == null)
        {
            target.Add(new MenuFlyoutItem { Text = "(Loading...)", IsEnabled = false });
            if (refreshIfStale) RefreshDisplayedItems();
            return;
        }

        var canExpandDrives = _config.ThisPCItemsAsSubmenus && _config.FolderSubmenuDepth > 1;
        foreach (var entry in entries)
        {
            var label = entry.Name;
            var root = entry.FullPath;

            if (canExpandDrives)
            {
                target.Add(CreateLazyFolderSubmenu(entry, depth: 1,
                    showIcon: ShouldShowFileSystemObjectIcon(isFolder: true, isRoot: itemsInRootMenu),
                    allowInitialPopulation: true,
                    useSpecialFolderCache: useSpecialFolderCache));
            }
            else
            {
                var fi = new MenuFlyoutItem { Text = label };
                if (ShouldShowFileSystemObjectIcon(isFolder: true, isRoot: itemsInRootMenu)) DeferFolderPathIcon(fi, root);
                fi.Click += (_, _) => { CloseMenuWindow(); CommandExecutor.ShellOpen(root); };
                AttachPathContextMenu(fi, root);
                target.Add(fi);
            }
        }

        if (entries.Count == 0)
            target.Add(new MenuFlyoutItem { Text = "(None)", IsEnabled = false });

        if (refreshIfStale && stale) RefreshDisplayedItems();

        async void RefreshDisplayedItems()
        {
            // Inline menus share this collection with other configured items.
            // Replace only the entries owned by this This PC section.
            var owned = target.Skip(firstItem).ToList();
            var token = _iconLoadCancel?.Token ?? CancellationToken.None;
            try
            {
                var loaded = await WarmSpecialFolderRootAsync(_config,
                    SpecialFolderRoot.ThisPC, openedByUser: true, force: true);
                if (token.IsCancellationRequested || !_isOpen || _isClosing) return;
                var index = target.IndexOf(owned[0]);
                if (index < 0 || owned.Where((entry, offset) =>
                    index + offset >= target.Count || !ReferenceEquals(target[index + offset], entry)).Any()) return;
                if (!loaded)
                {
                    if (entries == null && owned[0] is MenuFlyoutItem marker)
                        marker.Text = "(Unavailable)";
                    return;
                }
                var replacement = new List<MenuFlyoutItemBase>();
                AddThisPCItems(replacement, itemsInRootMenu, useSpecialFolderCache, refreshIfStale: false);
                foreach (var entry in owned) target.Remove(entry);
                foreach (var entry in replacement) target.Insert(index++, entry);
                NormalizeFlyoutMetrics(target);
            }
            catch (Exception ex)
            {
                App.LogException("Refresh This PC", ex);
                if (!token.IsCancellationRequested && entries == null && owned[0] is MenuFlyoutItem marker)
                    marker.Text = "(Unavailable)";
            }
        }
    }

    private sealed record HomeKnownFolder(string Name, Guid Id);

    private static readonly HomeKnownFolder[] HomeKnownFolders =
    {
        new("Desktop", new Guid("B4BFCC3A-DB2C-424C-B029-7FE99A87C641")),
        new("Documents", new Guid("FDD39AD0-238F-46AF-ADB4-6C85480369C7")),
        new("Downloads", new Guid("374DE290-123F-4565-9164-39C4925E467B")),
        new("Pictures", new Guid("33E28130-4E1E-4676-835A-98395C3BC3BB")),
        new("Music", new Guid("4BD8D571-6D19-48D3-BE97-422220080E43")),
        new("Videos", new Guid("18989B1D-99B5-455B-841C-AB7C74E4DDFC"))
    };

    private static string NormalizeHomePath(string path)
    {
        try
        {
            return Path.GetFullPath(path).TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
        }
        catch
        {
            return path.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
        }
    }

    private static bool IsHomePathInside(string path, string root)
    {
        var normalizedPath = NormalizeHomePath(path);
        var normalizedRoot = NormalizeHomePath(root);
        return normalizedPath.Equals(normalizedRoot, StringComparison.OrdinalIgnoreCase) ||
               normalizedPath.StartsWith(normalizedRoot + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase);
    }

    private static string? ResolveKnownFolderPath(Guid folderId)
    {
        var id = folderId;
        var result = SHGetKnownFolderPath(ref id, 0, IntPtr.Zero, out var pathPtr);
        if (result != 0 || pathPtr == IntPtr.Zero) return null;
        try
        {
            return Marshal.PtrToStringUni(pathPtr);
        }
        finally
        {
            CoTaskMemFree(pathPtr);
        }
    }

    private List<FolderEntry> GetHomeFolderContents(string homePath)
        => GetHomeFolderContents(_config, homePath);

    private static List<FolderEntry> GetHomeFolderContents(AppConfig config, string homePath)
    {
        // Home adds known folders outside the profile root, so collect the full
        // set before sorting and applying the global maximum.
        var entries = CommandExecutor.GetFolderContents(homePath, config, applyMaxItems: false);
        var existing = new HashSet<string>(
            entries.Select(e => NormalizeHomePath(e.FullPath)),
            StringComparer.OrdinalIgnoreCase);

        foreach (var knownFolder in HomeKnownFolders)
        {
            var path = ResolveKnownFolderPath(knownFolder.Id);
            if (string.IsNullOrWhiteSpace(path) || !Directory.Exists(path)) continue;
            if (IsHomePathInside(path, homePath)) continue;

            var normalizedPath = NormalizeHomePath(path);
            if (!existing.Add(normalizedPath)) continue;

            entries.Add(new FolderEntry
            {
                Name = knownFolder.Name,
                FullPath = path,
                IsDirectory = true
            });
        }

        return CommandExecutor.SortFolderEntries(entries, config);
    }

    private void AddCachedHomeFolderItemsOrPlaceholder(IList<MenuFlyoutItemBase> target, string homePath)
    {
        if (TryGetCachedFolderContents(homePath, out var entries))
        {
            RenderHomeFolderItems(target, entries, itemsInRootMenu: false, useSpecialFolderCache: true);
            return;
        }

        target.Add(new MenuFlyoutItem { Text = "(Loading...)", IsEnabled = false });
    }

    private void AddHomeFolderItems(
        IList<MenuFlyoutItemBase> target,
        string homePath,
        bool itemsInRootMenu = false,
        bool useSpecialFolderCache = false)
    {
        List<FolderEntry> entries;
        if (useSpecialFolderCache)
        {
            if (!TryGetCachedFolderContents(homePath, out entries))
            {
                target.Add(new MenuFlyoutItem { Text = "(Unavailable)", IsEnabled = false });
                return; // A missing/expired snapshot must not trigger UI-thread I/O.
            }
        }
        else entries = GetHomeFolderContents(homePath);
        RenderHomeFolderItems(target, entries, itemsInRootMenu, useSpecialFolderCache);
    }

    private void RenderHomeFolderItems(
        IList<MenuFlyoutItemBase> target,
        IReadOnlyList<FolderEntry> entries,
        bool itemsInRootMenu,
        bool useSpecialFolderCache)
    {
        var canExpandFolders = _config.HomeItemsAsSubmenus && _config.FolderSubmenuDepth > 1;
        foreach (var entry in entries)
        {
            var e = entry;

            if (canExpandFolders && e.IsDirectory)
            {
                target.Add(CreateLazyFolderSubmenu(e, depth: 1,
                    showIcon: ShouldShowFileSystemObjectIcon(isFolder: true, isRoot: itemsInRootMenu),
                    allowInitialPopulation: false,
                    useSpecialFolderCache: useSpecialFolderCache));
            }
            else
            {
                var fi = new MenuFlyoutItem { Text = e.Name };
                if (ShouldShowFileSystemObjectIcon(e.IsDirectory, isRoot: itemsInRootMenu))
                {
                    if (e.IsDirectory) DeferFolderPathIcon(fi, e.FullPath);
                    else DeferFilePathIcon(fi, e.FullPath);
                }
                fi.Click += (_, _) => { CloseMenuWindow(); CommandExecutor.ShellOpen(e.FullPath); };
                AttachPathContextMenu(fi, e.FullPath);
                target.Add(fi);
            }
        }

        if (entries.Count == 0)
            target.Add(new MenuFlyoutItem { Text = "(None)", IsEnabled = false });
    }

    private static List<FolderEntry> BuildThisPCEntries(AppConfig config)
    {
        var drives = DriveInfo.GetDrives()
            .Where(d => d.IsReady)
            .Select(drive => new FolderEntry
            {
                Name = string.IsNullOrEmpty(drive.VolumeLabel)
                    ? drive.Name
                    : $"{drive.VolumeLabel} ({drive.Name.TrimEnd('\\')})",
                FullPath = drive.RootDirectory.FullName,
                IsDirectory = true
            });

        if (config.SortBy.Equals("name", StringComparison.OrdinalIgnoreCase))
        {
            var descending = config.SortDirection.Equals("descending", StringComparison.OrdinalIgnoreCase);
            return descending
                ? drives.OrderByDescending(d => d.FullPath, StringComparer.OrdinalIgnoreCase).ToList()
                : drives.OrderBy(d => d.FullPath, StringComparer.OrdinalIgnoreCase).ToList();
        }

        return CommandExecutor.SortFolderEntries(drives, config, applyMaxItems: false);
    }

    private static string GetThisPCCacheKey(AppConfig config)
        => string.Join("|",
            config.ThisPCItemsAsSubmenus,
            config.SortBy,
            config.SortDirection,
            config.FoldersFirst);

    private static void StoreThisPCCache(AppConfig config, List<FolderEntry> entries)
    {
        lock (ThisPCCacheLock)
        {
            ThisPCCache = entries.Select(i => i with { }).ToList();
            ThisPCCacheKey = GetThisPCCacheKey(config);
            ThisPCCacheTimeUtc = DateTime.UtcNow;
        }
    }

    private void DeferFolderPathIcon(MenuFlyoutItemBase target, string path)
    {
        DeferPathIcon(target, path, iconOnly: false);
    }

    private void DeferFilePathIcon(MenuFlyoutItemBase target, string path)
    {
        DeferPathIcon(target, path, iconOnly: _config.WinUIAlwaysShowIcons);
    }

    private void DeferPathIcon(MenuFlyoutItemBase target, string path, bool iconOnly)
    {
        if (_isClosing || string.IsNullOrWhiteSpace(path)) return;

        var cachedUri = IconLoader.GetMemoryCachedUri(path, iconOnly);
        if (cachedUri != null)
        {
            var icon = IconLoader.CreateImageIcon(cachedUri);
            if (target is MenuFlyoutItem fi) fi.Icon = icon;
            else if (target is MenuFlyoutSubItem si) si.Icon = icon;
            return;
        }

        var token = _iconLoadCancel?.Token ?? CancellationToken.None;
        var dispatcher = target.DispatcherQueue ?? DispatcherQueue;
        _ = LoadPathIconAsync(target, dispatcher, path, iconOnly, token);
    }

    private async Task LoadPathIconAsync(
        MenuFlyoutItemBase target,
        Microsoft.UI.Dispatching.DispatcherQueue dispatcher,
        string path,
        bool iconOnly,
        CancellationToken token)
    {
        Uri? uri = null;
        var enteredGate = false;
        var started = System.Diagnostics.Stopwatch.GetTimestamp();
        using var timeout = new CancellationTokenSource(DeferredIconLoadTimeoutMs);
        using var linked = CancellationTokenSource.CreateLinkedTokenSource(token, timeout.Token);
        try
        {
            await DeferredIconLoadGate.WaitAsync(linked.Token).ConfigureAwait(false);
            enteredGate = true;
            uri = await Task.Run(() => IconLoader.ResolveUri(path, iconOnly), linked.Token).ConfigureAwait(false);
        }
        catch (OperationCanceledException)
        {
            return;
        }
        catch
        {
            return;
        }
        finally
        {
            if (enteredGate)
                DeferredIconLoadGate.Release();
        }

        if (uri == null || token.IsCancellationRequested || _isClosing)
            return;

        var elapsedMs = System.Diagnostics.Stopwatch.GetElapsedTime(started).TotalMilliseconds;
        if (elapsedMs >= 250)
            App.LogBreadcrumb($"IconResolve elapsedMs={elapsedMs:F0} iconOnly={iconOnly} path={path}");

        dispatcher.TryEnqueue(() =>
        {
            if (token.IsCancellationRequested || _isClosing)
                return;

            var icon = IconLoader.CreateImageIcon(uri);
            if (target is MenuFlyoutItem fi) fi.Icon = icon;
            else if (target is MenuFlyoutSubItem si) si.Icon = icon;
        });
    }

    private bool ShouldShowItemIcon(bool isFolder, bool isRoot = false)
    {
        if (_config.IconMode == IconDisplayMode.Disabled) return false;
        if (isRoot && _config.HideIconsInRootMenu) return false;
        if (isRoot) return true;
        return ShouldShowFileSystemObjectIcon(isFolder, isRoot: false);
    }

    private bool ShouldShowFileSystemObjectIcon(bool isFolder, bool isRoot)
    {
        if (_config.IconMode == IconDisplayMode.Disabled) return false;
        if (isRoot && _config.HideIconsInRootMenu) return false;
        return _config.IconMode switch
        {
            IconDisplayMode.FilesOnly => !isFolder,
            IconDisplayMode.FoldersOnly => isFolder,
            _ => true,
        };
    }

    /// <summary>
    /// Resolves the best icon for a configured item under the global icon policy.
    /// </summary>
    private void ApplyIcon(
        MenuFlyoutItemBase target,
        ConfigItem item,
        bool inSubmenu,
        bool bypassRootIconPolicy = false)
    {
        var isFolder = item.Type is ConfigItemType.Folder or ConfigItemType.FolderSubmenu or ConfigItemType.ThisPC or ConfigItemType.Home;
        if (bypassRootIconPolicy && !inSubmenu)
        {
            if (_config.IconMode == IconDisplayMode.Disabled) return;
        }
        else if (!ShouldShowItemIcon(isFolder, isRoot: !inSubmenu))
        {
            return;
        }

        // Pick theme-aware path if available, fall back to generic then default.
        // The Settings command intentionally mirrors the tray icon selection.
        var isDark   = Application.Current.RequestedTheme == ApplicationTheme.Dark;
        var iconPath = item.Type == ConfigItemType.Settings ? ResolveSettingsTrayIconPath(isDark)
                     : isDark && !string.IsNullOrEmpty(item.IconPathDark)  ? item.IconPathDark
                     : !isDark && !string.IsNullOrEmpty(item.IconPathLight) ? item.IconPathLight
                     : !string.IsNullOrEmpty(item.IconPath)                 ? item.IconPath
                     : isDark && !string.IsNullOrEmpty(_config.DefaultIconPathDark) ? _config.DefaultIconPathDark
                     : !isDark && !string.IsNullOrEmpty(_config.DefaultIconPathLight) ? _config.DefaultIconPathLight
                     : _config.DefaultIconPath;

        if (string.IsNullOrEmpty(iconPath)) return;

        // Configured icons must be attached while constructing the menu item.
        // Keep filesystem-entry icon discovery deferred, but do not defer this
        // path: the flyout must see the configured icon during initial layout.
        var icon = IconLoader.Load(iconPath);
        if (icon == null) return;

        if (target is MenuFlyoutItem fi)        fi.Icon  = icon;
        else if (target is MenuFlyoutSubItem si) si.Icon  = icon;
    }

    private void ApplyFolderItemIcon(MenuFlyoutItemBase target, ConfigItem item, bool inSubmenu)
    {
        var hasConfiguredIcon = !string.IsNullOrEmpty(item.IconPath) ||
                                !string.IsNullOrEmpty(item.IconPathLight) ||
                                !string.IsNullOrEmpty(item.IconPathDark) ||
                                !string.IsNullOrEmpty(_config.DefaultIconPath) ||
                                !string.IsNullOrEmpty(_config.DefaultIconPathLight) ||
                                !string.IsNullOrEmpty(_config.DefaultIconPathDark);

        // A plain FOLDER represents its filesystem target, including at the
        // root, so HIR applies to it. FOLDER_SUBMENU remains a menu heading:
        // its root icon follows the master toggle and HIR, but not type mode.
        var showIcon = item.Type == ConfigItemType.Folder
            ? ShouldShowItemIcon(isFolder: true, isRoot: !inSubmenu)
            : inSubmenu
                ? ShouldShowItemIcon(isFolder: true)
                : ShouldShowItemIcon(isFolder: true, isRoot: true);
        if (!showIcon) return;

        if (hasConfiguredIcon)
        {
            ApplyIcon(target, item, inSubmenu);
            return;
        }

        DeferFolderPathIcon(target, item.Path);
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
            fi.Click += (_, _) => { CloseMenuWindow(); CommandExecutor.Execute(ci, _config); };
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

        PositionAnchorWindow();
    }

    private void PositionAnchorWindow()
    {
        // Position the 1x1 anchor window at the cursor or configured fixed point.
        // Outside-click dismissal is handled by a low-level mouse hook while the
        // flyout is open, avoiding any visible full-screen overlay.
        var hwnd = WinRT.Interop.WindowNative.GetWindowHandle(this);
        var anchorPt = _config.PointerRelative
            ? GetCursorAnchor()
            : GetFixedAnchor();

        GetAppWindowForCurrentWindow().MoveAndResize(new RectInt32(anchorPt.X, anchorPt.Y, 1, 1));
        _flyoutAnchorPoint = new Point(0, 0);
        HideAnchorWindowPixels(hwnd);
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
            SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
        // Dismissal hides (and deactivates) this retained owner. Merely showing
        // it with SWP_NOACTIVATE leaves XamlRoot's backdrop configuration inactive,
        // allowing desktop acrylic to fall back to its opaque color on reopen.
        // Activate through WinUI too, so its input state follows the native HWND.
        Activate();
        SetForegroundWindow(hwnd);
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
        var horizontal = _config.Horizontal.ToLowerInvariant();
        var vertical = _config.Vertical.ToLowerInvariant();

        // Horizontal: left/center/right edge of work area + HOffset
        int x = horizontal switch
        {
            "right"  => work.X + work.Width  + _config.HOffset,
            "center" => work.X + work.Width  / 2 + _config.HOffset,
            _        => work.X + _config.HOffset,           // "left" (default)
        };

        // Vertical: top/center/bottom edge of work area + VOffset
        int y = vertical switch
        {
            "bottom" => work.Y + work.Height + _config.VOffset,
            "center" => work.Y + work.Height / 2 + _config.VOffset,
            _        => work.Y + _config.VOffset,           // "top" (default)
        };

        // MenuFlyout keeps a small internal distance from its anchor on the
        // leading edges. Nudge only fixed top/left anchors so zero offset means
        // the visible WinUI menu starts exactly at the work-area border.
        if (horizontal != "right" && horizontal != "center")
            x -= FixedLeftEdgeFlyoutGap;
        if (vertical != "bottom" && vertical != "center")
            y -= FixedTopEdgeFlyoutGap;

        // Clamp to work area, allowing the calibrated top/left anchors to sit
        // just outside the visible edge.
        var minX = horizontal != "right" && horizontal != "center" ? work.X - FixedLeftEdgeFlyoutGap : work.X;
        var minY = vertical != "bottom" && vertical != "center" ? work.Y - FixedTopEdgeFlyoutGap : work.Y;
        x = Math.Clamp(x, minX, work.X + work.Width  - 1);
        y = Math.Clamp(y, minY, work.Y + work.Height - 1);

        return new PointInt32(x, y);
    }

    private AppWindow GetAppWindowForCurrentWindow()
    {
        var hwnd  = WinRT.Interop.WindowNative.GetWindowHandle(this);
        var wndId = Win32Interop.GetWindowIdFromWindow(hwnd);
        return AppWindow.GetFromWindowId(wndId);
    }
}
