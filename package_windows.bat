@echo off
setlocal EnableExtensions

rem Package an already-built PageConvert.exe with its Qt runtime dependencies.
rem Run this script in a command prompt configured for the same Qt MinGW kit
rem used to build PageConvert, so windeployqt and the compiler runtime are available.

if "%~1"=="" (
    echo Usage:
    echo   package_windows.bat "C:\path\to\PageConvert.exe"
    echo.
    echo Build PageConvert with the Release configuration in Qt Creator first.
    echo Then pass the full path to that Release executable.
    exit /b 2
)

set "SOURCE_EXE=%~f1"
if not exist "%SOURCE_EXE%" (
    echo ERROR: Executable not found:
    echo "%SOURCE_EXE%"
    exit /b 2
)

where windeployqt.exe >nul 2>&1
if errorlevel 1 (
    echo ERROR: windeployqt.exe is not on PATH.
    echo Open the command prompt for the same Qt kit, or run that kit's qtenv2.bat,
    echo then run this script again.
    exit /b 2
)

set "OUT_DIR=%~dp0dist\PageConvert"
if not exist "%OUT_DIR%" mkdir "%OUT_DIR%"
if errorlevel 1 (
    echo ERROR: Could not create output directory:
    echo "%OUT_DIR%"
    exit /b 2
)

copy /Y "%SOURCE_EXE%" "%OUT_DIR%\PageConvert.exe" >nul
if errorlevel 1 (
    echo ERROR: Could not copy the executable.
    exit /b 2
)

echo Deploying Qt libraries and plugins...
windeployqt.exe --release --compiler-runtime --no-translations "%OUT_DIR%\PageConvert.exe"
if errorlevel 1 (
    echo.
    echo ERROR: windeployqt failed. Make sure this command prompt is configured
    echo for the same Qt kit and compiler used to build the Release executable.
    exit /b 1
)

echo.
echo Deployment folder created:
echo "%OUT_DIR%"
echo Copy the entire PageConvert folder to the other Windows computer.
echo Do not copy only the EXE: it needs the DLLs and plugins beside it.
echo.
echo NOTE: Word-to-PDF conversion still requires LibreOffice on the target PC.
exit /b 0
