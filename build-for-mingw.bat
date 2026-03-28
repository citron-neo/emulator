@echo off
REM SPDX-FileCopyrightText: 2026 citron Emulator Project
REM SPDX-License-Identifier: GPL-3.0-or-later
REM
REM One-click MinGW build launcher for Citron Neo.
REM Finds your MSYS2 installation and runs build-for-mingw.sh in UCRT64 mode.

setlocal enabledelayedexpansion

REM ---------- locate MSYS2 ----------
set "MSYS2_PATH="

REM Check common install locations
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

REM Try the PATH
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
echo  Please install MSYS2 from https://www.msys2.org/ and try again,
echo  or set MSYS2_PATH before running this script:
echo.
echo      set MSYS2_PATH=C:\msys64
echo      build-for-mingw.bat
echo.
pause
exit /b 1

:found_msys2
echo Found MSYS2 at: %MSYS2_PATH%

REM ---------- resolve script path ----------
set "SCRIPT_DIR=%~dp0"
REM Remove trailing backslash
if "%SCRIPT_DIR:~-1%"=="\" set "SCRIPT_DIR=%SCRIPT_DIR:~0,-1%"

REM Convert Windows path to MSYS2 path  (C:\foo\bar -> /c/foo/bar)
set "MSYS_SOURCE=%SCRIPT_DIR:\=/%"
set "MSYS_SOURCE=/%MSYS_SOURCE:~0,1%%MSYS_SOURCE:~2%"
REM Lowercase the drive letter
for %%a in (a b c d e f g h i j k l m n o p q r s t u v w x y z) do (
    set "MSYS_SOURCE=!MSYS_SOURCE:/%%a/=/%%a/!"
)

REM ---------- run the build ----------
echo.
echo Launching MSYS2 UCRT64 build ...
echo.

"%MSYS2_PATH%\usr\bin\env.exe" MSYSTEM=UCRT64 ^
    "%MSYS2_PATH%\usr\bin\bash.exe" -lc ^
    "cd '%MSYS_SOURCE%' && bash build-for-mingw.sh %*"

if %ERRORLEVEL% neq 0 (
    echo.
    echo Build failed with error code %ERRORLEVEL%.
    pause
    exit /b %ERRORLEVEL%
)

echo.
echo Build succeeded! Executables are in build-mingw\bin\
pause
