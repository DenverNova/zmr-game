<#
.SYNOPSIS
    Builds Zombie Master: Reborn using Visual Studio MSBuild with 32-bit configuration.

.DESCRIPTION
    This script builds the Zombie Master: Reborn Source engine mod using MSBuild.
    It generates the Visual Studio solution via VPC, applies the required solution
    configuration fix, and builds both client and server DLLs as 32-bit binaries.
    The VPC-generated PostBuildEvent automatically copies DLLs to the game bin folder.

.PARAMETER Configuration
    Build configuration (Debug or Release).
    Default: Release

.PARAMETER Clean
    Perform a clean build before building.

.PARAMETER RebuildSolution
    Force regeneration of the Visual Studio solution even if it already exists.

.EXAMPLE
    # Default Release build (double-click or run from terminal)
    .\Build-ZMR-32.ps1

    # Debug build
    .\Build-ZMR-32.ps1 -Configuration Debug

    # Clean rebuild with solution regeneration
    .\Build-ZMR-32.ps1 -Clean -RebuildSolution
#>

param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release",

    [switch]$Clean,

    [switch]$RebuildSolution
)

$ErrorActionPreference = "Stop"

# ─── Double-click detection ──────────────────────────────────────────────────
$isDoubleClicked = $false
try {
    $parentId = (Get-CimInstance Win32_Process -Filter "ProcessId = $PID" -ErrorAction SilentlyContinue).ParentProcessId
    if ($parentId) {
        $parentProcess = Get-CimInstance Win32_Process -Filter "ProcessId = $parentId" -ErrorAction SilentlyContinue
        if ($parentProcess -and $parentProcess.Name -eq "explorer.exe") {
            $isDoubleClicked = $true
        }
    }
} catch { }

function Pause-IfDoubleClicked {
    if ($isDoubleClicked) {
        Write-Host ""
        Write-Host "Press any key to exit..." -ForegroundColor Yellow
        $null = $Host.UI.RawUI.ReadKey("NoEcho,IncludeKeyDown")
    }
}

# ─── Locate Visual Studio 2022 ──────────────────────────────────────────────
function Find-VisualStudio {
    $vsWherePath = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vsWherePath) {
        $vsPath = (& $vsWherePath -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2>$null)
        if ($vsPath) { return $vsPath.Trim() }
    }
    $fallbacks = @(
        "${env:ProgramFiles}\Microsoft Visual Studio\2022\Community",
        "${env:ProgramFiles}\Microsoft Visual Studio\2022\Professional",
        "${env:ProgramFiles}\Microsoft Visual Studio\2022\Enterprise"
    )
    foreach ($p in $fallbacks) { if (Test-Path $p) { return $p } }
    return $null
}

function Find-MsBuild {
    param([string]$VsPath)
    $msbuild = Join-Path $VsPath "MSBuild\Current\Bin\MSBuild.exe"
    if (Test-Path $msbuild) { return $msbuild }
    return $null
}

# ─── Registry fix (required by VPC-generated vcxproj files) ─────────────────
function Ensure-VcxprojRegistryKey {
    $regKey   = "HKLM:\SOFTWARE\WOW6432Node\Microsoft\VisualStudio\10.0\Projects\{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}"
    $regValue = "DefaultProjectExtension"

    $needsFix = $false
    try {
        $current = Get-ItemProperty -Path $regKey -Name $regValue -ErrorAction Stop
        if ($current.$regValue -ne "vcxproj") { $needsFix = $true }
    } catch {
        $needsFix = $true
    }

    if (-not $needsFix) {
        Write-Host "  Registry key already set." -ForegroundColor DarkGray
        return
    }

    Write-Host "  Setting vcxproj registry key (requires admin)..." -ForegroundColor Yellow

    # Check if we are already elevated
    $isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)

    if ($isAdmin) {
        if (-not (Test-Path $regKey)) {
            New-Item -Path $regKey -Force | Out-Null
        }
        Set-ItemProperty -Path $regKey -Name $regValue -Value "vcxproj" -Type String -Force
        Write-Host "  Registry key set successfully." -ForegroundColor Green
    } else {
        $regCmd = "reg add `"HKEY_LOCAL_MACHINE\SOFTWARE\WOW6432Node\Microsoft\VisualStudio\10.0\Projects\{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}`" /v DefaultProjectExtension /t REG_SZ /d vcxproj /f"
        Start-Process -FilePath "cmd.exe" -ArgumentList "/c $regCmd" -Verb RunAs -Wait
        Write-Host "  Registry key set successfully (via elevation)." -ForegroundColor Green
    }
}

# ─── Generate solution with VPC ─────────────────────────────────────────────
function Generate-Solution {
    param([string]$SrcDir, [string]$SolutionName)

    $slnPath = Join-Path $SrcDir "$SolutionName"
    $slnFixPath = Join-Path $SrcDir "sln_fix.txt"

    Write-Host "Generating solution with VPC..." -ForegroundColor Yellow
    Push-Location $SrcDir
    try {
        # VPC defaults to Win32 platform and VS2013 format.
        # The v142 toolset is hardcoded in the VPC scripts and VS2022 handles it.
        & ".\devtools\bin\vpc.exe" /zmr +game /mksln $SolutionName
        if ($LASTEXITCODE -ne 0) {
            throw "VPC failed with exit code $LASTEXITCODE"
        }
        Write-Host "VPC solution generated." -ForegroundColor Green

        # Append the solution configuration fix (required for MSBuild to resolve
        # Debug|Win32 and Release|Win32 platform mappings).
        if (Test-Path $slnFixPath) {
            Write-Host "Applying sln_fix.txt..." -ForegroundColor Yellow
            $slnContent  = [System.IO.File]::ReadAllBytes($slnPath)
            $fixContent   = [System.IO.File]::ReadAllBytes($slnFixPath)
            $combined     = New-Object byte[] ($slnContent.Length + $fixContent.Length)
            [System.Buffer]::BlockCopy($slnContent, 0, $combined, 0, $slnContent.Length)
            [System.Buffer]::BlockCopy($fixContent, 0, $combined, $slnContent.Length, $fixContent.Length)
            [System.IO.File]::WriteAllBytes($slnPath, $combined)
            Write-Host "Solution fix applied." -ForegroundColor Green
        } else {
            Write-Host "WARNING: sln_fix.txt not found, build may fail." -ForegroundColor Red
        }
    }
    finally {
        Pop-Location
    }
}

# ═════════════════════════════════════════════════════════════════════════════
#  MAIN
# ═════════════════════════════════════════════════════════════════════════════
Write-Host "========================================" -ForegroundColor Cyan
Write-Host " Zombie Master: Reborn - 32-bit Build"   -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan

$projectRoot   = $PSScriptRoot
$srcDir        = Join-Path $projectRoot "mp\src"
$gameDir       = Join-Path $projectRoot "mp\game\zombie_master_reborn"
$solutionName  = "zmr-games.sln"
$solutionFile  = Join-Path $srcDir $solutionName

# ─── Verify project structure ────────────────────────────────────────────────
if (-not (Test-Path $srcDir)) {
    Write-Host "ERROR: Source directory not found: $srcDir" -ForegroundColor Red
    Pause-IfDoubleClicked; exit 1
}
if (-not (Test-Path $gameDir)) {
    Write-Host "ERROR: Game directory not found: $gameDir" -ForegroundColor Red
    Write-Host "Run: git submodule update --init --recursive" -ForegroundColor Yellow
    Pause-IfDoubleClicked; exit 1
}

# ─── Check dependencies ─────────────────────────────────────────────────────
Write-Host ""
Write-Host "Checking dependencies..." -ForegroundColor Cyan

$vsPath = Find-VisualStudio
if (-not $vsPath) {
    Write-Host "ERROR: Visual Studio 2022 with C++ tools not found!" -ForegroundColor Red
    Write-Host "Install VS2022 with 'Desktop development with C++' workload." -ForegroundColor Yellow
    Pause-IfDoubleClicked; exit 1
}
Write-Host "  Visual Studio: $vsPath" -ForegroundColor Green

$msbuildPath = Find-MsBuild -VsPath $vsPath
if (-not $msbuildPath) {
    Write-Host "ERROR: MSBuild not found in Visual Studio installation!" -ForegroundColor Red
    Pause-IfDoubleClicked; exit 1
}
Write-Host "  MSBuild: $msbuildPath" -ForegroundColor Green

# ─── Registry fix ────────────────────────────────────────────────────────────
Write-Host ""
Write-Host "Checking vcxproj registry key..." -ForegroundColor Cyan
Ensure-VcxprojRegistryKey

# ─── Generate or regenerate solution ─────────────────────────────────────────
Write-Host ""
if ($RebuildSolution -or -not (Test-Path $solutionFile)) {
    Generate-Solution -SrcDir $srcDir -SolutionName $solutionName
} else {
    Write-Host "Solution already exists: $solutionFile" -ForegroundColor DarkGray
    Write-Host "  (use -RebuildSolution to regenerate)" -ForegroundColor DarkGray
}

if (-not (Test-Path $solutionFile)) {
    Write-Host "ERROR: Solution file not found after generation: $solutionFile" -ForegroundColor Red
    Pause-IfDoubleClicked; exit 1
}

# ─── Build ───────────────────────────────────────────────────────────────────
Write-Host ""
Write-Host "Build Settings:" -ForegroundColor Cyan
Write-Host "  Solution:      $solutionFile" -ForegroundColor White
Write-Host "  Configuration: $Configuration" -ForegroundColor White
Write-Host "  Platform:      Win32" -ForegroundColor White
Write-Host ""

if ($Clean) {
    Write-Host "Cleaning..." -ForegroundColor Yellow
    & $msbuildPath $solutionFile /t:Clean "/p:Configuration=$Configuration" "/p:Platform=x86" /v:minimal /m
    Write-Host ""
}

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "Building ($Configuration|Win32)..."      -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan

# Build the entire solution. VPC project PostBuildEvents handle copying DLLs
# to mp/game/zombie_master_reborn/bin/ automatically.
& $msbuildPath $solutionFile "/p:Configuration=$Configuration" "/p:Platform=x86" /v:minimal /m

if ($LASTEXITCODE -ne 0) {
    Write-Host ""
    Write-Host "========================================" -ForegroundColor Red
    Write-Host "BUILD FAILED!" -ForegroundColor Red
    Write-Host "========================================" -ForegroundColor Red
    Pause-IfDoubleClicked; exit 1
}

# ─── Verify output ──────────────────────────────────────────────────────────
Write-Host ""
$gameBinDir = Join-Path $gameDir "bin"
$clientDll  = Join-Path $gameBinDir "client.dll"
$serverDll  = Join-Path $gameBinDir "server.dll"

$clientOk = Test-Path $clientDll
$serverOk = Test-Path $serverDll

Write-Host "Output verification:" -ForegroundColor Cyan
if ($clientOk) {
    $info = Get-Item $clientDll
    Write-Host "  client.dll  OK  ($([math]::Round($info.Length/1KB)) KB, $($info.LastWriteTime))" -ForegroundColor Green
} else {
    Write-Host "  client.dll  MISSING" -ForegroundColor Red
}
if ($serverOk) {
    $info = Get-Item $serverDll
    Write-Host "  server.dll  OK  ($([math]::Round($info.Length/1KB)) KB, $($info.LastWriteTime))" -ForegroundColor Green
} else {
    Write-Host "  server.dll  MISSING" -ForegroundColor Red
}

Write-Host ""
if ($clientOk -and $serverOk) {
    Write-Host "========================================" -ForegroundColor Green
    Write-Host "BUILD SUCCESSFUL!" -ForegroundColor Green
    Write-Host "========================================" -ForegroundColor Green
    Write-Host "DLLs are in: $gameBinDir" -ForegroundColor Green
    Write-Host "Run the game via Source SDK Base 2013 Multiplayer." -ForegroundColor Green
} else {
    Write-Host "========================================" -ForegroundColor Yellow
    Write-Host "BUILD COMPLETED (some DLLs may be missing)" -ForegroundColor Yellow
    Write-Host "========================================" -ForegroundColor Yellow
    Write-Host "Check build output above for errors." -ForegroundColor Yellow
}

Pause-IfDoubleClicked
