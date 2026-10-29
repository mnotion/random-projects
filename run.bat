@echo off
setlocal
cd /d "%~dp0"

where py >nul 2>nul
if errorlevel 1 (
    echo Python launcher not found. Install Python 3.11 or newer from https://www.python.org/downloads/
    echo During installation, enable the option to add Python to PATH.
    pause
    exit /b 1
)

if not exist ".venv\Scripts\python.exe" (
    py -3 -m venv .venv
    if errorlevel 1 goto failed
    ".venv\Scripts\python.exe" -m pip install --upgrade pip
    if errorlevel 1 goto failed
    ".venv\Scripts\python.exe" -m pip install -r requirements.txt
    if errorlevel 1 goto failed
)

".venv\Scripts\python.exe" pdf_word_converter.py
if errorlevel 1 goto failed
exit /b 0

:failed
echo.
echo Setup or launch failed. Check the message above.
pause
exit /b 1
