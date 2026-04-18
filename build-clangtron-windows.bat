@echo off
REM SPDX-FileCopyrightText: 2026 citron Emulator Project
REM SPDX-License-Identifier: GPL-3.0-or-later
REM
REM build-clangtron-windows.bat
REM Opens an MSYS2 CLANG64 shell in the repository directory, prints the
REM build-clangtron-windows.sh help text, and leaves the prompt open for you
REM to run build stages manually.
REM
REM This is NOT a one-click build.  See docs/BUILDING-CLANG-MINGW.md for
REM the full pipeline.

setlocal enabledelayedexpansion

REM ---------- locate MSYS2 ----------
set "MSYS2_PATH="

for %%P in (
    "C:\msys64"
    "C:\msys2"
    "%USERPROFILE%\msys64"
    "%USERPROFILE%\msys2"
    "D:\msys64"
    "D:\msys2"
) do (
    if exist "%%~P\usr\bin\bash.exe" (
        set "MSYS2_PATH=%%~P"
        goto :found_msys2
    )
)

REM Try to locate via PATH
where bash.exe >nul 2>&1
if %ERRORLEVEL% equ 0 (
    for /f "delims=" %%I in ('where bash.exe') do (
        set "BASH_LOC=%%~dpI"
        if exist "!BASH_LOC!..\..\..\usr\bin\bash.exe" (
            for %%J in ("!BASH_LOC!..\..\..") do set "MSYS2_PATH=%%~fJ"
            goto :found_msys2
        )
    )
)

echo.
echo  ERROR: Could not find an MSYS2 installation.
echo.
echo  Install MSYS2 from https://www.msys2.org/ then run setup:
echo.
echo      build-clangtron-windows.bat
echo      bash build-clangtron-windows.sh setup
echo.
echo  Or set MSYS2_PATH before running:
echo      set MSYS2_PATH=C:\msys64
echo      build-clangtron-windows.bat
echo.
pause
exit /b 1

:found_msys2
echo Found MSYS2 at: %MSYS2_PATH%

REM ---------- resolve script directory ----------
REM Strip trailing backslash left by %%~dp0
set "SCRIPT_DIR=%~dp0"
if "%SCRIPT_DIR:~-1%"=="\" set "SCRIPT_DIR=%SCRIPT_DIR:~0,-1%"

REM Use cygpath (always present in MSYS2) to convert the Windows path to a
REM POSIX path.  The manual letter-by-letter loop it replaces was inserting
REM extra slashes into every path component (e.g. /citron -> /c/itron).
for /f "delims=" %%P in ('^""%MSYS2_PATH%\usr\bin\cygpath.exe" "%SCRIPT_DIR%"^"') do set "MSYS_SOURCE=%%P"

if "%MSYS_SOURCE%"=="" (
    echo.
    echo  ERROR: cygpath failed to convert path: %SCRIPT_DIR%
    echo  Ensure MSYS2 is installed correctly.
    echo.
    pause
    exit /b 1
)

REM ---------- open CLANG64 shell, print help, leave prompt open ----------
echo.
echo  Opening MSYS2 CLANG64 shell.
echo  The build script help will be printed above the prompt.
echo  Run stages manually -- see docs/BUILDING-CLANG-MINGW.md for guidance.
echo.

"%MSYS2_PATH%\usr\bin\env.exe" MSYSTEM=CLANG64 ^
    "%MSYS2_PATH%\usr\bin\bash.exe" --login -c ^
    "cd '%MSYS_SOURCE%' && bash build-clangtron-windows.sh --help; exec bash"
