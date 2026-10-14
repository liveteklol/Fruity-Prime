@echo off
setlocal
rem Defaults: Kanden, seven AI clients, 250 ms bot RTT, Data Shrine.
rem Run with "stop" to stop this launcher's session.
if /i "%~1"=="stop" (
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\start.ps1" -Stop
) else (
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\start.ps1" %*
)
set "TEST_EXIT=%ERRORLEVEL%"
if not "%TEST_EXIT%"=="0" pause
exit /b %TEST_EXIT%
