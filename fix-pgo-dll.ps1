# SPDX-FileCopyrightText: 2025 citron Emulator Project
# SPDX-License-Identifier: GPL-2.0-or-later

# Fix PGO DLL Script for Citron (Windows)
# This script helps locate and copy the required pgort140.dll for MSVC PGO builds

param(
    [Parameter()]
    [string]$OutputDir = "build\bin\Release",
    
    [Parameter()]
    [switch]$Help
)

function Show-Usage {
    Write-Host @"
Usage: .\fix-pgo-dll.ps1 [OPTIONS]

This script helps fix the "pgort140.DLL was not found" error by locating
and copying the required PGO runtime DLL to your build output directory.

Options:
  -OutputDir PATH    Target directory to copy DLL (default: build\bin\Release)
  -Help              Show this help message

Example:
  .\fix-pgo-dll.ps1
  .\fix-pgo-dll.ps1 -OutputDir "C:\MyBuild\bin"
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

Write-Header "PGO DLL Fixer for Citron"

# Find Visual Studio installation
$VSInstallPath = $null
$VSWhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"

if (Test-Path $VSWhere) {
    Write-Info "Searching for Visual Studio installations..."
    $VSInstallPath = & $VSWhere -latest -property installationPath
    if ($VSInstallPath) {
        Write-Info "Found Visual Studio at: $VSInstallPath"
    }
} else {
    Write-Warning "vswhere.exe not found. Trying common installation paths..."
}

# Common Visual Studio installation paths
$CommonPaths = @(
    "${env:ProgramFiles}\Microsoft Visual Studio\2022\Community",
    "${env:ProgramFiles}\Microsoft Visual Studio\2022\Professional", 
    "${env:ProgramFiles}\Microsoft Visual Studio\2022\Enterprise",
    "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2019\Community",
    "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2019\Professional",
    "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2019\Enterprise"
)

if (-not $VSInstallPath) {
    foreach ($path in $CommonPaths) {
        if (Test-Path $path) {
            $VSInstallPath = $path
            Write-Info "Found Visual Studio at: $VSInstallPath"
            break
        }
    }
}

if (-not $VSInstallPath) {
    Write-Error-Custom "Visual Studio installation not found!"
    Write-Host "Please ensure Visual Studio 2019 or 2022 is installed with C++ support."
    Write-Host "You can download it from: https://visualstudio.microsoft.com/downloads/"
    exit 1
}

# Search for pgort140.dll
Write-Info "Searching for pgort140.dll..."

$DllPaths = @(
    "$VSInstallPath\VC\Redist\MSVC\*\x64\Microsoft.VC*.CRT\pgort140.dll",
    "$VSInstallPath\VC\Redist\MSVC\*\x86\Microsoft.VC*.CRT\pgort140.dll",
    "$VSInstallPath\VC\Tools\MSVC\*\bin\Hostx64\x64\pgort140.dll",
    "$VSInstallPath\VC\Tools\MSVC\*\bin\Hostx64\x86\pgort140.dll",
    "$VSInstallPath\VC\Tools\MSVC\*\bin\Hostx86\x64\pgort140.dll",
    "$VSInstallPath\VC\Tools\MSVC\*\bin\Hostx86\x86\pgort140.dll"
)

$FoundDll = $null
foreach ($pattern in $DllPaths) {
    $matches = Get-ChildItem -Path $pattern -ErrorAction SilentlyContinue
    if ($matches) {
        $FoundDll = $matches[0].FullName
        Write-Info "Found pgort140.dll at: $FoundDll"
        break
    }
}

if (-not $FoundDll) {
    Write-Error-Custom "pgort140.dll not found in Visual Studio installation!"
    Write-Host "This usually means:"
    Write-Host "1. Visual Studio was installed without PGO support"
    Write-Host "2. You need to install the 'MSVC v143 - VS 2022 C++ x64/x86 build tools' component"
    Write-Host "3. Try repairing your Visual Studio installation"
    Write-Host ""
    Write-Host "To fix this:"
    Write-Host "1. Open Visual Studio Installer"
    Write-Host "2. Click 'Modify' on your Visual Studio installation"
    Write-Host "3. Go to 'Individual components' tab"
    Write-Host "4. Search for 'PGO' and ensure it's checked"
    Write-Host "5. Click 'Modify' to install the component"
    exit 1
}

# Create output directory if it doesn't exist
if (-not (Test-Path $OutputDir)) {
    Write-Info "Creating output directory: $OutputDir"
    New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
}

# Copy the DLL
try {
    Write-Info "Copying pgort140.dll to: $OutputDir"
    Copy-Item -Path $FoundDll -Destination $OutputDir -Force
    Write-Info "Successfully copied pgort140.dll!"
    
    # Verify the copy
    $CopiedDll = Join-Path $OutputDir "pgort140.dll"
    if (Test-Path $CopiedDll) {
        Write-Info "Verification: pgort140.dll is now available in $OutputDir"
        Write-Host ""
        Write-Host "You can now run your PGO instrumented Citron build!"
    } else {
        Write-Error-Custom "Failed to copy pgort140.dll"
        exit 1
    }
} catch {
    Write-Error-Custom "Error copying pgort140.dll: $($_.Exception.Message)"
    exit 1
}

Write-Header "PGO DLL Fix Complete!"
Write-Info "The pgort140.dll has been copied to your build output directory."
Write-Info "Your PGO instrumented Citron build should now run without the DLL error."
Write-Host ""
Write-Info "Next steps:"
Write-Host "1. Run your PGO instrumented build"
Write-Host "2. Play games to collect profile data"
Write-Host "3. Rebuild with PGO USE stage for optimization"
