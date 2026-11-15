@echo off
setlocal EnableExtensions
pushd "%~dp0"

where py >nul 2>nul
if not errorlevel 1 (
    set "PYTHON=py -3"
) else (
    where python >nul 2>nul
    if errorlevel 1 (
        echo Python 3 is required on the build computer only.
        echo The finished FileBrowser.exe does not require Python.
        popd
        exit /b 1
    )
    set "PYTHON=python"
)

if not exist ".venv-build\Scripts\python.exe" (
    %PYTHON% -m venv .venv-build
    if errorlevel 1 goto :failed
)

call ".venv-build\Scripts\activate.bat"
python -m pip install --upgrade pip
if errorlevel 1 goto :failed
python -m pip install -r requirements-build.txt
if errorlevel 1 goto :failed

python -m unittest discover -s tests -v
if errorlevel 1 goto :failed

python -m PyInstaller --noconfirm --clean --onefile --windowed --name FileBrowser --distpath dist --workpath build app.py
if errorlevel 1 goto :failed

if not exist "dist\stuff\backups" mkdir "dist\stuff\backups"
if errorlevel 1 goto :failed

echo.
echo SUCCESS: dist\FileBrowser.exe
echo The EXE is self-contained. It does not need Python or extra libraries on the target Windows PC.
echo Keep the stuff folder beside FileBrowser.exe when moving existing files and backups.
echo The app creates stuff and stuff\backups automatically if they do not exist.
popd
exit /b 0

:failed
echo.
echo Build failed. Read the error above.
popd
exit /b 1
