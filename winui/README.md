# WinMac Menu — WinUI 3

A WinUI 3 (C# / Windows App SDK) reimplementation of WinMac Menu. It reads the **same
`config.ini`** format as the Win32 app but draws the menu with WinUI's native
`MenuFlyout`, which matches the Windows 11 *Win+X* power-menu look (rounded corners,
acrylic backdrop, cascading submenus, light-dismiss) out of the box.

## Build & run

Requirements:
- Windows 10 (1809+) or Windows 11
- Visual Studio 2022 with the **.NET Desktop** and **Windows App SDK / WinUI** workloads,
  or the .NET 8 SDK + Windows App SDK
- Targets `net8.0-windows10.0.19041.0`

```powershell
cd winui
dotnet restore
dotnet build -c Release
dotnet run --project WinMacMenu
# or open WinMacMenu.sln in Visual Studio and press F5
```

The app is **unpackaged** (single EXE, no MSIX), matching the Win32 app's deployment model.
On first run it writes a default `config.ini` next to the executable if one is not present.

```powershell
WinMacMenu.exe                 # use config.ini next to the exe
WinMacMenu.exe --config C:\path\to\custom.ini
```

## How it maps to the C app

| Concern | C app | This project |
| --- | --- | --- |
| Config parsing | `config.c` / `GetPrivateProfile*` | `Config/IniFile.cs`, `Config/ConfigLoader.cs` |
| Item model | `ConfigItem` (config.h) | `Models/ConfigItem.cs`, `Models/Config.cs` |
| Drawing | Win32 legacy popup menus | `Menu/MenuBuilder.cs` → WinUI `MenuFlyout` |
| Icons (`shell32.dll,-271`, .ico) | `menu.c` | `Services/IconResolver.cs` |
| Launch / power | `util.c`, `menu.c` | `Services/ActionLauncher.cs` |
| Recent Items | `recent.c` | `Services/RecentItemsProvider.cs` + `Interop/ShellLink.cs` |
| End task | TaskKill | `Services/TaskKillProvider.cs` |
| This PC / Home | menu.c | `Services/SpecialFolderProvider.cs` |
| Folder submenus + sort | menu.c | `Services/FolderProvider.cs` |
| Placement | `[Placement]` | `Services/PlacementCalculator.cs` |
| Tray icon + menu | `main.c` | `Services/TrayIcon.cs` + `Services/MessageWindow.cs` |
| Start on login | `util.c` | `Services/StartupRegistry.cs` |
| Single instance + toggle | `main.c` | `Services/SingleInstance.cs` (mutex + named event) |
| Windows-key trigger | `taskbar_hook.c` / controls | `Services/KeyboardTrigger.cs` (low-level hook) |

All `[General]`, `[Placement]`, `[Sorting]`, `[Power]`, `[RecentItems]`, `[TaskKill]`,
`[ThisPC]`, `[Home]`, `[Controls]`, `[Control]`, `[Icons]`, `[IconsLight]`, `[IconsDark]`
keys and their defaults / back-compat fallbacks are honoured, matching `config.c`.

## Known gaps vs. the C app

- **Large-menu appearance** (`[Appearance] LargeMenuIcons`, large-menu highlight/animation)
  is intentionally **not** implemented here — it will be added separately.
- **Start-button click triggers** (`[Controls] LeftClick/RightClick/MiddleClick`) rely on the
  C app's taskbar shell-hook injection (`taskbar_hook.c`). That subsystem is not ported; the
  **Windows-key / Shift+Windows-key** trigger is implemented via a low-level keyboard hook.
- Folder submenus are populated eagerly up to `FolderSubmenuDepth`, with a global node budget
  to avoid stalls on very large trees (the C app populates lazily on hover).
