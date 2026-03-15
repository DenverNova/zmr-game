# Zombie Master: Reborn - Game Launcher
# Automatically detects Steam and launches the game

param(
    [switch]$Debug,
    [switch]$Dev
)

# Find Steam installation (check both C: and D: drives)
$steamPaths = @(
    "${env:ProgramFiles (x86)}\Steam",
    "${env:ProgramFiles}\Steam",
    "${env:LOCALAPPDATA}\Steam",
    "${env:APPDATA}\Steam",
    "D:\Program Files (x86)\Steam",
    "D:\Program Files\Steam",
    "D:\Steam"
)

$steamPath = $null
foreach ($path in $steamPaths) {
    if (Test-Path $path) {
        $steamPath = $path
        break
    }
}

if (-not $steamPath) {
    Write-Host "Error: Steam not found. Please install Steam and Source SDK Base 2013 Multiplayer." -ForegroundColor Red
    if (-not $Debug) { Read-Host "Press Enter to exit" }
    exit 1
}

Write-Host "Steam found at: $steamPath" -ForegroundColor Green

# Find Source SDK Base 2013 Multiplayer
$sdkPath = Join-Path $steamPath "steamapps\common\Source SDK Base 2013 Multiplayer"
if (-not (Test-Path $sdkPath)) {
    Write-Host "Error: Source SDK Base 2013 Multiplayer not found." -ForegroundColor Red
    Write-Host "Please install it from Steam: https://store.steampowered.com/app/243750/" -ForegroundColor Yellow
    if (-not $Debug) { Read-Host "Press Enter to exit" }
    exit 1
}

Write-Host "Source SDK Base 2013 Multiplayer found at: $sdkPath" -ForegroundColor Green

# Game paths
$gameDir = Join-Path $PSScriptRoot "mp\game\zombie_master_reborn"
$hl2Exe = Join-Path $sdkPath "hl2.exe"

if (-not (Test-Path $hl2Exe)) {
    Write-Host "Error: hl2.exe not found in Source SDK directory." -ForegroundColor Red
    if (-not $Debug) { Read-Host "Press Enter to exit" }
    exit 1
}

if (-not (Test-Path $gameDir)) {
    Write-Host "Error: Game directory not found at: $gameDir" -ForegroundColor Red
    if (-not $Debug) { Read-Host "Press Enter to exit" }
    exit 1
}

# Build launch arguments
$arguments = @(
    "-game",
    "`"$gameDir`"",
    "-steam"
)

# Add debug flags if requested
if ($Debug) {
    $arguments += @(
        "-dev",
        "-console",
        "-novid"
    )
}

# Add dev mode if requested
if ($Dev) {
    $arguments += @(
        "-dev",
        "-console",
        "-novid",
        "-allowdebug"
    )
}

Write-Host "Launching Zombie Master: Reborn..." -ForegroundColor Cyan
Write-Host "Executable: $hl2Exe" -ForegroundColor White
Write-Host "Arguments: $($arguments -join ' ')" -ForegroundColor White
Write-Host ""

# Launch the game
try {
    Start-Process -FilePath $hl2Exe -ArgumentList $arguments -WorkingDirectory $sdkPath
    Write-Host "Game launched successfully!" -ForegroundColor Green
}
catch {
    Write-Host "Error launching game: $($_.Exception.Message)" -ForegroundColor Red
    if (-not $Debug) { Read-Host "Press Enter to exit" }
    exit 1
}

if (-not $Debug) {
    Write-Host ""
    Write-Host "Game is starting. You can close this window." -ForegroundColor Cyan
    Start-Sleep -Seconds 2
}
