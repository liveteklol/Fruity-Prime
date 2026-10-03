@echo off
setlocal EnableExtensions
rem Double-click helper: runs build-cpp.bat and writes the full output to
rem tools\build\out\build-cpp.log. With no arguments it installs dependencies, builds, and starts the game on Vulkan.
set "SCRIPT_DIR=%~dp0"
if not exist "%SCRIPT_DIR%out" mkdir "%SCRIPT_DIR%out"
set "LOG=%SCRIPT_DIR%out\build-cpp.log"
set "ARGS=%*"
if "%ARGS%"=="" set "ARGS=deps run"
echo Building... (log: %LOG%)
call "%SCRIPT_DIR%build-cpp.bat" %ARGS% > "%LOG%" 2>&1
set "RC=%ERRORLEVEL%"
rem Redirect first: "%RC%>>" would read a 1 as the stdout handle.
>> "%LOG%" echo exit code: %RC%
if not "%RC%"=="0" >> "%LOG%" echo BUILD FAILED: the FruityPrime.exe in tools\build\out is from an earlier build.
powershell -NoProfile -Command "Get-Content -Tail 40 '%LOG%'"
echo.
echo exit code: %RC%
pause
exit /b %RC%
