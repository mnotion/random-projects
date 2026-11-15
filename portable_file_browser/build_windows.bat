@echo off
setlocal EnableExtensions
title Portable File Browser - Build
pushd "%~dp0" || exit /b 1

rem Keep pip from silently waiting on a network retry for minutes.
set "PIP_DISABLE_PIP_VERSION_CHECK=1"
set "PIP_DEFAULT_TIMEOUT=30"
set "PIP_RETRIES=1"

echo [1/6] Checking Python...
where py >nul 2>nul
if not errorlevel 1 (
    set "PYTHON=py -3"
) else (
    where python >nul 2>nul
    if errorlevel 1 (
        echo ERROR: Python 3 is required on the build computer only.
        echo Install Python 3 and rerun this script.
        goto :failed
    )
    set "PYTHON=python"
)

echo [2/6] Creating build environment (first run only)...
if not exist ".venv-build\Scripts\python.exe" (
    %PYTHON% -m venv .venv-build
    if errorlevel 1 goto :failed
)

echo [3/6] Installing PyInstaller...
call ".venv-build\Scripts\python.exe" -m pip install --disable-pip-version-check --timeout 30 --retries 1 -r requirements-build.txt
if errorlevel 1 goto :failed

echo [4/6] Running tests...
call ".venv-build\Scripts\python.exe" -u -m unittest discover -s tests -v
if errorlevel 1 goto :failed

echo [5/6] Packaging EXE (this can take a few minutes)...
call ".venv-build\Scripts\python.exe" -u -m PyInstaller --noconfirm --clean --onefile --windowed --name FileBrowser --distpath dist --workpath build app.py
if errorlevel 1 goto :failed

echo [6/6] Creating portable data folders...
if not exist "dist\stuff\backups" mkdir "dist\stuff\backups"
if errorlevel 1 goto :failed

echo.
echo SUCCESS: "%CD%\dist\FileBrowser.exe"
echo Keep the stuff folder beside FileBrowser.exe when moving files/backups.
echo The EXE does not require Python on the target PC.
popd
echo.
pause
exit /b 0

:failed
echo.
echo BUILD FAILED (error %errorlevel%).
echo Check the error messages above. Build output is in the "build" folder.
popd
echo.
pause
exit /b 1
