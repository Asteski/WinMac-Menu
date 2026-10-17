using System.Diagnostics;
using System.Runtime.InteropServices;
using WinMacMenuWinUI3.Models;

namespace WinMacMenuWinUI3.Services;

public static class CommandExecutor
{
    [DllImport("user32.dll")]
    private static extern bool LockWorkStation();

    [DllImport("user32.dll", SetLastError = true)]
    private static extern bool ExitWindowsEx(uint uFlags, uint dwReason);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern int MessageBox(IntPtr hWnd, string text, string caption, uint type);

    [DllImport("powrprof.dll", SetLastError = true)]
    private static extern bool SetSuspendState(bool hibernate, bool forceCritical, bool disableWakeEvent);

    [DllImport("shell32.dll", CharSet = CharSet.Unicode)]
    private static extern int SHParseDisplayName(string pszName, IntPtr pbc, out IntPtr ppidl, uint sfgaoIn, out uint psfgaoOut);

    [DllImport("shell32.dll")]
    private static extern int SHBindToParent(IntPtr pidl, ref Guid riid, out IShellFolder ppv, out IntPtr ppidlLast);

    [DllImport("user32.dll")]
    private static extern IntPtr CreatePopupMenu();

    [DllImport("user32.dll")]
    private static extern bool DestroyMenu(IntPtr hMenu);

    [DllImport("user32.dll")]
    private static extern uint TrackPopupMenuEx(IntPtr hmenu, uint fuFlags, int x, int y, IntPtr hwnd, IntPtr lptpm);

    [DllImport("user32.dll")]
    private static extern bool SetForegroundWindow(IntPtr hWnd);

    [DllImport("user32.dll")]
    private static extern bool PostMessage(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam);

    [DllImport("user32.dll")]
    private static extern short GetKeyState(int nVirtKey);

    [DllImport("user32.dll")]
    private static extern bool GetCursorPos(out POINT lpPoint);
    [DllImport("user32.dll")]
    private static extern bool SetWindowPos(IntPtr hWnd, IntPtr hWndInsertAfter, int x, int y, int cx, int cy, uint uFlags);


    [DllImport("user32.dll")]
    private static extern bool IsIconic(IntPtr hWnd);

    [DllImport("user32.dll")]
    private static extern bool EnumThreadWindows(uint dwThreadId, EnumThreadWindowsProc lpfn, IntPtr lParam);


    [DllImport("kernel32.dll")]
    private static extern uint GetCurrentThreadId();

    [DllImport("ole32.dll")]
    private static extern int CoInitializeEx(IntPtr pvReserved, uint dwCoInit);

    [DllImport("ole32.dll")]
    private static extern void CoUninitialize();

    [DllImport("ole32.dll")]
    private static extern void CoTaskMemFree(IntPtr pv);

    private const uint TPM_LEFTALIGN = 0x0000;
    private const uint TPM_TOPALIGN = 0x0000;
    private const uint TPM_RETURNCMD = 0x0100;
    private const uint TPM_RIGHTBUTTON = 0x0002;
    private const uint TPM_RECURSE = 0x0001;
    private const uint SWP_NOSIZE = 0x0001;
    private const uint SWP_NOMOVE = 0x0002;
    private const uint SWP_NOACTIVATE = 0x0010;
    private const uint SWP_SHOWWINDOW = 0x0040;
    private const uint CMF_NORMAL = 0x0000;
    private const uint CMF_EXTENDEDVERBS = 0x0100;
    private const uint CMIC_MASK_UNICODE = 0x00004000;
    private const uint COINIT_APARTMENTTHREADED = 0x2;
    private const uint COINIT_DISABLE_OLE1DDE = 0x4;
    private const uint WM_NULL = 0x0000;
    private const uint WM_COMMAND = 0x0111;
    private const uint WM_CLOSE = 0x0010;
    private const uint WM_CANCELMODE = 0x001F;
    private const int SW_SHOWNORMAL = 1;
    private const int VK_SHIFT = 0x10;
    private const uint MB_ICONQUESTION = 0x00000020;
    private const uint MB_YESNO = 0x00000004;
    private const uint MB_DEFBUTTON2 = 0x00000100;
    private const int IDYES = 6;
    private static readonly IntPtr HWND_TOPMOST = new(-1);
    private static readonly Guid IID_IShellFolder = new("000214E6-0000-0000-C000-000000000046");
    private static readonly Guid IID_IContextMenu = new("000214E4-0000-0000-C000-000000000046");

    private delegate bool EnumThreadWindowsProc(IntPtr hWnd, IntPtr lParam);

    public static void Execute(ConfigItem item, AppConfig? config = null)
    {
        try
        {
            switch (item.Type)
            {
                case ConfigItemType.Uri:
                    OpenUri(item.Path);
                    break;
                case ConfigItemType.File:
                    ShellOpen(item.Path, item.Params);
                    break;
                case ConfigItemType.Cmd:
                    RunCmd(string.IsNullOrEmpty(item.Params) ? item.Path : item.Params);
                    break;
                case ConfigItemType.Folder:
                case ConfigItemType.FolderSubmenu:
                    ShellOpen(item.Path);
                    break;
                case ConfigItemType.PowerSleep:
                    if (!ConfirmPowerAction(config, "sleep")) break;
                    SetSuspendState(false, false, false);
                    break;
                case ConfigItemType.PowerHibernate:
                    if (!ConfirmPowerAction(config, "hibernate")) break;
                    SetSuspendState(true, false, false);
                    break;
                case ConfigItemType.PowerShutdown:
                    if (!ConfirmPowerAction(config, "shut down")) break;
                    Process.Start("shutdown", "/s /t 0");
                    break;
                case ConfigItemType.PowerRestart:
                    if (!ConfirmPowerAction(config, "restart")) break;
                    Process.Start("shutdown", "/r /t 0");
                    break;
                case ConfigItemType.PowerLock:
                    if (!ConfirmPowerAction(config, "lock")) break;
                    LockWorkStation();
                    break;
                case ConfigItemType.PowerLogoff:
                    if (!ConfirmPowerAction(config, "sign out")) break;
                    ExitWindowsEx(0x00, 0);
                    break;
                case ConfigItemType.ThisPC:
                    ShellOpen("shell:MyComputerFolder");
                    break;
                case ConfigItemType.Home:
                    ShellOpen("shell:UsersFilesFolder");
                    break;
            }
        }
        catch (Exception ex)
        {
            Debug.WriteLine($"Execute failed for {item.Type}: {ex.Message}");
        }
    }

    private static bool ConfirmPowerAction(AppConfig? config, string action)
    {
        if (config?.PowerAskForConfirmation != true) return true;
        return MessageBox(
            IntPtr.Zero,
            $"Are you sure you want to {action}?",
            "Confirm Power Action",
            MB_ICONQUESTION | MB_YESNO | MB_DEFBUTTON2) == IDYES;
    }

    public static void OpenUri(string uri)
    {
        if (string.IsNullOrEmpty(uri)) return;

        // .msc snap-ins live in System32 and must be launched via mmc.exe
        if (uri.EndsWith(".msc", StringComparison.OrdinalIgnoreCase))
        {
            var mscPath = Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.System), uri);
            Process.Start(new ProcessStartInfo("mmc.exe", $"\"{mscPath}\"")
            {
                UseShellExecute = true
            });
            return;
        }

        Process.Start(new ProcessStartInfo(uri) { UseShellExecute = true });
    }

    public static void OpenSettings(int? parentPid, string iniPath)
    {
        try
        {
            if (parentPid is int pid && TryPostSettingsCommand(pid))
                return;

            var root = Directory.GetParent(AppContext.BaseDirectory.TrimEnd(Path.DirectorySeparatorChar))?.FullName;
            var settingsExe = root == null ? null : Path.Combine(root, "bin", "WinMacMenuSettings.exe");
            if (settingsExe != null && File.Exists(settingsExe))
            {
                Process.Start(new ProcessStartInfo(settingsExe, $"--config \"{iniPath}\" --from-tray")
                {
                    UseShellExecute = false,
                    CreateNoWindow = true,
                    WorkingDirectory = Path.GetDirectoryName(settingsExe)!
                });
            }
        }
        catch (Exception ex)
        {
            Debug.WriteLine($"Open settings failed: {ex.Message}");
        }
    }

    private static bool TryPostSettingsCommand(int parentPid)
    {
        var owner = IntPtr.Zero;
        EnumWindows((hWnd, _) =>
        {
            GetWindowThreadProcessId(hWnd, out var processId);
            if (processId != (uint)parentPid)
                return true;

            owner = hWnd;
            return false;
        }, IntPtr.Zero);

        return owner != IntPtr.Zero && PostMessage(owner, WM_COMMAND, (IntPtr)10005, IntPtr.Zero);
    }

    public static void ShellOpen(string path, string? args = null)
    {
        if (string.IsNullOrEmpty(path)) return;
        var psi = new ProcessStartInfo(path)
        {
            UseShellExecute = true,
            Arguments = args ?? ""
        };
        try
        {
            Process.Start(psi);
        }
        catch (Exception ex)
        {
            App.LogException("ShellOpen: " + path, ex);
            MessageBox(IntPtr.Zero, "Could not open this item. It may have moved or become unavailable.",
                "WinMac Menu", 0x10);
        }
    }

    public static bool ShowShellContextMenu(string path, IntPtr owner)
    {
        if (string.IsNullOrWhiteSpace(path) || !File.Exists(path) && !Directory.Exists(path))
            return false;

        IntPtr pidl = IntPtr.Zero;
        IShellFolder? parent = null;
        IContextMenu? contextMenu = null;
        var menu = IntPtr.Zero;
        var coInit = CoInitializeEx(IntPtr.Zero, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        var shouldUninitialize = coInit is 0 or 1;

        try
        {
            if (SHParseDisplayName(path, IntPtr.Zero, out pidl, 0, out _) != 0 || pidl == IntPtr.Zero)
                return false;

            var shellFolderIid = IID_IShellFolder;
            if (SHBindToParent(pidl, ref shellFolderIid, out parent, out var child) != 0 || parent == null || child == IntPtr.Zero)
                return false;

            var contextMenuIid = IID_IContextMenu;
            if (parent.GetUIObjectOf(owner, 1, ref child, ref contextMenuIid, IntPtr.Zero, out contextMenu) != 0 ||
                contextMenu == null)
                return false;

            menu = CreatePopupMenu();
            if (menu == IntPtr.Zero)
                return false;

            var flags = CMF_NORMAL;
            if ((GetKeyState(VK_SHIFT) & 0x8000) != 0)
                flags |= CMF_EXTENDEDVERBS;

            if (contextMenu.QueryContextMenu(menu, 0, 1, 0x7FFF, flags) < 0)
                return false;

            if (!GetCursorPos(out var pt))
                return false;

            SetForegroundWindow(owner);
            using var monitorCancel = new CancellationTokenSource();
            var popupThreadId = GetCurrentThreadId();
            var monitor = StartShellContextMenuMonitor(owner, popupThreadId, monitorCancel.Token);
            var cmd = TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RECURSE,
                pt.x, pt.y, owner, IntPtr.Zero);
            monitorCancel.Cancel();
            try { monitor.Wait(250); } catch { }
            PostMessage(owner, WM_NULL, IntPtr.Zero, IntPtr.Zero);

            if (cmd < 1)
                return false;

            var invoke = new CMINVOKECOMMANDINFOEX
            {
                cbSize = Marshal.SizeOf<CMINVOKECOMMANDINFOEX>(),
                fMask = CMIC_MASK_UNICODE,
                hwnd = owner,
                lpVerb = new IntPtr(cmd - 1),
                lpVerbW = new IntPtr(cmd - 1),
                nShow = SW_SHOWNORMAL
            };

            return contextMenu.InvokeCommand(ref invoke) == 0;
        }
        catch
        {
            return false;
        }
        finally
        {
            if (menu != IntPtr.Zero) DestroyMenu(menu);
            if (contextMenu != null) Marshal.ReleaseComObject(contextMenu);
            if (parent != null) Marshal.ReleaseComObject(parent);
            if (pidl != IntPtr.Zero) CoTaskMemFree(pidl);
            if (shouldUninitialize) CoUninitialize();
        }
    }

    private static Task StartShellContextMenuMonitor(IntPtr owner, uint popupThreadId, CancellationToken token)
    {
        return Task.Run(async () =>
        {
            while (!token.IsCancellationRequested)
            {
                var popup = FindThreadPopupMenu(popupThreadId);
                if (popup != IntPtr.Zero)
                {
                    SetWindowPos(popup, HWND_TOPMOST, 0, 0, 0, 0,
                        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
                }

                if (owner != IntPtr.Zero && (!IsWindowVisible(owner) || IsIconic(owner)))
                {
                    PostMessage(owner, WM_CANCELMODE, IntPtr.Zero, IntPtr.Zero);
                    if (popup != IntPtr.Zero)
                        PostMessage(popup, WM_CLOSE, IntPtr.Zero, IntPtr.Zero);
                    break;
                }

                try { await Task.Delay(20, token).ConfigureAwait(false); }
                catch { break; }
            }
        }, token);
    }

    public static void CancelShellContextMenus(IntPtr owner = default)
    {
        if (owner != IntPtr.Zero)
            PostMessage(owner, WM_CANCELMODE, IntPtr.Zero, IntPtr.Zero);

        EnumWindows((hWnd, _) =>
        {
            var className = new System.Text.StringBuilder(64);
            if (GetClassName(hWnd, className, className.Capacity) > 0 &&
                string.Equals(className.ToString(), "#32768", StringComparison.Ordinal))
            {
                PostMessage(hWnd, WM_CLOSE, IntPtr.Zero, IntPtr.Zero);
            }
            return true;
        }, IntPtr.Zero);
    }

    private static IntPtr FindThreadPopupMenu(uint threadId)
    {
        var result = IntPtr.Zero;
        EnumThreadWindows(threadId, (hWnd, _) =>
        {
            var className = new System.Text.StringBuilder(64);
            if (GetClassName(hWnd, className, className.Capacity) > 0 &&
                string.Equals(className.ToString(), "#32768", StringComparison.Ordinal))
            {
                result = hWnd;
                return false;
            }
            return true;
        }, IntPtr.Zero);

        if (result != IntPtr.Zero)
            return result;

        EnumWindows((hWnd, _) =>
        {
            var className = new System.Text.StringBuilder(64);
            if (GetClassName(hWnd, className, className.Capacity) > 0 &&
                string.Equals(className.ToString(), "#32768", StringComparison.Ordinal))
            {
                result = hWnd;
                return false;
            }
            return true;
        }, IntPtr.Zero);

        return result;
    }
    private static void RunCmd(string command)
    {
        var psi = new ProcessStartInfo("cmd.exe")
        {
            UseShellExecute = true,
            Arguments = command
        };
        Process.Start(psi);
    }

    public static List<FolderEntry> GetFolderContents(string folderPath, AppConfig cfg, bool applyMaxItems = true)
    {
        try
        {
            var dir = new DirectoryInfo(folderPath);
            if (!dir.Exists) return new List<FolderEntry>();

            IEnumerable<FileSystemInfo> entries = dir.GetFileSystemInfos();

            if (!cfg.ShowHidden)
                entries = entries.Where(e => !e.Attributes.HasFlag(FileAttributes.Hidden) ||
                                             (cfg.ShowDotfiles && e.Name.StartsWith(".", StringComparison.Ordinal)));

            if (!cfg.ShowDotfiles)
                entries = entries.Where(e => !e.Name.StartsWith(".", StringComparison.Ordinal));

            var result = entries.Select(entry =>
            {
                var name = entry is FileInfo && !cfg.ShowFileExtensions
                    ? Path.GetFileNameWithoutExtension(entry.Name)
                    : entry.Name;

                return new FolderEntry
                {
                    Name = name,
                    FullPath = entry.FullName,
                    IsDirectory = entry is DirectoryInfo
                };
            });

            return SortFolderEntries(result, cfg, applyMaxItems);
        }
        catch { }
        return new List<FolderEntry>();
    }

    public static List<FolderEntry> SortFolderEntries(IEnumerable<FolderEntry> entries, AppConfig cfg, bool applyMaxItems = true)
    {
        var descending = cfg.SortDirection.Equals("descending", StringComparison.OrdinalIgnoreCase);

        IEnumerable<FolderEntry> OrderEntries(IEnumerable<FolderEntry> source) => cfg.SortBy.ToLowerInvariant() switch
        {
            "date_modified" => descending
                ? source.OrderByDescending(GetLastWriteTimeUtc)
                : source.OrderBy(GetLastWriteTimeUtc),
            "date_created" => descending
                ? source.OrderByDescending(GetCreationTimeUtc)
                : source.OrderBy(GetCreationTimeUtc),
            "size" => descending
                ? source.OrderByDescending(GetSize)
                : source.OrderBy(GetSize),
            "type" => descending
                ? source.OrderByDescending(GetExtension, StringComparer.OrdinalIgnoreCase)
                : source.OrderBy(GetExtension, StringComparer.OrdinalIgnoreCase),
            _ => descending
                ? source.OrderByDescending(e => e.Name, StringComparer.OrdinalIgnoreCase)
                : source.OrderBy(e => e.Name, StringComparer.OrdinalIgnoreCase),
        };

        var ordered = cfg.FoldersFirst
            ? OrderEntries(entries.Where(e => e.IsDirectory)).Concat(OrderEntries(entries.Where(e => !e.IsDirectory)))
            : OrderEntries(entries);

        if (applyMaxItems && cfg.MaxItems > 0)
            ordered = ordered.Take(cfg.MaxItems);

        return ordered.ToList();
    }

    private static DateTime GetLastWriteTimeUtc(FolderEntry entry)
    {
        try
        {
            return entry.IsDirectory
                ? Directory.GetLastWriteTimeUtc(entry.FullPath)
                : File.GetLastWriteTimeUtc(entry.FullPath);
        }
        catch { return DateTime.MinValue; }
    }

    private static DateTime GetCreationTimeUtc(FolderEntry entry)
    {
        try
        {
            return entry.IsDirectory
                ? Directory.GetCreationTimeUtc(entry.FullPath)
                : File.GetCreationTimeUtc(entry.FullPath);
        }
        catch { return DateTime.MinValue; }
    }

    private static long GetSize(FolderEntry entry)
    {
        try { return entry.IsDirectory ? 0 : new FileInfo(entry.FullPath).Length; }
        catch { return 0; }
    }

    private static string GetExtension(FolderEntry entry)
        => entry.IsDirectory ? string.Empty : Path.GetExtension(entry.FullPath);

    // ── Win32 for window enumeration ──────────────────────────────────────

    private delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);

    [DllImport("user32.dll")] private static extern bool EnumWindows(EnumWindowsProc lpEnumFunc, IntPtr lParam);
    [DllImport("user32.dll")] private static extern bool EnumChildWindows(IntPtr hWndParent, EnumWindowsProc lpEnumFunc, IntPtr lParam);
    [DllImport("user32.dll")] private static extern bool IsWindowVisible(IntPtr hWnd);
    [DllImport("user32.dll")] private static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint lpdwProcessId);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern int GetWindowText(IntPtr hWnd, System.Text.StringBuilder lpString, int nMaxCount);
    [DllImport("user32.dll")] private static extern int GetWindowTextLength(IntPtr hWnd);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern int GetClassName(IntPtr hWnd, System.Text.StringBuilder lpClassName, int nMaxCount);
    [DllImport("user32.dll", EntryPoint = "GetWindowLongPtrW")] private static extern IntPtr GetWindowLongPtr(IntPtr hWnd, int nIndex);
    [DllImport("user32.dll")] private static extern IntPtr GetWindow(IntPtr hWnd, uint uCmd);
    [DllImport("user32.dll")] private static extern bool GetWindowRect(IntPtr hWnd, out RECT lpRect);
    [DllImport("dwmapi.dll")] private static extern int DwmGetWindowAttribute(IntPtr hwnd, int dwAttribute, out int pvAttribute, int cbAttribute);
    [DllImport("shell32.dll")] private static extern int SHGetPropertyStoreForWindow(IntPtr hwnd, ref Guid riid, out IPropertyStore propertyStore);
    [DllImport("shell32.dll", CharSet = CharSet.Unicode)] private static extern int SHCreateItemInKnownFolder(ref Guid kfid, uint flags, string item, ref Guid riid, out IShellItem shellItem);
    [DllImport("ole32.dll")] private static extern int PropVariantClear(ref PROPVARIANT pvar);

    private const int GWL_EXSTYLE = -20;
    private const long WS_EX_TOOLWINDOW = 0x00000080L;
    private const long WS_EX_APPWINDOW = 0x00040000L;
    private const uint GW_OWNER = 4;
    private const int DWMWA_CLOAKED = 14;
    private const int DWM_CLOAKED_SHELL = 2;
    private const ushort VT_LPWSTR = 31;
    private const uint SIGDN_NORMALDISPLAY = 0;
    private const int S_OK = 0;
    private const int S_FALSE = 1;
    private static readonly Guid IID_IPropertyStore = new("886D8EEB-8CF2-4446-8D02-CDBA1DBDCF99");
    private static readonly Guid IID_IShellItem = new("43826D1E-E718-42EE-BC55-A1E261C37BFE");
    private static readonly Guid FOLDERID_AppsFolder = new("1E87508D-89C2-42F0-8A7E-645A0F50CA58");
    private static readonly PROPERTYKEY PKEY_AppUserModel_ID = new()
    {
        fmtid = new Guid("9F4C2855-9F79-4B39-A8D0-E1D42DE1D5F3"),
        pid = 5
    };

    // Background/hidden Windows system process names — excluded when TaskKillIgnoreSystem=true
    private static readonly HashSet<string> SystemProcessNames = new(StringComparer.OrdinalIgnoreCase)
    {
        "system", "registry", "smss", "csrss", "wininit", "services", "lsass", "lsaiso",
        "svchost", "dwm", "conhost", "winlogon", "fontdrvhost", "sihost", "taskhostw",
        "searchindexer", "searchhost", "spoolsv", "audiodg", "ctfmon", "dllhost",
        "securityhealthservice", "msmpeng", "nisSrv", "sgrmbroker", "wudfhost",
        "wmiprvse", "runtimebroker", "backgroundtaskhost", "applicationframehost",
        "shellexperiencehost", "startmenuexperiencehost", "textinputhost",
        "systemsettings", "lockapp", "logonui", "userinit",
    };

    public static List<ProcessEntry> GetRunningProcesses(AppConfig cfg)
    {
        var coInit = CoInitializeEx(IntPtr.Zero, COINIT_APARTMENTTHREADED);
        var shouldUninitialize = coInit is S_OK or S_FALSE;
        try
        {
            var excludes = cfg.TaskKillExcludes
                .Split(new[] { ',', ';' }, StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries)
                .ToHashSet(StringComparer.OrdinalIgnoreCase);

            return cfg.TaskKillListWindows
                ? EnumerateWindows(cfg, excludes)
                : EnumerateProcessesWithWindows(cfg, excludes);
        }
        finally
        {
            if (shouldUninitialize) CoUninitialize();
        }
    }

    // TaskKillListWindows=false (default): one entry per process that owns a visible window
    private static List<ProcessEntry> EnumerateProcessesWithWindows(AppConfig cfg, HashSet<string> excludes)
    {
        var seen    = new HashSet<int>();   // deduplicate by PID
        var result  = new List<ProcessEntry>();

        EnumWindows((hWnd, _) =>
        {
            if (result.Count >= cfg.TaskKillMax) return false;
            if (!IsRealUserWindow(hWnd, cfg.TaskKillAllDesktops)) return true;

            GetWindowThreadProcessId(hWnd, out var pid);
            if ((int)pid == Environment.ProcessId) return true;

            try
            {
                using var proc = Process.GetProcessById((int)pid);
                var name = proc.ProcessName;
                var appUserModelId = GetBestWindowAppUserModelId(hWnd);
                if (cfg.TaskKillIgnoreSystem && SystemProcessNames.Contains(name)) return true;
                var exePath = TryGetProcessPath(proc);
                if (IsExcludedProcess(excludes, name, exePath)) return true;
                var displayName = GetProcessDisplayName(name, exePath, appUserModelId);
                var useWindowIdentity = IsHostedTaskProcess(name, exePath, displayName);

                if (!useWindowIdentity)
                {
                    if (seen.Contains((int)pid)) return true;
                    seen.Add((int)pid);
                }

                var itemName = displayName;
                var forceWindowIdentity = EndsWithExe(itemName);
                if (useWindowIdentity || forceWindowIdentity)
                {
                    var title = GetBestWindowTitle(hWnd, appUserModelId);
                    if (!string.IsNullOrWhiteSpace(title))
                        itemName = title;
                }
                itemName = NormalizeTaskKillDisplayName(itemName);
                if (forceWindowIdentity)
                    useWindowIdentity = true;

                result.Add(new ProcessEntry
                {
                    Name = itemName,
                    Pid = (int)pid,
                    Hwnd = hWnd,
                    ExePath = exePath,
                    AppUserModelId = appUserModelId,
                    PreferWindowIdentity = useWindowIdentity
                });
            }
            catch { }

            return true;
        }, IntPtr.Zero);

        return result.OrderBy(p => p.Name, StringComparer.OrdinalIgnoreCase).ToList();
    }

    // TaskKillListWindows=true: one entry per visible window (by title), may repeat same process
    private static List<ProcessEntry> EnumerateWindows(AppConfig cfg, HashSet<string> excludes)
    {
        var result = new List<ProcessEntry>();

        EnumWindows((hWnd, _) =>
        {
            if (result.Count >= cfg.TaskKillMax) return false;
            if (!IsRealUserWindow(hWnd, cfg.TaskKillAllDesktops)) return true;

            var appUserModelId = GetBestWindowAppUserModelId(hWnd);
            var title = GetBestWindowTitle(hWnd, appUserModelId);
            if (string.IsNullOrWhiteSpace(title)) return true;
            title = NormalizeTaskKillDisplayName(title);

            GetWindowThreadProcessId(hWnd, out var pid);
            if ((int)pid == Environment.ProcessId) return true;

            try
            {
                using var proc = Process.GetProcessById((int)pid);
                var name = proc.ProcessName;
                if (cfg.TaskKillIgnoreSystem && SystemProcessNames.Contains(name)) return true;
                var exePath = TryGetProcessPath(proc);
                if (IsExcludedProcess(excludes, name, exePath)) return true;

                result.Add(new ProcessEntry
                {
                    Name = title,
                    Pid = (int)pid,
                    Hwnd = hWnd,
                    ExePath = exePath,
                    AppUserModelId = appUserModelId
                });
            }
            catch { }

            return true;
        }, IntPtr.Zero);

        return result;
    }

    private static string? TryGetProcessPath(Process proc)
    {
        try { return proc.MainModule?.FileName; }
        catch { return null; }
    }

    private static bool IsExcludedProcess(HashSet<string> excludes, string processName, string? exePath)
    {
        if (excludes.Contains(processName) || excludes.Contains(processName + ".exe")) return true;

        var executableName = string.IsNullOrWhiteSpace(exePath) ? "" : Path.GetFileName(exePath);
        return !string.IsNullOrWhiteSpace(executableName) &&
               excludes.Contains(executableName);
    }

    private static string GetProcessDisplayName(string processName, string? exePath, string? appUserModelId)
    {
        var appName = GetAppUserModelDisplayName(appUserModelId);
        if (!string.IsNullOrWhiteSpace(appName))
            return appName;

        if (!string.IsNullOrWhiteSpace(exePath))
        {
            var exeName = Path.GetFileNameWithoutExtension(exePath);
            if (string.Equals(exeName, "mspaint", StringComparison.OrdinalIgnoreCase))
                return "Paint";
            if (string.Equals(exeName, "notepad", StringComparison.OrdinalIgnoreCase))
                return "Notepad";
            if (string.Equals(exeName, "SnippingTool", StringComparison.OrdinalIgnoreCase))
                return "Snipping Tool";
            if (string.Equals(exeName, "PowerToys.Settings", StringComparison.OrdinalIgnoreCase))
                return "PowerToys";

            try
            {
                var description = FileVersionInfo.GetVersionInfo(exePath).FileDescription;
                if (!string.IsNullOrWhiteSpace(description))
                    return description;
            }
            catch { }

            if (!string.IsNullOrWhiteSpace(exeName))
                return exeName;
        }

        return processName;
    }

    private static bool IsHostedTaskProcess(string processName, string? exePath, string displayName)
    {
        var exeName = Path.GetFileName(exePath);
        return string.Equals(processName, "ApplicationFrameHost", StringComparison.OrdinalIgnoreCase) ||
               string.Equals(processName, "svchost", StringComparison.OrdinalIgnoreCase) ||
               string.Equals(exeName, "ApplicationFrameHost.exe", StringComparison.OrdinalIgnoreCase) ||
               string.Equals(exeName, "svchost.exe", StringComparison.OrdinalIgnoreCase) ||
               string.Equals(displayName, "Application Frame Host", StringComparison.OrdinalIgnoreCase) ||
               string.Equals(displayName, "Host Process for Windows Services", StringComparison.OrdinalIgnoreCase);
    }

    private static bool EndsWithExe(string? value)
        => !string.IsNullOrWhiteSpace(value) &&
           value.Trim().EndsWith(".exe", StringComparison.OrdinalIgnoreCase);

    private static string NormalizeTaskKillDisplayName(string name)
    {
        return string.Equals(name, "Winver UWP", StringComparison.OrdinalIgnoreCase)
            ? "About Windows"
            : name;
    }

    private static string GetBestWindowTitle(IntPtr hWnd, string? appUserModelId)
    {
        var title = GetWindowTitle(hWnd);
        var className = GetWindowClass(hWnd);
        if (string.Equals(className, "ApplicationFrameWindow", StringComparison.OrdinalIgnoreCase))
        {
            var childTitle = FindChildWindowTitle(hWnd);
            if (!string.IsNullOrWhiteSpace(childTitle))
                title = childTitle;
        }

        var appName = GetAppUserModelDisplayName(appUserModelId);
        if (string.IsNullOrWhiteSpace(title))
            return appName ?? "";

        if (!string.IsNullOrWhiteSpace(appName) &&
            (string.Equals(title, className, StringComparison.OrdinalIgnoreCase) ||
             string.Equals(title, "ApplicationFrameHost", StringComparison.OrdinalIgnoreCase) ||
             string.Equals(title, "ApplicationFrameWindow", StringComparison.OrdinalIgnoreCase)))
            return appName;

        return title;
    }

    private static string GetWindowTitle(IntPtr hWnd)
    {
        var len = GetWindowTextLength(hWnd);
        if (len <= 0) return "";
        var sb = new System.Text.StringBuilder(len + 1);
        GetWindowText(hWnd, sb, sb.Capacity);
        return sb.ToString().Trim();
    }

    private static string GetWindowClass(IntPtr hWnd)
    {
        var sb = new System.Text.StringBuilder(256);
        return GetClassName(hWnd, sb, sb.Capacity) > 0 ? sb.ToString() : "";
    }

    private static string FindChildWindowTitle(IntPtr parent)
    {
        var best = "";
        EnumChildWindows(parent, (child, _) =>
        {
            var title = GetWindowTitle(child);
            if (!string.IsNullOrWhiteSpace(title))
            {
                best = title;
                return false;
            }
            return true;
        }, IntPtr.Zero);
        return best;
    }

    private static string? GetBestWindowAppUserModelId(IntPtr hWnd)
    {
        var appUserModelId = GetWindowAppUserModelId(hWnd);
        if (!string.IsNullOrWhiteSpace(appUserModelId))
            return appUserModelId;

        EnumChildWindows(hWnd, (child, _) =>
        {
            appUserModelId = GetWindowAppUserModelId(child);
            return string.IsNullOrWhiteSpace(appUserModelId);
        }, IntPtr.Zero);

        return appUserModelId;
    }

    private static string? GetWindowAppUserModelId(IntPtr hWnd)
    {
        IPropertyStore? store = null;
        try
        {
            var propertyStoreIid = IID_IPropertyStore;
            if (SHGetPropertyStoreForWindow(hWnd, ref propertyStoreIid, out store) != 0 || store == null)
                return null;

            var key = PKEY_AppUserModel_ID;
            if (store.GetValue(ref key, out var value) != 0)
                return null;

            try
            {
                if (value.vt == VT_LPWSTR && value.pointerValue != IntPtr.Zero)
                    return Marshal.PtrToStringUni(value.pointerValue);
                return null;
            }
            finally { PropVariantClear(ref value); }
        }
        catch { return null; }
        finally
        {
            if (store != null) Marshal.ReleaseComObject(store);
        }
    }

    private static string? GetAppUserModelDisplayName(string? appUserModelId)
    {
        if (string.IsNullOrWhiteSpace(appUserModelId)) return null;
        IShellItem? shellItem = null;
        try
        {
            var folder = FOLDERID_AppsFolder;
            var iid = IID_IShellItem;
            if (SHCreateItemInKnownFolder(ref folder, 0, appUserModelId, ref iid, out shellItem) != 0 || shellItem == null)
                return null;

            if (shellItem.GetDisplayName(SIGDN_NORMALDISPLAY, out var namePtr) != 0 || namePtr == IntPtr.Zero)
                return null;

            try { return Marshal.PtrToStringUni(namePtr); }
            finally { CoTaskMemFree(namePtr); }
        }
        catch { return null; }
        finally
        {
            if (shellItem != null) Marshal.ReleaseComObject(shellItem);
        }
    }

    private static bool IsRealUserWindow(IntPtr hWnd, bool allowCloakedShell)
    {
        if (!IsWindowVisible(hWnd)) return false;

        var exStyle = GetWindowLongPtr(hWnd, GWL_EXSTYLE).ToInt64();
        if ((exStyle & WS_EX_TOOLWINDOW) != 0 && (exStyle & WS_EX_APPWINDOW) == 0) return false;

        var owner = GetWindow(hWnd, GW_OWNER);
        if (owner != IntPtr.Zero && IsWindowVisible(owner) && (exStyle & WS_EX_APPWINDOW) == 0) return false;

        var className = GetWindowClass(hWnd);
        if (string.Equals(className, "Shell_TrayWnd", StringComparison.OrdinalIgnoreCase) ||
            string.Equals(className, "Progman", StringComparison.OrdinalIgnoreCase)) return false;

        if (GetWindowTextLength(hWnd) <= 0 ||
            string.Equals(GetWindowTitle(hWnd), "Program Manager", StringComparison.Ordinal)) return false;

        if (DwmGetWindowAttribute(hWnd, DWMWA_CLOAKED, out var cloaked, sizeof(int)) >= 0 &&
            cloaked != 0 && !(allowCloakedShell && cloaked == DWM_CLOAKED_SHELL)) return false;

        if (GetWindowRect(hWnd, out var rect) &&
            (rect.right - rect.left <= 1 || rect.bottom - rect.top <= 1)) return false;

        return true;
    }

    public static void KillProcess(int pid)
    {
        try { Process.GetProcessById(pid)?.Kill(); } catch { }
    }
}

public record FolderEntry
{
    public string Name { get; init; } = "";
    public string FullPath { get; init; } = "";
    public bool IsDirectory { get; init; }
}

public record ProcessEntry
{
    public string Name { get; init; } = "";
    public int Pid { get; init; }
    public IntPtr Hwnd { get; init; }
    public string? ExePath { get; init; }
    public string? AppUserModelId { get; init; }
    public bool PreferWindowIdentity { get; init; }
}

[StructLayout(LayoutKind.Sequential)]
internal struct PROPERTYKEY
{
    public Guid fmtid;
    public uint pid;
}

[StructLayout(LayoutKind.Explicit)]
internal struct PROPVARIANT
{
    [FieldOffset(0)] public ushort vt;
    [FieldOffset(8)] public IntPtr pointerValue;
}

[StructLayout(LayoutKind.Sequential)]
internal struct POINT
{
    public int x;
    public int y;
}

[StructLayout(LayoutKind.Sequential)]
internal struct RECT
{
    public int left;
    public int top;
    public int right;
    public int bottom;
}

[StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
internal struct CMINVOKECOMMANDINFOEX
{
    public int cbSize;
    public uint fMask;
    public IntPtr hwnd;
    public IntPtr lpVerb;
    public string? lpParameters;
    public string? lpDirectory;
    public int nShow;
    public uint dwHotKey;
    public IntPtr hIcon;
    public string? lpTitle;
    public IntPtr lpVerbW;
    public string? lpParametersW;
    public string? lpDirectoryW;
    public string? lpTitleW;
    public POINT ptInvoke;
}

[ComImport, Guid("886D8EEB-8CF2-4446-8D02-CDBA1DBDCF99"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
internal interface IPropertyStore
{
    [PreserveSig] int GetCount(out uint propertyCount);
    [PreserveSig] int GetAt(uint propertyIndex, out PROPERTYKEY key);
    [PreserveSig] int GetValue(ref PROPERTYKEY key, out PROPVARIANT value);
    [PreserveSig] int SetValue(ref PROPERTYKEY key, ref PROPVARIANT value);
    [PreserveSig] int Commit();
}

[ComImport, Guid("43826D1E-E718-42EE-BC55-A1E261C37BFE"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
internal interface IShellItem
{
    [PreserveSig] int BindToHandler(IntPtr pbc, ref Guid bhid, ref Guid riid, out IntPtr ppv);
    [PreserveSig] int GetParent(out IShellItem ppsi);
    [PreserveSig] int GetDisplayName(uint sigdnName, out IntPtr ppszName);
    [PreserveSig] int GetAttributes(uint sfgaoMask, out uint psfgaoAttribs);
    [PreserveSig] int Compare(IShellItem psi, uint hint, out int piOrder);
}

[ComImport, Guid("000214E6-0000-0000-C000-000000000046"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
internal interface IShellFolder
{
    [PreserveSig] int ParseDisplayName(IntPtr hwnd, IntPtr pbc, string pszDisplayName, out uint pchEaten, out IntPtr ppidl, ref uint pdwAttributes);
    [PreserveSig] int EnumObjects(IntPtr hwnd, uint grfFlags, out IntPtr ppenumIDList);
    [PreserveSig] int BindToObject(IntPtr pidl, IntPtr pbc, ref Guid riid, out IntPtr ppv);
    [PreserveSig] int BindToStorage(IntPtr pidl, IntPtr pbc, ref Guid riid, out IntPtr ppv);
    [PreserveSig] int CompareIDs(IntPtr lParam, IntPtr pidl1, IntPtr pidl2);
    [PreserveSig] int CreateViewObject(IntPtr hwndOwner, ref Guid riid, out IntPtr ppv);
    [PreserveSig] int GetAttributesOf(uint cidl, ref IntPtr apidl, ref uint rgfInOut);
    [PreserveSig] int GetUIObjectOf(IntPtr hwndOwner, uint cidl, ref IntPtr apidl, ref Guid riid, IntPtr rgfReserved, out IContextMenu ppv);
    [PreserveSig] int GetDisplayNameOf(IntPtr pidl, uint uFlags, out IntPtr pName);
    [PreserveSig] int SetNameOf(IntPtr hwnd, IntPtr pidl, string pszName, uint uFlags, out IntPtr ppidlOut);
}

[ComImport, Guid("000214E4-0000-0000-C000-000000000046"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
internal interface IContextMenu
{
    [PreserveSig] int QueryContextMenu(IntPtr hmenu, uint indexMenu, uint idCmdFirst, uint idCmdLast, uint uFlags);
    [PreserveSig] int InvokeCommand(ref CMINVOKECOMMANDINFOEX pici);
    [PreserveSig] int GetCommandString(UIntPtr idCmd, uint uType, IntPtr pReserved, IntPtr pszName, uint cchMax);
}
