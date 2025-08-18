# WinMac Menu

A tiny, fast Win32 application that shows a Windows context-like popup menu. It’s configurable via an INI file, adapts to your light/dark theme, supports a dynamic Recent Items submenu, and can render in either native (legacy) or a modern owner‑draw style.

![WinMacMenu screenshot](assets/winmacmenu-demo.png)

- Windows 10/11, x86 and ARM64 supported
- No installer, single EXE
- Low-latency popup that exits after the menu closes

## Features

- Recent Items: dynamic submenu from %AppData%\Microsoft\Windows\Recent
- Config-driven items with separators, folders as submenus, URIs, commands, and power actions
- Light/Dark auto-adaptation; immersive dark hint on the invisible owner window
- Styles: Legacy (native popup) or Modern (owner-draw, Win11-like)
- Icons per item (+ default icon); optional icons in legacy style without changing native look
- Placement controls (edges or cursor, with offsets), per‑monitor DPI aware
- First-letter activation, outside-click dismissal, process exits after use
- Folder submenu behaviors: lazy population, max depth, name-only items, single vs double click to open
- Per-config single-instance (separate instance per INI path)

## Run

- Double‑click the EXE to show the menu.
- The menu closes on selection or outside click and the process exits.
- Use --config to point at a custom INI; each INI path gets its own single‑instance gate.

Examples:
- WinMacMenu.exe --config C:\Tools\menu.ini

## Configuration

WinMacMenu reads an INI. If missing, a default is created.

### Sections
- [General] global behavior and style
- [Placement] position rules
- [Menu] menu items Item1..ItemN
- [Icons] per-item icon mapping Icon1..IconN
- [Modern] modern-only visual tweaks

### Notes
- Environment variables expand in labels, paths, params, and icon paths (e.g., %USERNAME%).
- Indices N in [Icons] map to ItemN in [Menu].
- MenuWidth and [Modern] options apply to Modern style only; Legacy keeps native look.

#### [General]

- MenuStyle = legacy | modern
- DefaultIcon = path\to\default.ico (optional)
- LegacyIcons = 0 | 1  (show icons in legacy style via MIIM_BITMAP)
- RecentMax = 10       (max items in Recent submenu)
- FolderMaxDepth = 2   (1..4 for nested folder submenus)
- FolderSubmenuOpen = single | double (open folder itself on single/double click)
- ShowHidden = 0 | 1   (show FILE_ATTRIBUTE_HIDDEN files)
- ShowDotfiles = 0 | 1 (show entries whose name starts with '.')
- PointerRelative = 0 | 1 (place menu near cursor instead of edges)
- MenuWidth = 226..255 (0 or omit = auto; Modern only, DPI‑scaled)
- Corners = rounded | square (Modern only; selection pill corners)

#### [Placement]

Used when PointerRelative = 0.

- HPlacement = Left | Center | Right
- HOffset   = integer pixels from left/right when not centered
- VPlacement = Top | Center | Bottom
- VOffset   = integer pixels from top/bottom when not centered

#### [Menu]

Define items Item1..ItemN as pipe-separated fields:

ItemN = Label | TYPE | PathOrTarget | Params | IconPath

- Label: text shown in the menu (env vars expand)
- TYPE: one of
  - URI              (ms-settings:, shell:, http:, etc.)
  - FILE             (launch a file or executable)
  - CMD              (run a command line; Params appended)
  - FOLDER           (open folder directly)
  - FOLDER_SUBMENU   (show folder contents as submenu)
  - POWER_SLEEP
  - POWER_SHUTDOWN
  - POWER_RESTART
  - POWER_LOCK
  - POWER_LOGOFF
  - RECENT_SUBMENU   (inserts the dynamic Recent items submenu)
- PathOrTarget: meaning depends on TYPE; for URI use the full URI; for FOLDER[_SUBMENU] use the folder path
- Params: optional command-line parameters for FILE/CMD (ignored otherwise)
- IconPath: optional per-item icon (.ico recommended)

You can also use a value in [Icons] to assign an icon to ItemN: IconN = path\to\icon.ico

Separators: use TYPE = SEPARATOR, or leave Label empty with a valid TYPE/Path; the parser also accepts explicit CI_SEPARATOR internally.

#### [Icons]

- Icon1 = C:\path\to\apps.ico
- Icon2 = C:\path\to\settings.ico
- ... (maps 1:1 to ItemN)

If an item has no IconPath and there is no IconN mapping, DefaultIcon (if set) is used.

#### [Modern]

Modern style only:
- MenuWidth = 226..255
- Corners = rounded | square

## Examples

### Minimal

[General]  
MenuStyle=modern  
RecentMax=12  
PointerRelative=1  
MenuWidth=240

[Menu]  
Item1=Settings|URI|ms-settings:  
Item2=File Explorer|FILE|explorer.exe||%SystemRoot%\\System32\\imageres.dll  
Item3=Recent|RECENT_SUBMENU  
Item4=Sleep|POWER_SLEEP  
Item5=Restart|POWER_RESTART  

### Folder submenu with visibility and click behavior

[General]  
MenuStyle=modern  
FolderMaxDepth=2  
FolderSubmenuOpen=single  
ShowHidden=0  
ShowDotfiles=0  

[Menu]  
Item1=Downloads|FOLDER_SUBMENU|%USERPROFILE%\\Downloads  
Item2=Documents|FOLDER_SUBMENU|%USERPROFILE%\\Documents  
Item3=Open Pictures|FOLDER|%USERPROFILE%\\Pictures  

### Placement (edge-based)

[General]  
PointerRelative=0  

[Placement]  
HPlacement=Right  
HOffset=16  
VPlacement=Bottom  
VOffset=24  

## Behavior details

- First-letter activation: typing a letter activates the first matching item immediately.
- Outside click: clicking away dismisses the menu.
- DPI/scaling: sizes and MenuWidth are DPI-scaled in Modern style.
- Legacy vs Modern: Legacy uses the system popup renderer; Modern uses owner-draw for a Win11-like look. Legacy ignores MenuWidth and Modern-only options.
- Icons in Legacy: if LegacyIcons=1, icons attach via MIIM_BITMAP without changing native look.
- Recent: resolves .lnk targets under the Recent folder; missing targets are skipped.
- Power actions: Shutdown/Restart attempt to enable the shutdown privilege. Sleep uses SetSuspendState; may be blocked by policy.
- Single-instance: one instance per INI path (mutex includes the INI path hash). Launching again forwards the request.

## Troubleshooting

- Width doesn’t change in Legacy: by design; only Modern honors MenuWidth.
- Icons look blurry: use 16x16 .ico where possible. Large PNGs in .ico may scale poorly.
- Folder submenus are slow: contents load lazily on first open; deep folders or network paths can still be slow — reduce FolderMaxDepth.

## Notes

- Built with standard Win32 APIs: user32, shell32, shlwapi, comctl32, uxtheme, dwmapi, powrprof, advapi32.
- This app uses normal popup menus; exact visual parity with the system Win+X menu isn’t guaranteed.
- It's recommended to use it together with Open-Shell, so the WinMacMenu can be triggered by clicking the Start menu button with the left mouse button or by pressing the Windows key.
- You can also pin shortcuts to taskbar, or add to Links toolbar. Each shortcut can refer to different config.ini files.

> [!NOTE]
> **Please be informed that this is a beta version - you're using it at your own risk!**

## Future plans

- Update modern style to look like Windows 11 context menu
- Sub-menus sorting options
- Different depths level for specific folders
- Always hide specific file by the name/extension for every folder submenus
- Possibility to open WinMac menu with right clicking Start button using Open-Shell ([#2286](https://github.com/Open-Shell/Open-Shell-Menu/issues/2286))