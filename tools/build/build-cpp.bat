@echo off
setlocal EnableExtensions EnableDelayedExpansion

rem ======================================================================
rem  Fruity-Prime native C++ local build
rem  (CMakeLists.txt at repo root: src/NcsfPlay.Native + src/MphRead.Native)
rem
rem  Usage:
rem    tools\build\build-cpp.bat [toolchain] [config] [options...]
rem
rem    toolchain : auto (default), msys2, msvc
rem    config    : Release (default), Debug, RelWithDebInfo, MinSizeRel
rem    options   : deps       install dependencies first
rem                           (msys2 = pacman, msvc = vcpkg classic mode)
rem                clean      delete the build tree before configuring
rem                configure  run the CMake configure step only
rem                run        start the built game on Vulkan afterwards
rem                           (FruityPrime.exe -launcher -rhi vulkan)
rem
rem  Environment overrides:
rem    MSYS2_ROOT  MSYS2 install root            (default C:\msys64)
rem    MSYS2_ENV   ucrt64, mingw64 or clang64     (default: first one found)
rem    VCPKG_ROOT  vcpkg checkout, needed by msvc (same as the CI runner)
rem    BUILD_JOBS  parallel build jobs            (default NUMBER_OF_PROCESSORS)
rem
rem  Output: tools\build\out\<toolchain>-<config>\
rem ======================================================================

set "SCRIPT_DIR=%~dp0"
for %%I in ("%SCRIPT_DIR%..\..") do set "REPO_ROOT=%%~fI"
set "OUT_ROOT=%SCRIPT_DIR%out"

set "TOOLCHAIN=auto"
set "CONFIG=Release"
set "DO_DEPS=0"
set "DO_CLEAN=0"
set "CONFIGURE_ONLY=0"
set "DO_RUN=0"
if not defined BUILD_JOBS set "BUILD_JOBS=%NUMBER_OF_PROCESSORS%"
if not defined BUILD_JOBS set "BUILD_JOBS=4"

:parse
if "%~1"=="" goto :parsed
if /i "%~1"=="auto"           (set "TOOLCHAIN=auto"          & goto :next)
if /i "%~1"=="msys2"          (set "TOOLCHAIN=msys2"         & goto :next)
if /i "%~1"=="mingw"          (set "TOOLCHAIN=msys2"         & goto :next)
if /i "%~1"=="msvc"           (set "TOOLCHAIN=msvc"          & goto :next)
if /i "%~1"=="Release"        (set "CONFIG=Release"          & goto :next)
if /i "%~1"=="Debug"          (set "CONFIG=Debug"            & goto :next)
if /i "%~1"=="RelWithDebInfo" (set "CONFIG=RelWithDebInfo"   & goto :next)
if /i "%~1"=="MinSizeRel"     (set "CONFIG=MinSizeRel"       & goto :next)
if /i "%~1"=="deps"           (set "DO_DEPS=1"               & goto :next)
if /i "%~1"=="clean"          (set "DO_CLEAN=1"              & goto :next)
if /i "%~1"=="configure"      (set "CONFIGURE_ONLY=1"        & goto :next)
if /i "%~1"=="run"            (set "DO_RUN=1"                & goto :next)
if /i "%~1"=="help"   goto :usage
if /i "%~1"=="-h"     goto :usage
if /i "%~1"=="--help" goto :usage
echo [build] ERROR: unknown argument "%~1"
echo.
goto :usage_fail
:next
shift
goto :parse
:parsed

if not exist "%REPO_ROOT%\CMakeLists.txt" (
    echo [build] ERROR: CMakeLists.txt not found under "%REPO_ROOT%"
    exit /b 1
)

if not defined MSYS2_ROOT set "MSYS2_ROOT=C:\msys64"
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"

if /i "%TOOLCHAIN%"=="auto" call :detect_toolchain
if /i "%TOOLCHAIN%"=="msys2" goto :msys2
if /i "%TOOLCHAIN%"=="msvc"  goto :msvc
echo [build] ERROR: no usable toolchain was found.
echo [build]        Install MSYS2 at C:\msys64 and run "build-cpp.bat msys2 deps",
echo [build]        or install Visual Studio C++ tools plus vcpkg and use "build-cpp.bat msvc deps".
exit /b 1


rem ----------------------------------------------------------------------
rem  MSYS2 (MinGW-w64 GCC / Clang) + Ninja
rem ----------------------------------------------------------------------
:msys2
if not exist "!MSYS2_ROOT!\usr\bin\bash.exe" (
    echo [build] ERROR: MSYS2 not found at "!MSYS2_ROOT!". Set MSYS2_ROOT.
    exit /b 1
)
if not defined MSYS2_ENV (
    for %%E in (ucrt64 mingw64 clang64) do (
        if not defined MSYS2_ENV (
            if exist "!MSYS2_ROOT!\%%E\bin\cmake.exe" set "MSYS2_ENV=%%E"
        )
    )
)
if not defined MSYS2_ENV set "MSYS2_ENV=ucrt64"

set "PKG_COMPILER=gcc"
set "CXX_NAME=g++"
if /i "!MSYS2_ENV!"=="ucrt64"  set "PKG_PREFIX=mingw-w64-ucrt-x86_64"
if /i "!MSYS2_ENV!"=="mingw64" set "PKG_PREFIX=mingw-w64-x86_64"
if /i "!MSYS2_ENV!"=="clang64" (
    set "PKG_PREFIX=mingw-w64-clang-x86_64"
    set "PKG_COMPILER=clang"
    set "CXX_NAME=clang++"
)
if not defined PKG_PREFIX (
    echo [build] ERROR: unsupported MSYS2_ENV "!MSYS2_ENV!" ^(use ucrt64, mingw64 or clang64^)
    exit /b 1
)
set "P=!PKG_PREFIX!"
set "MSYS2_PACKAGES=!P!-!PKG_COMPILER! !P!-cmake !P!-ninja !P!-pkgconf !P!-zlib !P!-curl !P!-libarchive"

if "%DO_DEPS%"=="1" (
    rem Only what is missing: "pacman -S" on packages already installed
    rem pulls newer versions from the sync database, which is a partial
    rem upgrade (gcc 16 against an installed gcc-libs 14 is a conflict
    rem pacman refuses under --noconfirm). Anything missing is installed
    rem with a full -Syu, the one upgrade MSYS2 supports; --ask=4 accepts
    rem package replacements such as gcc-libs -> libgcc. Run twice, since
    rem an update of MSYS2's own core ends the first pass early.
    set "MISSING="
    for /f "usebackq delims=" %%M in (`""!MSYS2_ROOT!\usr\bin\bash.exe" -lc "pacman -T !MSYS2_PACKAGES!""`) do set "MISSING=!MISSING! %%M"
    if not defined MISSING (
        echo [build] MSYS2 packages already installed: !MSYS2_PACKAGES!
    ) else (
        echo [build] Installing MSYS2 packages:!MISSING! ^(with a full system upgrade^)
        "!MSYS2_ROOT!\usr\bin\bash.exe" -lc "pacman -Syu --needed --noconfirm --ask=4 !MSYS2_PACKAGES!"
        "!MSYS2_ROOT!\usr\bin\bash.exe" -lc "pacman -Syu --needed --noconfirm --ask=4 !MSYS2_PACKAGES!"
        if errorlevel 1 (
            echo [build] ERROR: pacman failed. Run "pacman -Syu" in an MSYS2 shell, then retry.
            exit /b 1
        )
    )
)

set "ENV_BIN=!MSYS2_ROOT!\!MSYS2_ENV!\bin"
set "CMAKE_EXE=!ENV_BIN!\cmake.exe"
if not exist "!CMAKE_EXE!" (
    echo [build] ERROR: "!CMAKE_EXE!" not found.
    echo [build]        Run "tools\build\build-cpp.bat msys2 deps" to install:
    echo [build]        !MSYS2_PACKAGES!
    exit /b 1
)
if not exist "!ENV_BIN!\!CXX_NAME!.exe" (
    echo [build] ERROR: "!ENV_BIN!\!CXX_NAME!.exe" not found. Run "build-cpp.bat msys2 deps".
    exit /b 1
)

rem The compiler and its runtime DLLs must come from the selected environment.
set "PATH=!ENV_BIN!;%PATH%"

if exist "!ENV_BIN!\ninja.exe" (
    set "GENERATOR=Ninja"
) else (
    set "GENERATOR=MinGW Makefiles"
)

set "BUILD_DIR=%OUT_ROOT%\msys2-!MSYS2_ENV!-%CONFIG%"
set "CONFIGURE_ARGS=-G "!GENERATOR!" -DCMAKE_BUILD_TYPE=%CONFIG% -DCMAKE_CXX_COMPILER=!CXX_NAME! -DFRUITY_REQUIRE_VULKAN=ON -DFRUITY_REQUIRE_SKIA_VULKAN=ON"
set "BUILD_ARGS="
goto :run


rem ----------------------------------------------------------------------
rem  MSVC + vcpkg (mirrors .github/workflows/native-cpp-windows.yml)
rem ----------------------------------------------------------------------
:msvc
if not exist "!VSWHERE!" (
    echo [build] ERROR: vswhere.exe not found. Visual Studio 2019 or later is required.
    exit /b 1
)
set "VS_PATH="
set "VS_VERSION="
for /f "usebackq tokens=*" %%i in (`"!VSWHERE!" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VS_PATH=%%i"
for /f "usebackq tokens=*" %%i in (`"!VSWHERE!" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationVersion`) do set "VS_VERSION=%%i"
if not defined VS_PATH (
    echo [build] ERROR: MSVC x86/x64 C++ tools were not found.
    exit /b 1
)
for /f "tokens=1 delims=." %%v in ("!VS_VERSION!") do set "VS_MAJOR=%%v"
set "GENERATOR="
if "!VS_MAJOR!"=="16" set "GENERATOR=Visual Studio 16 2019"
if "!VS_MAJOR!"=="17" set "GENERATOR=Visual Studio 17 2022"
if "!VS_MAJOR!"=="18" set "GENERATOR=Visual Studio 18 2026"
if not defined GENERATOR (
    echo [build] ERROR: unknown Visual Studio version "!VS_VERSION!".
    exit /b 1
)

if not defined VCPKG_ROOT if exist "C:\vcpkg\vcpkg.exe" set "VCPKG_ROOT=C:\vcpkg"
if not defined VCPKG_ROOT (
    echo [build] ERROR: VCPKG_ROOT is not set and C:\vcpkg does not exist.
    echo [build]        git clone https://github.com/microsoft/vcpkg C:\vcpkg
    echo [build]        C:\vcpkg\bootstrap-vcpkg.bat
    exit /b 1
)
if not exist "!VCPKG_ROOT!\vcpkg.exe" (
    echo [build] ERROR: "!VCPKG_ROOT!\vcpkg.exe" not found. Run bootstrap-vcpkg.bat first.
    exit /b 1
)

rem A clean PATH: an MSYS2 prefix on it makes find_package take MinGW
rem libraries (libopenal, Skia without Vulkan) into an MSVC link. English
rem compiler messages, and Ninja from the Visual Studio environment.
set "VSLANG=1033"
set "PATH=%SystemRoot%\system32;%SystemRoot%;%SystemRoot%\System32\WindowsPowerShell\v1.0"
for /d %%V in ("C:\VulkanSDK\*") do if exist "%%V\Bin\glslc.exe" set "PATH=!PATH!;%%V\Bin"
call "!VS_PATH!\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 (
    echo [build] ERROR: vcvars64.bat failed.
    exit /b 1
)
set "CMAKE_EXE="
for /f "delims=" %%c in ('where cmake 2^>nul') do if not defined CMAKE_EXE set "CMAKE_EXE=%%c"
if not defined CMAKE_EXE (
    echo [build] ERROR: cmake.exe not found in the Visual Studio environment.
    exit /b 1
)

rem The same package list as the Windows CI job: Skia with Vulkan, the
rem Vulkan loader, VMA and shaderc are what make -rhi vulkan work.
set "VCPKG_PACKAGES="
for /f "usebackq delims=" %%P in ("%REPO_ROOT%\tools\ci\dependencies\windows-game.txt") do set "VCPKG_PACKAGES=!VCPKG_PACKAGES! %%P"
if "%DO_DEPS%"=="1" (
    echo [build] Installing vcpkg packages:!VCPKG_PACKAGES!
    "!VCPKG_ROOT!\vcpkg.exe" install !VCPKG_PACKAGES! --classic
    if errorlevel 1 (
        echo [build] ERROR: vcpkg install failed.
        exit /b 1
    )
)

echo [build] Visual Studio: !VS_PATH! ^(!VS_VERSION!^)
set "GENERATOR=Ninja"
set "BUILD_DIR=%OUT_ROOT%\msvc-%CONFIG%"
set "CONFIGURE_ARGS=-G Ninja -DCMAKE_BUILD_TYPE=%CONFIG% -DCMAKE_TOOLCHAIN_FILE="!VCPKG_ROOT!\scripts\buildsystems\vcpkg.cmake" -DVCPKG_TARGET_TRIPLET=x64-windows -DVCPKG_MANIFEST_MODE=OFF -DFRUITY_REQUIRE_VULKAN=ON -DFRUITY_REQUIRE_SKIA_VULKAN=ON -DCMAKE_NINJA_CMCLDEPS_RC=OFF -DCMAKE_IGNORE_PREFIX_PATH=C:/msys64/mingw64;C:/msys64/ucrt64;C:/msys64/clang64 "-DCMAKE_CXX_FLAGS=/utf-8 /EHsc" "-DCMAKE_C_FLAGS=/utf-8""
set "BUILD_ARGS="
goto :run


rem ----------------------------------------------------------------------
rem  Configure and build
rem ----------------------------------------------------------------------
:run
echo [build] Repository : %REPO_ROOT%
echo [build] Toolchain  : %TOOLCHAIN%
echo [build] Config     : %CONFIG%
echo [build] Generator  : !GENERATOR!
echo [build] CMake      : !CMAKE_EXE!
echo [build] Build dir  : !BUILD_DIR!
echo.

if "%DO_CLEAN%"=="1" if exist "!BUILD_DIR!" (
    echo [build] Removing !BUILD_DIR!
    rmdir /s /q "!BUILD_DIR!"
)

"!CMAKE_EXE!" -S "%REPO_ROOT%" -B "!BUILD_DIR!" !CONFIGURE_ARGS!
if errorlevel 1 (
    echo.
    echo [build] ERROR: CMake configure failed.
    if /i "%TOOLCHAIN%"=="msys2" echo [build]        Missing packages? Try "tools\build\build-cpp.bat msys2 deps".
    if /i "%TOOLCHAIN%"=="msvc"  echo [build]        Missing packages? Try "tools\build\build-cpp.bat msvc deps".
    exit /b 1
)
if "%CONFIGURE_ONLY%"=="1" (
    echo [build] Configure finished.
    exit /b 0
)

"!CMAKE_EXE!" --build "!BUILD_DIR!" !BUILD_ARGS! --parallel %BUILD_JOBS%
if errorlevel 1 (
    echo.
    echo [build] ERROR: build failed.
    exit /b 1
)

echo.
echo [build] Build succeeded:
dir /b "%BUILD_DIR%\FruityPrime.exe" 2>nul
dir /s /b "%BUILD_DIR%\*.a" "%BUILD_DIR%\*.lib" 2>nul
if "%DO_RUN%"=="1" goto :launch
exit /b 0


rem ----------------------------------------------------------------------
rem  Start the game on Vulkan
rem ----------------------------------------------------------------------
:launch
rem paths.txt (where the extracted game files are) has to sit beside the
rem exe. A fresh build folder has none: take the one another build uses.
if not exist "%BUILD_DIR%\paths.txt" (
    for /d %%D in ("%OUT_ROOT%\*") do (
        if not exist "%BUILD_DIR%\paths.txt" if exist "%%D\paths.txt" (
            copy /y "%%D\paths.txt" "%BUILD_DIR%\paths.txt" >nul
            echo [build] paths.txt copied from %%D
        )
    )
)
if not exist "%BUILD_DIR%\paths.txt" echo [build] No paths.txt yet: the game will ask for the game files.
echo [build] Starting %BUILD_DIR%\FruityPrime.exe -launcher -rhi vulkan
start "" /d "%BUILD_DIR%" "%BUILD_DIR%\FruityPrime.exe" -launcher -rhi vulkan
exit /b 0


rem ----------------------------------------------------------------------
:detect_toolchain
rem MSVC first: it is the toolchain with a Skia that has Vulkan (vcpkg).
if exist "!VSWHERE!" (
    if defined VCPKG_ROOT set "TOOLCHAIN=msvc"
    if exist "C:\vcpkg\vcpkg.exe" set "TOOLCHAIN=msvc"
    if /i "!TOOLCHAIN!"=="msvc" goto :eof
)
for %%E in (ucrt64 mingw64 clang64) do (
    if exist "!MSYS2_ROOT!\%%E\bin\cmake.exe" (
        set "TOOLCHAIN=msys2"
        goto :eof
    )
)
goto :eof


:usage
call :print_usage
exit /b 0
:usage_fail
call :print_usage
exit /b 1
:print_usage
echo Usage: tools\build\build-cpp.bat [auto^|msys2^|msvc] [Release^|Debug^|RelWithDebInfo^|MinSizeRel] [deps] [clean] [configure]
echo.
echo   First time with MSYS2 : tools\build\build-cpp.bat msys2 deps
echo   First time with MSVC  : set VCPKG_ROOT=C:\vcpkg  then  tools\build\build-cpp.bat msvc deps
echo   Rebuild from scratch  : tools\build\build-cpp.bat clean
echo   Build and play on Vulkan: tools\build\build-cpp.bat run
echo.
echo   Output goes to tools\build\out\
goto :eof
