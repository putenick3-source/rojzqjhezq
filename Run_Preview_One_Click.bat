@echo off
setlocal
title RobRun Overlay Preview
cd /d "%~dp0"
echo ==========================================
echo       ROBRUN OVERLAY - ONE CLICK
echo ==========================================
where py >nul 2>nul
if %errorlevel%==0 (
  py -3 preview.py
  goto :end
)
where python >nul 2>nul
if %errorlevel%==0 (
  python preview.py
  goto :end
)
echo Python 3 was not found.
echo Install Python 3 from https://www.python.org/downloads/
echo During setup, enable "Add Python to PATH".
pause
:end
endlocal
