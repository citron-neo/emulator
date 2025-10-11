# SPDX-FileCopyrightText: 2025 citron Emulator Project
# SPDX-License-Identifier: GPL-2.0-or-later

# PGO Build Script for Citron (Windows/PowerShell)
# This script automates the Profile-Guided Optimization build process

param(
    [Parameter(Position=0, Mandatory=$true)]
    [ValidateSet('generate', 'use', 'clean')]
    [string]$Stage,
    
    [Parameter()]
    [int]$Jobs = 0,
    
    [Parameter()]
    [switch]$EnableLTO,
    
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

Example:
  .\pgo-build.ps1 generate    # Build instrumented version
  # Run citron.exe to collect profiles
  .\pgo-build.ps1 use         # Build optimized version

Options:
  -Jobs N       Number of parallel jobs (default: auto-detect)
  -EnableLTO    Enable Link-Time Optimization
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

# Clean stage
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

# Generate stage
if ($Stage -eq "generate") {
    Write-Header "PGO Stage 1: Generate Profile Data"
    
    # Create build directory
    New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null
    Set-Location $BuildDir
    
    # Configure
    Write-Info "Configuring CMake..."
    cmake .. `
        -DCITRON_ENABLE_PGO_GENERATE=ON `
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
    Write-Info "Next steps:"
    Write-Host "  1. Run: .\bin\Release\citron.exe"
    Write-Host "  2. Play games for 15-30 minutes to collect profile data"
    Write-Host "  3. Exit citron"
    Write-Host "  4. Run: .\pgo-build.ps1 use"
    
    Set-Location $ScriptDir
}

# Use stage
if ($Stage -eq "use") {
    Write-Header "PGO Stage 2: Build Optimized Binary"
    
    # Check if profile data exists
    if (-not (Test-Path $PgoProfilesDir) -or -not (Get-ChildItem $PgoProfilesDir -ErrorAction SilentlyContinue)) {
        Write-Error-Custom "No profile data found in $PgoProfilesDir"
        Write-Info "Please run the generate stage first and collect profile data"
        exit 1
    }
    
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
    Write-Info "Enjoy improved performance! 🚀"
    
    Set-Location $ScriptDir
}

