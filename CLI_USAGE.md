# WinMacMenu CLI Usage Guide

WinMacMenu now supports command-line interface (CLI) operations, allowing you to manage multiple WinMacMenu sessions from the command line.

## Available CLI Commands

### Help
Display help information about available commands:
```cmd
WinMacMenu.exe --help
WinMacMenu.exe -h  
WinMacMenu.exe /?
```

### List Sessions
List all currently running WinMacMenu sessions with detailed information:
```cmd
WinMacMenu.exe --list
WinMacMenu.exe -l                          # Short alias
WinMacMenu.exe --list --output list        # Default list format (detailed)
WinMacMenu.exe -l --output table           # Compact table format
```

**Output includes:**
- Process ID (PID)
- Window Title
- Config File Name
- Config File Path
- ShowOnLaunch setting
- ShowTrayIcon setting

**Output Formats:**
- **List format** (default): Detailed, multi-line format with Window Title first, no title header
  - Best for: Interactive use, detailed inspection, troubleshooting
- **Table format**: Compact tabular format with Window Title first, column headers, no title header
  - Best for: Scripts, quick overview, when managing many sessions

**Example list format output:**
```
Window Title: WinMacMenu
PID: 1234
Config File: config.ini
Config Path: <default>\config.ini
ShowOnLaunch: true
ShowTrayIcon: true

Window Title: WinMacMenu::custom
PID: 5678
Config File: custom.ini
Config Path: custom.ini
ShowOnLaunch: false
ShowTrayIcon: true
```

**Example table format output:**
```
Window Title         PID      Config File          ShowOnLaunch ShowTrayIcon Config Path
-------------------- -------- -------------------- ------------ ------------ --------------------
WinMacMenu           1234     config.ini           true         true         <default>\config.ini
WinMacMenu::custom   5678     custom.ini           false        true         custom.ini
```

### Reload Session
Restart a specific WinMacMenu session by PID (preserves configuration):
```cmd
WinMacMenu.exe --reload <PID>
WinMacMenu.exe -r <PID>        # Short alias
```

**Example:**
```cmd
WinMacMenu.exe --reload 1234
WinMacMenu.exe -r 1234         # Short alias
```

This will:
1. Gracefully terminate the specified session
2. Start a new session with the same configuration
3. The new session will have a different PID

### Shutdown Session
Gracefully shutdown a specific WinMacMenu session by PID:
```cmd
WinMacMenu.exe --shutdown <PID>
WinMacMenu.exe -k <PID>         # Short alias
```

**Example:**
```cmd
WinMacMenu.exe --shutdown 1234
WinMacMenu.exe -k 1234          # Short alias
```

### Open Settings
Open the settings dialog for a specific WinMacMenu session:
```cmd
WinMacMenu.exe --settings <PID>
WinMacMenu.exe -s <PID>         # Short alias
```

**Example:**
```cmd
WinMacMenu.exe --settings 1234
WinMacMenu.exe -s 1234          # Short alias
```

This will open the settings GUI dialog for the specified session, allowing you to modify configuration options.

## Configuration Management

### Using Custom Configurations
You can specify custom configuration files when starting WinMacMenu:
```cmd
WinMacMenu.exe --config "path\to\custom.ini"
```

This works with both GUI and CLI modes:
```cmd
WinMacMenu.exe --config "custom.ini" --list
WinMacMenu.exe --config "project1.ini"
```

### Multiple Sessions
You can run multiple WinMacMenu sessions simultaneously with different configurations:
```cmd
WinMacMenu.exe --config "config1.ini"
WinMacMenu.exe --config "config2.ini"
WinMacMenu.exe --config "config3.ini"
```

Each session will have its own:
- Process ID
- Configuration settings
- Tray icon (if enabled)
- Menu customizations

## Workflow Examples

### Daily Management Workflow
```cmd
# Quick overview of running sessions (table format)
WinMacMenu.exe -l --output table

# Detailed inspection of sessions (list format)
WinMacMenu.exe -l --output list

# Reload a session after config changes
WinMacMenu.exe -r 1234

# Open settings to modify configuration
WinMacMenu.exe -s 1234

# Shutdown unnecessary sessions
WinMacMenu.exe -k 5678
```

### Multi-Configuration Setup
```cmd
# Start different configurations for different projects
WinMacMenu.exe --config "work.ini"
WinMacMenu.exe --config "personal.ini"
WinMacMenu.exe --config "development.ini"

# List all running sessions
WinMacMenu.exe -l

# Manage specific sessions as needed
WinMacMenu.exe -s 1234
WinMacMenu.exe -r 5678
```

## Integration with Scripts

The CLI interface makes it easy to integrate WinMacMenu management into scripts:

### PowerShell Example
```powershell
# Get all WinMacMenu PIDs using table format (easier to parse)
$sessions = & "WinMacMenu.exe" -l --output table | Select-Object -Skip 4 | Where-Object { $_ -match "^\d+" }

# Reload all sessions
foreach ($session in $sessions) {
    if ($session -match "^(\d+)") {
        $pid = $matches[1]
        Write-Host "Reloading session $pid"
        & "WinMacMenu.exe" -r $pid
    }
}

# Alternative using list format
$sessions = & "WinMacMenu.exe" -l | Where-Object { $_ -match "PID: (\d+)" }
foreach ($session in $sessions) {
    if ($session -match "PID: (\d+)") {
        $pid = $matches[1]
        Write-Host "Reloading session $pid"
        & "WinMacMenu.exe" -r $pid
    }
}
```

### Batch Example
```batch
@echo off
echo Checking WinMacMenu sessions...
WinMacMenu.exe -l

echo.
echo Reloading session 1234...
WinMacMenu.exe -r 1234

echo.
echo Opening settings for session 5678...
WinMacMenu.exe -s 5678
```

## Error Handling

The CLI commands provide appropriate error messages:

- **Invalid PID**: "Error: Invalid PID 'abc' for --reload"
- **Session not found**: "Error: No WinMacMenu session found with PID 1234"
- **No sessions**: "No WinMacMenu sessions found."

## Return Codes

- **0**: Success
- **1**: Error (invalid arguments, session not found, etc.)

This allows for proper error handling in scripts and automation workflows.