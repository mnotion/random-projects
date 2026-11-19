@echo off
setlocal EnableExtensions
title Build and Run GitHub Activity Window

cd /d "%~dp0"
echo.
echo ==========================================
echo   GitHub Activity Window - Build and Run
echo ==========================================
echo.

where cmake >nul 2>nul
if errorlevel 1 (
    echo ERROR: CMake was not found.
    echo Install CMake and the Visual Studio C++ desktop workload, then try again.
    pause
    exit /b 1
)

if not defined CMAKE_PREFIX_PATH (
    if exist "C:\Qt" (
        for /f "delims=" %%D in ('dir /b /ad /o-n "C:\Qt\6*" 2^>nul') do (
            for /f "delims=" %%K in ('dir /b /ad /o-n "C:\Qt\%%D\msvc*_64" 2^>nul') do (
                if exist "C:\Qt\%%D\%%K\lib\cmake\Qt6\Qt6Config.cmake" (
                    set "CMAKE_PREFIX_PATH=C:\Qt\%%D\%%K"
                    goto :qt_found
                )
            )
        )
    )
)

:qt_found
if not defined CMAKE_PREFIX_PATH (
    echo ERROR: Qt 6 for MSVC was not found automatically.
    echo Install Qt 6 with the MSVC 64-bit kit and the Widgets component.
    echo Or set CMAKE_PREFIX_PATH to your Qt kit folder, for example:
    echo C:\Qt\6.x.x\msvc2022_64
    pause
    exit /b 1
)

echo Using Qt: "%CMAKE_PREFIX_PATH%"
echo.
echo Configuring project...
cmake -S "%~dp0." -B "%~dp0build"
if errorlevel 1 goto :failed

echo.
echo Building application...
cmake --build "%~dp0build" --config Release
if errorlevel 1 goto :failed

set "APP="
if exist "%~dp0build\Release\SimpleScreen.exe" set "APP=%~dp0build\Release\SimpleScreen.exe"
if not defined APP if exist "%~dp0build\SimpleScreen.exe" set "APP=%~dp0build\SimpleScreen.exe"

if not defined APP (
    echo ERROR: Build completed, but SimpleScreen.exe was not found.
    goto :failed
)

for %%I in ("%CMAKE_PREFIX_PATH%") do set "QT_BIN=%%~fI\bin"
if exist "%QT_BIN%\windeployqt.exe" (
    echo.
    echo Copying Qt runtime files...
    "%QT_BIN%\windeployqt.exe" "%APP%" --release
    if errorlevel 1 echo WARNING: windeployqt reported a problem. The app may not launch.
)

echo.
echo Launching the window...
start "" "%APP%"
echo.
echo Done. You can run this batch file again whenever you want.
timeout /t 3 >nul
exit /b 0

:failed
echo.
echo BUILD OR LAUNCH FAILED.
echo Check the error messages above. Make sure Visual Studio C++ tools and Qt 6 Widgets are installed.
pause
exit /b 1
