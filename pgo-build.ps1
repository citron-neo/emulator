# SPDX-FileCopyrightText: 2025 citron Emulator Project
# SPDX-License-Identifier: GPL-2.0-or-later

# PGO Build Script for Citron (Windows/PowerShell)
# This script automates the Profile-Guided Optimization build process

param(
    [Parameter(Position=0, Mandatory=$true)]
    [ValidateSet('generate', 'use', 'clean', 'merge', 'summary')]
    [string]$Stage,
    
    [Parameter()]
    [int]$Jobs = 0,
    
    [Parameter()]
    [switch]$EnableLTO,

    [Parameter()]
    [switch]$Exact,
    
    [Parameter()]
    [switch]$Help
)

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$BuildDir = Join-Path $ScriptDir "build"
$PgoProfilesDir = Join-Path $BuildDir "pgo-profiles"
$BackupProfilesDir = Join-Path $ScriptDir "pgo-profiles-backup"

function Show-Usage {
    Write-Host @"
Usage: .\pgo-build.ps1 [STAGE] [OPTIONS]

STAGE can be:
  generate  - Build with PGO instrumentation (Stage 1)
  use       - Build using PGO profile data (Stage 2)
  clean     - Clean build directory but preserve profiles
  merge     - Merge PGC files into PGD without rebuilding
  summary   - Show profile coverage statistics

Example workflow:
  .\pgo-build.ps1 generate       # Build instrumented version
  # Run citron.exe, play 2-3 games for 5-10 min each, exit cleanly
  .\pgo-build.ps1 merge          # Merge collected profiles
  .\pgo-build.ps1 summary        # Check coverage
  .\pgo-build.ps1 use            # Build optimized version

Options:
  -Jobs N       Number of parallel jobs (default: auto-detect)
  -EnableLTO    Enable Link-Time Optimization
  -Exact        Use /GENPROFILE:EXACT for maximum precision (slower)
  -Help         Show this help message
"@
}

function Write-Header {
    param([string]$Message)
    Write-Host "`n=================================================================" -ForegroundColor Cyan
    Write-Host $Message -ForegroundColor Cyan
    Write-Host "=================================================================`n" -ForegroundColor Cyan
}

function Write-Info {
    param([string]$Message)
    Write-Host "[INFO] $Message" -ForegroundColor Green
}

function Write-Warning {
    param([string]$Message)
    Write-Host "[WARNING] $Message" -ForegroundColor Yellow
}

function Write-Error-Custom {
    param([string]$Message)
    Write-Host "[ERROR] $Message" -ForegroundColor Red
}

function Find-Pgomgr {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $vsPath = & $vswhere -latest -property installationPath
        $pgomgr = Get-ChildItem -Path "$vsPath\VC\Tools\MSVC" -Recurse -Filter "pgomgr.exe" -ErrorAction SilentlyContinue |
            Where-Object { $_.FullName -match "Hostx64\\x64" } |
            Select-Object -First 1
        if ($pgomgr) { return $pgomgr.FullName }
    }
    $found = Get-Command pgomgr.exe -ErrorAction SilentlyContinue
    if ($found) { return $found.Source }
    return $null
}

function Clean-StaleProfiles {
    if (-not (Test-Path $PgoProfilesDir)) { return }

    $pgcFiles = Get-ChildItem -Path $PgoProfilesDir -Filter "*.pgc" -ErrorAction SilentlyContinue
    if ($pgcFiles -and $pgcFiles.Count -gt 0) {
        Write-Info "Removing $($pgcFiles.Count) stale PGC file(s)..."
        $pgcFiles | Remove-Item -Force
    }
}

function Merge-Profiles {
    $pgomgr = Find-Pgomgr
    if (-not $pgomgr) {
        Write-Warning "pgomgr.exe not found. Cannot merge profiles."
        Write-Info "Ensure Visual Studio Build Tools are installed and on PATH."
        return $false
    }

    $pgdFiles = Get-ChildItem -Path $PgoProfilesDir -Filter "*.pgd" -ErrorAction SilentlyContinue
    if (-not $pgdFiles -or $pgdFiles.Count -eq 0) {
        Write-Warning "No PGD files found in $PgoProfilesDir"
        return $false
    }

    $pgcFiles = Get-ChildItem -Path $PgoProfilesDir -Filter "*.pgc" -ErrorAction SilentlyContinue
    if (-not $pgcFiles -or $pgcFiles.Count -eq 0) {
        Write-Info "No PGC files to merge."
        return $true
    }

    foreach ($pgd in $pgdFiles) {
        Write-Info "Merging PGC files into $($pgd.Name)..."
        & $pgomgr /merge $pgd.FullName
        if ($LASTEXITCODE -ne 0) {
            Write-Warning "pgomgr /merge returned exit code $LASTEXITCODE for $($pgd.Name)"
        }
    }

    # Clean PGC files after successful merge
    Write-Info "Cleaning merged PGC files..."
    Get-ChildItem -Path $PgoProfilesDir -Filter "*.pgc" -ErrorAction SilentlyContinue | Remove-Item -Force

    return $true
}

function Show-ProfileSummary {
    $pgomgr = Find-Pgomgr
    if (-not $pgomgr) {
        Write-Warning "pgomgr.exe not found. Cannot show summary."
        return
    }

    $pgdFiles = Get-ChildItem -Path $PgoProfilesDir -Filter "*.pgd" -ErrorAction SilentlyContinue
    if (-not $pgdFiles -or $pgdFiles.Count -eq 0) {
        Write-Warning "No PGD files found in $PgoProfilesDir"
        return
    }

    foreach ($pgd in $pgdFiles) {
        Write-Header "Profile Summary: $($pgd.Name)"
        & $pgomgr /summary $pgd.FullName
        Write-Host ""
    }
}

if ($Help) {
    Show-Usage
    exit 0
}

# Auto-detect number of processors
if ($Jobs -eq 0) {
    $Jobs = $env:NUMBER_OF_PROCESSORS
    if (-not $Jobs) { $Jobs = 4 }
}

$LtoFlag = if ($EnableLTO) { "ON" } else { "OFF" }
$ExactFlag = if ($Exact) { "ON" } else { "OFF" }

# --- Clean stage ---
if ($Stage -eq "clean") {
    Write-Header "Cleaning Build Directory"
    
    if (Test-Path $PgoProfilesDir) {
        Write-Info "Backing up PGO profiles..."
        New-Item -ItemType Directory -Force -Path $BackupProfilesDir | Out-Null
        Copy-Item -Path "$PgoProfilesDir\*" -Destination $BackupProfilesDir -Recurse -Force -ErrorAction SilentlyContinue
    }
    
    if (Test-Path $BuildDir) {
        Write-Info "Removing build directory..."
        Remove-Item -Path $BuildDir -Recurse -Force
    }
    
    if (Test-Path $BackupProfilesDir) {
        Write-Info "Restoring PGO profiles..."
        New-Item -ItemType Directory -Force -Path $PgoProfilesDir | Out-Null
        Move-Item -Path "$BackupProfilesDir\*" -Destination $PgoProfilesDir -Force -ErrorAction SilentlyContinue
        Remove-Item -Path $BackupProfilesDir -Recurse -Force
    }
    
    Write-Info "Clean complete!"
    exit 0
}

# --- Merge stage ---
if ($Stage -eq "merge") {
    Write-Header "Merging PGO Profiles"
    
    if (-not (Test-Path $PgoProfilesDir)) {
        Write-Error-Custom "Profile directory not found: $PgoProfilesDir"
        exit 1
    }

    $result = Merge-Profiles
    if ($result) {
        Write-Info "Merge complete! Run '.\pgo-build.ps1 summary' to check coverage."
    } else {
        Write-Error-Custom "Merge failed."
        exit 1
    }
    exit 0
}

# --- Summary stage ---
if ($Stage -eq "summary") {
    Write-Header "PGO Profile Summary"

    if (-not (Test-Path $PgoProfilesDir)) {
        Write-Error-Custom "Profile directory not found: $PgoProfilesDir"
        exit 1
    }

    Show-ProfileSummary
    exit 0
}

# --- Generate stage ---
if ($Stage -eq "generate") {
    Write-Header "PGO Stage 1: Generate Profile Data"

    # Clean stale PGC files from any previous run
    if (Test-Path $PgoProfilesDir) {
        Clean-StaleProfiles
    }
    
    # Create build directory
    New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null
    Set-Location $BuildDir
    
    # Configure
    Write-Info "Configuring CMake..."
    cmake .. `
        -DCITRON_ENABLE_PGO_GENERATE=ON `
        -DCITRON_PGO_EXACT=$ExactFlag `
        -DCITRON_ENABLE_LTO=$LtoFlag `
        -DCMAKE_BUILD_TYPE=Release
    
    if ($LASTEXITCODE -ne 0) {
        Write-Error-Custom "CMake configuration failed"
        exit 1
    }
    
    # Build
    Write-Info "Building instrumented Citron (this may take a while)..."
    cmake --build . --config Release -j $Jobs
    
    if ($LASTEXITCODE -ne 0) {
        Write-Error-Custom "Build failed"
        exit 1
    }
    
    Write-Header "Build Complete!"
    Write-Host ""
    Write-Host "  Training guide for best PGO results:" -ForegroundColor Yellow
    Write-Host ""
    Write-Host "  1. Run:  .\bin\Release\citron.exe"
    Write-Host "  2. Launch a game and play PAST initial loading"
    Write-Host "     (first-time shader compilation is a critical hot path)"
    Write-Host "  3. Play for at least 5-10 minutes per game"
    Write-Host "  4. Test 2-3 different games for broader code coverage"
    Write-Host "  5. Navigate menus, settings, and game list to profile the UI"
    Write-Host "  6. Exit citron cleanly (File -> Exit or Ctrl+Q)"
    Write-Host ""
    Write-Host "  After each session, you can run:" -ForegroundColor Yellow
    Write-Host "    .\pgo-build.ps1 merge     # Consolidate collected profiles"
    Write-Host "    .\pgo-build.ps1 summary   # Check profile coverage"
    Write-Host ""
    Write-Host "  When satisfied with coverage, build the optimized binary:" -ForegroundColor Yellow
    Write-Host "    .\pgo-build.ps1 use"
    Write-Host ""
    
    Set-Location $ScriptDir
}

# --- Use stage ---
if ($Stage -eq "use") {
    Write-Header "PGO Stage 2: Build Optimized Binary"
    
    # Check if profile data exists
    if (-not (Test-Path $PgoProfilesDir) -or -not (Get-ChildItem $PgoProfilesDir -ErrorAction SilentlyContinue)) {
        Write-Error-Custom "No profile data found in $PgoProfilesDir"
        Write-Info "Please run the generate stage first and collect profile data"
        exit 1
    }

    # Merge any outstanding PGC files before building
    Write-Info "Merging any outstanding PGC files..."
    Merge-Profiles | Out-Null
    
    # Backup profiles if build directory exists
    if (Test-Path $BuildDir) {
        Write-Info "Backing up PGO profiles..."
        New-Item -ItemType Directory -Force -Path $BackupProfilesDir | Out-Null
        Copy-Item -Path "$PgoProfilesDir\*" -Destination $BackupProfilesDir -Recurse -Force
        Remove-Item -Path $BuildDir -Recurse -Force
    }
    
    # Create build directory and restore profiles
    New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null
    if (Test-Path $BackupProfilesDir) {
        New-Item -ItemType Directory -Force -Path $PgoProfilesDir | Out-Null
        Move-Item -Path "$BackupProfilesDir\*" -Destination $PgoProfilesDir -Force
        Remove-Item -Path $BackupProfilesDir -Recurse -Force
    }
    
    Set-Location $BuildDir
    
    # Configure
    Write-Info "Configuring CMake..."
    cmake .. `
        -DCITRON_ENABLE_PGO_USE=ON `
        -DCITRON_ENABLE_LTO=$LtoFlag `
        -DCMAKE_BUILD_TYPE=Release
    
    if ($LASTEXITCODE -ne 0) {
        Write-Error-Custom "CMake configuration failed"
        Set-Location $ScriptDir
        exit 1
    }
    
    # Build
    Write-Info "Building optimized Citron (this may take a while)..."
    cmake --build . --config Release -j $Jobs
    
    if ($LASTEXITCODE -ne 0) {
        Write-Error-Custom "Build failed"
        Set-Location $ScriptDir
        exit 1
    }
    
    Write-Header "Build Complete!"
    Write-Info "Your optimized Citron binary is ready!"
    Write-Info "Location: $BuildDir\bin\Release\citron.exe"
    Write-Host ""
    Write-Info "This build is optimized for your specific usage patterns."
    
    Set-Location $ScriptDir
}
