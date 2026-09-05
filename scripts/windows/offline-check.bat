@echo off
setlocal
cd /d "%~dp0\..\.."
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0offline-check.ps1" %*
set ERR=%ERRORLEVEL%
echo.
if not "%ERR%"=="0" (
  echo RTXMac offline check failed with exit code %ERR%.
) else (
  echo RTXMac offline check completed successfully.
)
pause
exit /b %ERR%
