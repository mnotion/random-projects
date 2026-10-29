@echo off
setlocal
cd /d "%~dp0"

where cmake >nul 2>nul
if errorlevel 1 (
    echo CMake was not found. Install CMake and add it to PATH.
    pause
    exit /b 1
)

echo Building PageConvert...
cmake -S . -B build
if errorlevel 1 (
    echo.
    echo CMake configuration failed. Check that Qt 6.5+ including the Qt PDF module
    echo and a compatible C++ compiler are installed.
    echo Set CMAKE_PREFIX_PATH to your Qt kit directory if CMake cannot find Qt6.
    pause
    exit /b 1
)

cmake --build build --config Release
if errorlevel 1 (
    echo.
    echo Build failed. Check the compiler output above.
    pause
    exit /b 1
)

if exist "build\Release\PageConvert.exe" (
    start "" "build\Release\PageConvert.exe"
    exit /b 0
)
if exist "build\PageConvert.exe" (
    start "" "build\PageConvert.exe"
    exit /b 0
)
if exist "build\bin\PageConvert.exe" (
    start "" "build\bin\PageConvert.exe"
    exit /b 0
)

echo The build completed but PageConvert.exe was not found in the usual output folders.
echo Open the build folder and launch the executable from your active compiler kit.
pause
