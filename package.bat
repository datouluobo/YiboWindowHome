@echo off
setlocal
cd /d "%~dp0"
echo Building and testing YiboWindowHome Release...
echo Close YiboWindowHome before packaging if it is running.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\build.ps1"
if errorlevel 1 (
    echo.
    echo Packaging failed. See the messages above.
    pause
    exit /b 1
)
echo.
echo Packaging succeeded: %~dp0dist\YiboWindowHome.exe
start "" "%~dp0dist\YiboWindowHome.exe"
pause
exit /b 0
