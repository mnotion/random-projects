@echo off
setlocal
title PageConvert - Create Portable App
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0make_portable.ps1"
if errorlevel 1 (
    echo.
    echo Packaging did not finish. Read the error above.
) 
echo.
pause
