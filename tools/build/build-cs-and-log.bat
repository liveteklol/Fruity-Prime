@echo off
setlocal EnableExtensions
rem Double-click helper: runs the non-Android C# build and writes the full output to
rem tools\build\out\build-cs.log. With no arguments it builds Release.
set "SCRIPT_DIR=%~dp0"
if not exist "%SCRIPT_DIR%out" mkdir "%SCRIPT_DIR%out"
set "LOG=%SCRIPT_DIR%out\build-cs.log"
echo Building C# solution... (log: %LOG%)
call "%SCRIPT_DIR%build-cs.bat" %* > "%LOG%" 2>&1
set "RC=%ERRORLEVEL%"
>> "%LOG%" echo exit code: %RC%
powershell -NoProfile -Command "Get-Content -Tail 40 '%LOG%'"
echo.
echo exit code: %RC%
pause
exit /b %RC%
