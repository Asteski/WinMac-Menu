# WinMac Menu

A tiny Win32 application that shows a Windows context-like popup menu. It’s configurable via an INI file, adapts to your light/dark theme, , allows to specify relative and absolute positioning, supports a dynamic Recent Items submenu, shell objects, folder submenus, power options menu, showing folder content in root menu, icons, and many more!

![WinMacMenu screenshot](assets/winmacmenu-demo-11.png)
![WinMacMenu screenshot](assets/winmacmenu-demo-10.png)

## Features

- Windows 10/11, x64 and ARM64
- No installer, single EXE
- Low-latency popup that exits after the menu closes

- Recent Items: dynamic submenu from %AppData%\Microsoft\Windows\Recent
- Config-driven items with separators, folders as submenus, URIs, commands, power actions, and a consolidated Power menu (POWER_MENU)
- Light/Dark auto-adaptation; immersive dark hint on the invisible owner window
- Icons: per-item icons, theme-aware overrides (light/dark), optional DefaultIcon + theme variants, and optional system folder icon retrieval
- Placement controls (edges, center, or cursor, with offsets + ignore options), per‑monitor DPI aware
- First-letter activation, outside-click dismissal
- Folder submenu behaviors: lazy population, max depth, name-only items, optional “Open <folder>” entry
- Inline folder expansion (inject a folder’s contents directly into the root menu) with optional clickable header
- Granular extension hiding (global + recent-only override)

## Run

**Quick Start**: Double‑click the EXE. By default, it runs in background mode with a system tray icon.

**Modes**:
- **Background Mode (default)**: App stays running in background. Launch again to toggle/show menu.
- **One-shot**: Set `RunInBackground=false` in config to exit after menu closes (legacy behavior).
  - To start silently in background (without showing the menu on first launch), set `ShowOnLaunch=false`.

**Custom config**: Use `--config <path>` to point at a custom INI (single instance per INI path applies).

**Tray Icon**: Right-click for settings menu including:
- Show menu / Hide tray / Elevate (run as admin)
- Start on login toggle
- Show/Hide menu icons (toggles whether icons appear in the popup menu)
- Settings (opens config.ini) / Help / About
- Exit

## Security & Privacy

- No telemetry. The app makes no network connections and collects no data.
- Registry usage: optional HKCU\Software\Microsoft\Windows\CurrentVersion\Run entry when you enable “Start on login” from the tray. It’s off by default and only changed when you toggle it. There is no service or scheduled task.
- Keyboard/mouse hooks: installed only while the popup menu is visible to allow outside‑click/escape dismissal; they are immediately removed when the menu closes.
- Recent cleanup: choosing “Clear Recent Items list” deletes shortcut (.lnk) files from your user Recent folder. It doesn’t touch actual documents or programs. There’s no confirmation.
- Filesystem: reads your config.ini and enumerates folders you explicitly reference in the menu.

## Configuration

WinMacMenu reads an INI. If missing, a default is created.

Sections
- [General] global behavior and style
- [Placement] position rules
- [Menu] menu items Item1..ItemN
- [Icons] per-item icon mapping Icon1..IconN and optional DefaultIcon/DefaultIconLight/DefaultIconDark
- [IconsLight] theme-specific per-item icons for light theme (Icon1..IconN)
- [IconsDark] theme-specific per-item icons for dark theme (Icon1..IconN)

Notes
- Environment variables expand in labels, paths, params, and icon paths (e.g., %USERNAME%).
- Indices N in [Icons]/[IconsLight]/[IconsDark] map to ItemN in [Menu].
- Generated default INI contains no comments (to keep the file minimal). Comments are still supported by the parser if you add them manually: lines beginning with `;` or `#` are ignored.
- Duplicate keys: The last occurrence in a section wins (standard Win32 profile API behavior).
- Unknown keys are ignored.

### Quick Reference (All Keys)

| Section | Key | Values | Default | Notes / Synonyms / Deprecated |
|---------|-----|--------|---------|--------------------------------|
| General | RecentMax | integer (1..64) | 12 | Caps recent submenu items |
| General | RunInBackground | true/false | true | Stay running in background; false = exit after menu closes |
| General | ShowOnLaunch | true/false | true | When in background mode, show menu on first launch (set false to start silently) |
| General | ShowTrayIcon | true/false | true | Show system tray icon when running in background |
| General | StartOnLogin | true/false | false | Add to Windows startup (requires elevation to change) |
| General | FolderSubmenuDepth | 1..4 | 4 | Max recursive depth for folder submenus |
| General | FolderSubmenuOpen | single \| double | single | single = activate on first click; double = require second click (omitted from generated INI unless changed) |
| General | FolderShowOpenEntry | true/false | true | Adds "Open <folder>" entry at top when single-click open enabled |
| General | MenuStyle | legacy | legacy | modern hidden unless compiled with ENABLE_MODERN_STYLE |
| Icons | DefaultIcon | path or module,index | (empty) | Fallback icon when no per-item icon is set; env vars expand |
| Icons | DefaultIconLight | path or module,index | (empty) | Optional default icon for light theme |
| Icons | DefaultIconDark | path or module,index | (empty) | Optional default icon for dark theme |
| General | ShowIcons | true/false | true | Toggle available from tray menu; LegacyIcons (deprecated) still accepted |
| General | ShowFolderIcons | true/false | false | Uses system small folder icon (unless per-item icon) |
| General | ShowFileExtensions | true/false | true | Back-compat: ShowExtensions; HideExtensions (deprecated inverse) overrides if present |
| General | RecentShowExtensions | true/false | true | RecentHideExtensions (deprecated inverse) overrides if present |
| General | RecentShowCleanItems | true/false | true | Adds clear command to recent submenu |
| General | ShowHidden | true/false | false | Hidden attribute files |
| General | ShowDotfiles | false\|true\|filesonly\|foldersonly | false | true=both; synonyms: files-only/folders-only |
| General | RecentLabel | fullpath\|name | fullpath | name synonyms: filename,file,leaf |
| General | LogConfig | off\|basic\|verbose \| 0/1/2/true/false | off | basic=true=1, verbose=2 |
| General | LogFolder | path | (exe dir) | Dynamic file naming (see logging) |
| General | MenuWidth | 226..255 | 0 | Modern style only (ignored otherwise) |
| General | Corners | rounded\|square | (n/a) | Modern style only (not written by default) |
| Placement | PointerRelative | true/false | true | Toggle pointer anchoring |
| Placement | Horizontal | left\|center\|right | right | Along working area |
| Placement | HOffset | integer | 0 | Pixels; negative flips semantics |
| Placement | Vertical | top\|center\|bottom | bottom | Along working area |
| Placement | VOffset | integer | 0 | Pixels; negative flips semantics |
| Placement | IgnoreOffsetWhenCentered | false\|true\|hoffset\|voffset | false | When an axis is centered, ignore its offset (true=both) |
| Placement | IgnoreOffsetWhenRelative | false\|true\|hoffset\|voffset | false | When PointerRelative=1, ignore offsets per axis (true=both) |
| Menu | ItemN | see Menu format | (none) | Up to 64 entries |
| Icons | IconN | path | (none) | Maps to ItemN if item lacks explicit icon |
| Debug | LogConfig | (same as General) | (fallback) | Used only if not set in General |
| Debug | LogFolder | path | (fallback) | Used only if not set in General |

### Comment Syntax
The generated default INI is comment-free. If you add comments manually, use `;` or `#` at the start of a line. Inline comments (end‑of‑line) are not supported.

### [General]

Key | Description
----|------------
MenuStyle | `legacy` (modern hidden unless compiled with ENABLE_MODERN_STYLE)
DefaultIcon | Optional path to a .ico used when an item has no explicit icon and (for folders) system folder icon isn’t used
ShowIcons | `0|1` (renamed from LegacyIcons; LegacyIcons still accepted for backward compatibility)
ShowFileExtensions | `0|1` (default 1). When 0, file extensions are hidden in folder listings, inline expansions, and recent items (filename mode). Back-compat: `ShowExtensions`; deprecated `HideExtensions` still honored and inverts this value.
RecentShowExtensions | `0|1` (default 1). When 0, extensions are hidden in the Recent submenu (filename mode) regardless of ShowExtensions. Deprecated `RecentHideExtensions` still honored and inverts this value.
RecentShowCleanItems | `0|1` (default 1). When 1 adds a separator + "Clear Recent Items" entry at the bottom of the Recent submenu that deletes all .lnk entries from the system Recent folder.
ShowFolderIcons | `0|1` when true uses the system small folder icon for folder items/submenus instead of DefaultIcon unless a per-item icon is set
RecentMax | Maximum recent entries (default 12)
RunInBackground | `true|false` (default true). When true, app stays running in background with message loop for instant menu access. When false, app exits after menu closes (legacy behavior).
ShowTrayIcon | `true|false` (default true). When true and running in background, shows system tray icon with right-click context menu for settings and controls.
StartOnLogin | `true|false` (default false). When true, adds app to Windows startup via registry Run entry. Requires elevation to change this setting.
FolderSubmenuDepth | Max nested folder submenu depth (1–4)
FolderSubmenuOpen | `single|double` click depth-1 submenu folders to open (default single)
FolderShowOpenEntry | `true|false` show an “Open <folder>” top entry inside folder submenus when single-click open is active
ShowHidden | Show items with Hidden attribute
ShowDotfiles | `false|true|filesonly|foldersonly` extended dotfile visibility (dot overrides hidden filter for those names)
RecentLabel | `fullpath|name` controls label style for recent items (name aliases: filename, file, leaf)
PointerRelative | `0|1` position near cursor instead of configured edges
LogConfig | `off|0|false`, `basic|1|true`, `verbose|2` – logging level (can reside in [General] or [Debug])
LogFolder | Optional folder path (env vars expand) where a dynamic log file will be created. If omitted, the executable directory is used.

(Width / rounded corner settings for a modern style are intentionally omitted unless modern build is enabled.)

### Extension Visibility Details
### Logging Details
- Lookup order for `LogConfig` and `LogFolder`: `[General]` then `[Debug]` (first non-empty wins).
- Levels:
  - off / 0 / false: no output
  - basic / 1 / true: one config summary block to debugger (OutputDebugString) and to the log file if logging enabled
  - verbose / 2 : basic summary plus a per-item listing (Type, Label, Path, Icon, Params)
- Dynamic log file name pattern: `WinMacMenu_<configBase>_<yyMMdd-HHmm>.log` where `<configBase>` is the INI filename without extension. Timestamp uses local time when config loads (first run of process).
- Location: if `LogFolder` is set its expanded path is used (folder auto-created best-effort). Otherwise the executable directory.
- Encoding: UTF-8 (no BOM) with newline per entry.
- Legacy: `LogConfig=true` continues to map to basic. The former `LogFile` key is deprecated and replaced by `LogFolder` + dynamic naming (direct absolute filenames can be simulated by setting a dedicated empty folder path).
- Dot-prefixed files (like `.gitignore`) are not extension-stripped (mirrors Explorer convention).
- Keys now use positive logic: ShowFileExtensions / RecentShowExtensions (older HideExtensions / RecentHideExtensions still parsed and invert).
- Precedence for recent items when `RecentLabel=name`:
  1. RecentShowExtensions=0 (or legacy RecentHideExtensions=1) → hide extension
  2. Else ShowFileExtensions=0 (or legacy HideExtensions=1) → hide extension
  3. Else extension shown
- Folder listings & inline expansions ignore the recent-specific key and use only ShowFileExtensions (or legacy HideExtensions inversion).

### Icon Precedence
For each menu item / popup root (theme-aware):
1. Explicit per-item theme override: [IconsDark]/[IconsLight] (current theme wins)
2. Else explicit per-item generic: fifth field on ItemN or [Icons] mapping
3. Folders only, when ShowFolderIcons=1: use system folder icon (overrides defaults)
4. Else default icon theme override: DefaultIconDark/DefaultIconLight (current theme)
5. Else default icon generic: DefaultIcon
6. Else none

### Icon Sources (.ico or DLL/EXE resources)
You can now reference icons embedded in modules (DLL/EXE) using a comma + index syntax:

Examples:
```
Item5=Control Panel|FILE|control.exe||shell32.dll,21
Item6=Downloads|FOLDER|%USERPROFILE%\Downloads|submenu|imageres.dll,184
DefaultIcon=shell32.dll,167
DefaultIconLight=imageres.dll,3
DefaultIconDark=shell32.dll,167
```

Rules:
- Syntax: `<modulePath>,<index>` where modulePath ends in `.dll` or `.exe`.
- If the module path has no backslashes (e.g. `shell32.dll`), the System32 directory is assumed.
- Environment variables in the module path are expanded.
- Index can be positive or negative (negative values refer to resource IDs as understood by `ExtractIconExW`).
- When no comma is present the path is treated as a normal `.ico` file (existing behavior).
- Small (16x16) icons are extracted; large variant (if any) is discarded.
- Fallback: if extraction fails the loader attempts to treat the full string as a direct `.ico` path.

Recent submenu root & Power menu root follow: per-item icon > DefaultIcon. The optional "Clear Recent Items" command (when `RecentShowCleanItems=1`) has no icon.

### Theme-specific per-item icons
You can provide different icons per theme without changing `ItemN` lines using optional sections:

```
[IconsLight]
Icon1=C:\Icons\light\app.ico
Icon2=shell32.dll,46

[IconsDark]
Icon1=C:\Icons\dark\app.ico
Icon2=shell32.dll,47
```

Notes:
- Keys map by index: `IconN` corresponds to `ItemN` in the [Menu] section.
- Values accept the same sources as regular icons: `.ico` or `module.dll,index`.
- Environment variables expand in these paths as well.

### [Placement]
Used when PointerRelative = 0.

- HPlacement = Left | Center | Right
- HOffset   = integer (pixels; for Left/Right a negative value flips edge padding semantics; for Center it shifts relative to the screen center)
- VPlacement = Top | Center | Bottom
- VOffset   = integer (same rules as HOffset; for Center it shifts relative to the screen center)
 - IgnoreOffsetWhenCentered = false|true|hoffset|voffset
   - false: offsets apply normally even when centered
   - true (or both): ignore both HOffset and VOffset when the corresponding axis is centered
   - hoffset: ignore only HOffset when Horizontal=center
   - voffset: ignore only VOffset when Vertical=center

When PointerRelative = 1 (anchor near cursor):

- IgnoreOffsetWhenRelative = false|true|hoffset|voffset
  - false: apply both offsets relative to the cursor (default)
  - true (or both): ignore both offsets; anchor exactly at the cursor
  - hoffset: ignore only HOffset; apply VOffset
  - voffset: ignore only VOffset; apply HOffset

### [Menu]

ItemN = Label | TYPE | PathOrTarget | Params | IconPath

TYPE values:
- URI, FILE, CMD
- FOLDER (clickable or mode-adjusted by params tokens)
- FOLDER_SUBMENU
- POWER_SLEEP, POWER_SHUTDOWN, POWER_RESTART, POWER_LOCK, POWER_LOGOFF
- POWER_MENU (adds aggregated power submenu: Sleep, Shut down, Restart, Lock, Log off)
- RECENT_SUBMENU
- SEPARATOR

Folder params tokens (space-separated, case-insensitive):
- `submenu` treat as submenu (like FOLDER_SUBMENU)
- `link` force clickable folder
- `inline` inject contents at root (files & lazy submenus for subfolders)
- `inlineopen` clickable header + inline contents
- `notitle` / `noheader` suppress header row (with inline/inlineopen)

Example:
```
Item10=Code|FOLDER|%USERPROFILE%\Code|inline|
Item11=Scripts|FOLDER|%USERPROFILE%\Scripts|inline notitle|
Item12=Projects|FOLDER|%USERPROFILE%\Projects|inlineopen|
```

### Inline Expansion Notes
- Subdirectories become lazy submenus (depth starts at 2)
- Visibility filters apply (ShowHidden / ShowDotfiles modes)
- No automatic separator insertion

### [Icons]
`IconN` maps to `ItemN` if the item itself doesn’t define an icon path.

### Experimental Flags Recap
- inline, inlineopen, notitle/noheader (removable without schema changes)

## Behavior Details
- First-letter activation
- Outside click dismisses menu
- DPI-aware scaling via system menu metrics
- Icons in legacy mode use MIIM_BITMAP, keeping native look (no custom owner-draw)
- Recent resolves .lnk targets; missing targets skipped
- Power actions: consistent markers used internally (POWER_MENU aggregates)
- Single-instance per INI path
- Second invocation toggle: launching the executable again (e.g., via a Windows key binding in an external tool) sends a toggle message. If the menu is open it closes; if closed it opens at the configured position. This enables assigning the EXE both to open and to dismiss via the same key.

## Troubleshooting
- Empty folder submenu: check path, permissions, filters (ShowHidden / ShowDotfiles)
- No icons: ensure ShowIcons=1 (or LegacyIcons=1 for backward compatibility) and paths are correct
- Folder icons not appearing: set ShowFolderIcons=1; the system folder icon only shows when no per-item icon exists

## Notes
> [!NOTE]
> **Please be informed that this is a beta version - you're using it at your own risk!**
- Built with standard Win32 APIs: user32, shell32, shlwapi, comctl32, uxtheme, dwmapi, powrprof, advapi32.
- This app uses legacy popup menus; so no parity with Windows 11 Fluent Design System for now
- It's recommended to use it together with Open-Shell, so the WinMacMenu can be triggered by clicking the Start menu button with the left mouse button or by pressing the Windows key.
- You can also pin shortcuts to taskbar, or add to custom toolbar. Each shortcut can refer to different config.ini files.

## Future plans
- Sub-menus sorting options
- Different depths level for specific folders
- Add modern style to follow Fluent Design System principles
- Possibility to open WinMac menu with right clicking Start button using Open-Shell ([#2286](https://github.com/Open-Shell/Open-Shell-Menu/issues/2286))