<h1 align="center">
  <span>WinMac Menu</span>
</h1>

<p align="center">
  <a href="https://github.com/Asteski/WinMac-Menu/releases/latest/download/WinMacMenu-1.0.0-x64.zip"><img alt="Download x64" src="https://img.shields.io/badge/Download-x64-0078D6?style=for-the-badge"></a>
  <a href="https://github.com/Asteski/WinMac-Menu/releases/latest/download/WinMacMenu-arm64.zip"><img alt="Download ARM64" src="https://img.shields.io/badge/Download-ARM64-00A86B?style=for-the-badge"></a>
</p>

<p align="center">
  <img alt="Latest Release" src="https://img.shields.io/github/v/release/Asteski/WinMac-Menu">
  <img alt="Release Date" src="https://img.shields.io/github/release-date/Asteski/WinMac-Menu">
  <img alt="Downloads (all releases)" src="https://img.shields.io/github/downloads/Asteski/WinMac-Menu/total">
  <img alt="Downloads (latest release)" src="https://img.shields.io/github/downloads/Asteski/WinMac-Menu/latest/total">
</p>

Windows application that shows a Windows context-like popup menu. It’s configurable via an INI file, adapts to your light/dark theme, allows to specify relative and absolute positioning, supports a dynamic Recent Items and Force Quit submenus, shell objects, folder submenus, power options menu, showing folder content in root menu, icons, sorting and many more! [Discover the range of possible use cases.](https://github.com/Asteski/WinMac-Menu/wiki/Examples-of-use)

### Windows 11:
![WinMacMenu screenshot](img/winmacmenu-demo-11.png)
### Windows 10:
![WinMacMenu screenshot](img/winmacmenu-demo-10.png)

## Features

- Available in both Win32 and WinUI frameworks
- Config-driven items with separators, folders as submenus, URIs, commands, power actions, and more
- Recent Items - dynamic submenu from *%AppData%\Microsoft\Windows\Recent Items* folder
- Task Kill - dynamic submenu to forcibly end tasks
- This PC - dynamic submenu to list This PC content
- Home - dynamic submenu to show User Profile folder
- Light/Dark auto-adaptation - immersive dark hint on the invisible owner window
- Icons - per-item icons or glyphs, theme-aware overrides (light/dark), optional DefaultIcon + theme variants, and optional folder or files icon retrieval
- Placement controls (edges, center, or cursor, with offsets + ignore options), per‑monitor DPI aware
- Folder submenu behaviors: lazy population, max depth, name-only items, optional “Open <folder>” entry
- Inline folder expansion (inject a folder’s contents directly into the root menu) with optional clickable header
- Sorting of folder content by name, date, size and type
- Granular extension hiding (global + recent-only override)
- Settings GUI available for those, who do not want to modify INI file directly
- Enable activation of WinMac Menu using various Start button and Windows key triggers
- Trigger folder or file context menu directly from submenus

## Run

**Quick Start**: By default, it runs in background mode with a system tray icon.

**Modes**:
- **Background Mode (default)** - App stays running in background. Launch again to toggle/show menu.
- **Single Run** - Set `RunInBackground=false` in config to exit after menu closes (legacy behavior).
  - To start silently in background, without showing the menu on first launch, set `ShowOnLaunch=false`.

**Custom config**: Use `--config <path>` to point at a custom INI (single instance per INI path applies).

## Sections
- *[General]* global behavior and style
- *[Placement]* position rules
- *[Menu]* menu items Item1..ItemN
- *[Icons]* per-item icon mapping Icon1..IconN and optional DefaultIcon/DefaultIconLight/DefaultIconDark
- *[IconsLight]* theme-specific per-item icons for light theme (Icon1..IconN)
- *[IconsDark]* theme-specific per-item icons for dark theme (Icon1..IconN)
- *[Power]* exclude specific power options
- *[Sorting]* sort folder content
- *[Appearance]* enable and configure different frameworks
- *[Controls]* enable pre-defined triggers
- *[RecentItems]* item settings
- *[TaskKill]* item settings
- *[ThisPC]* item settings
- *[Home]* item settings
- *[Logging]*

You can find more details about each section in [Wiki](https://github.com/Asteski/WinMac-Menu/wiki) page.

## Security & Privacy
- No telemetry. The app makes no network connections and collects no data.
- Registry usage: optional HKCU\Software\Microsoft\Windows\CurrentVersion\Run entry when you enable “Start on login” from the tray. It’s off by default and only changed when you toggle it. There is no service or scheduled task.
- Keyboard/mouse hooks: installed only while the popup menu is visible to allow outside‑click/escape dismissal; they are immediately removed when the menu closes.
- Recent cleanup: choosing “Clear Recent Items list” deletes shortcut (.lnk) files from your user Recent folder. It doesn’t touch actual documents or programs. There’s no confirmation.
- Filesystem: reads your config.ini and enumerates folders you explicitly reference in the menu.

## Troubleshooting
- Empty folder submenu: check path, permissions, filters (ShowHidden / ShowDotfiles)
- No icons: ensure ShowIcons=true (or LegacyIcons=true for backward compatibility) and paths are correct
- Folder icons not appearing: set ShowFolderIcons=true; the system folder icon only shows when no per-item icon exists

## Notes
- WinMacMenu reads an INI. If missing, a default (config.ini) is created.
- Environment variables expand in labels, paths, params, and icon paths (e.g., %USERNAME%).
- Indices N in [Icons]/[IconsLight]/[IconsDark] map to ItemN in [Menu].
- Generated default INI contains no comments (to keep the file minimal). Comments are still supported by the parser if you add them manually: lines beginning with `;` or `#` are ignored.
- Built with standard Win32 APIs: user32, shell32, shlwapi, comctl32, uxtheme, dwmapi, powrprof, advapi32.
- This app uses legacy popup menus; so no parity with Windows 11 Fluent Design System
- You can also pin shortcuts to taskbar, or add to custom toolbar. Each shortcut can refer to different config.ini files with different ini file names.
- INI file used in current session will be highlighted in tooptip of tray icon, if file name is different than default (config.ini).
- You can reference either *.dll or *.exe file in [Icons] section as path or just a file name (if the file resides in directory defined in %PATH%).
