@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\start.ps1" -Stop
set "TEST_EXIT=%ERRORLEVEL%"
if not "%TEST_EXIT%"=="0" pause
exit /b %TEST_EXIT%
