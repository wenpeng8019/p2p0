@echo off
chcp 65001 >nul
setlocal EnableDelayedExpansion

:: ============================================================
:: build.bat - P2P project build script (Windows / MSVC)
::
:: Usage:
::   build.bat [target] [options]
::
:: Targets:
::   all        Build everything: library + p2p_ping + p2p_server (default)
::   lib        Build static library only (p2p_static)
::   ping       Build p2p_ping only  (implies lib)
::   server     Build p2p_server only (implies lib)
::   clean      Clean build output
::
:: Options:
::   --build-dir <dir>   CMake build directory (default: build_win)
::   --config <cfg>      Build configuration: Debug or Release (default: Debug)
::   --help              Show this help
::
:: Examples:
::   build.bat
::   build.bat all
::   build.bat ping
::   build.bat server --config Release
::   build.bat clean
::   build.bat all --build-dir build_win_rel --config Release
:: ============================================================

set BUILD_TARGET=all
set BUILD_DIR=build_win
set BUILD_CONFIG=Debug

:: Parse arguments
:parse_args
if "%~1"=="" goto done_args
if /i "%~1"=="--help"        goto show_help
if /i "%~1"=="-h"            goto show_help
if /i "%~1"=="help"          goto show_help
if /i "%~1"=="all"           ( set "BUILD_TARGET=all"    & shift & goto parse_args )
if /i "%~1"=="lib"           ( set "BUILD_TARGET=lib"    & shift & goto parse_args )
if /i "%~1"=="ping"          ( set "BUILD_TARGET=ping"   & shift & goto parse_args )
if /i "%~1"=="server"        ( set "BUILD_TARGET=server" & shift & goto parse_args )
if /i "%~1"=="clean"         ( set "BUILD_TARGET=clean"  & shift & goto parse_args )
if /i "%~1"=="--build-dir"   ( set "BUILD_DIR=%~2"       & shift & shift & goto parse_args )
if /i "%~1"=="--config"      ( set "BUILD_CONFIG=%~2"    & shift & shift & goto parse_args )
echo [WARN] Unknown argument: %~1
shift
goto parse_args
:done_args

:: ---- locate vcvarsall ----
set VCVARS=
for %%V in (
    "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvarsall.bat"
    "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvarsall.bat"
    "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat"
    "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat"
    "C:\Program Files\Microsoft Visual Studio\2019\Enterprise\VC\Auxiliary\Build\vcvarsall.bat"
    "C:\Program Files\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvarsall.bat"
    "C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvarsall.bat"
) do (
    if exist %%V (
        set VCVARS=%%~V
        goto found_vc
    )
)
echo [ERROR] Visual Studio / Build Tools not found.
echo         Install VS 2019/2022 with "Desktop development with C++" workload.
exit /b 1
:found_vc

:: ---- init VS environment ----
echo [INFO] VS environment: %VCVARS%
call "%VCVARS%" x64 >nul 2>&1
if errorlevel 1 (
    echo [ERROR] Failed to initialize VS environment.
    exit /b 1
)

:: ---- locate / configure CMake build dir ----
set ROOT=%~dp0
cd /d "%ROOT%"

if not exist "%BUILD_DIR%\CMakeCache.txt" (
    echo [INFO] Configuring CMake in %BUILD_DIR% ...
    cmake -S . -B "%BUILD_DIR%" -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=%BUILD_CONFIG%
    if errorlevel 1 ( echo [ERROR] CMake configure failed. & exit /b 1 )
)

:: ---- clean ----
if /i "%BUILD_TARGET%"=="clean" (
    echo [INFO] Cleaning %BUILD_DIR% ...
    cmake --build "%BUILD_DIR%" --target clean
    exit /b %errorlevel%
)

:: ---- build ----
if /i "%BUILD_TARGET%"=="all" (
    echo [INFO] Building: p2p_static + p2p_ping + p2p_server [%BUILD_CONFIG%]
    cmake --build "%BUILD_DIR%" --config %BUILD_CONFIG%
    goto check_result
)

if /i "%BUILD_TARGET%"=="lib" (
    echo [INFO] Building: p2p_static [%BUILD_CONFIG%]
    cmake --build "%BUILD_DIR%" --config %BUILD_CONFIG% --target p2p_static
    goto check_result
)

if /i "%BUILD_TARGET%"=="ping" (
    echo [INFO] Building: p2p_ping [%BUILD_CONFIG%]
    cmake --build "%BUILD_DIR%" --config %BUILD_CONFIG% --target p2p_ping
    goto check_result
)

if /i "%BUILD_TARGET%"=="server" (
    echo [INFO] Building: p2p_server [%BUILD_CONFIG%]
    cmake --build "%BUILD_DIR%" --config %BUILD_CONFIG% --target p2p_server
    goto check_result
)

:check_result
if errorlevel 1 (
    echo.
    echo [FAIL] Build failed.
    exit /b 1
)
echo.
echo [OK] Build succeeded.
echo      Build dir : %BUILD_DIR%
echo      Binaries  :
if /i "%BUILD_TARGET%"=="all" (
    echo        %BUILD_DIR%\p2p_ping\p2p_ping.exe
    echo        %BUILD_DIR%\p2p_server\p2p_server.exe
)
if /i "%BUILD_TARGET%"=="ping"   echo        %BUILD_DIR%\p2p_ping\p2p_ping.exe
if /i "%BUILD_TARGET%"=="server" echo        %BUILD_DIR%\p2p_server\p2p_server.exe
if /i "%BUILD_TARGET%"=="lib"    echo        %BUILD_DIR%\p2p_static.lib
exit /b 0

:show_help
echo.
echo  build.bat - P2P project build script (Windows / MSVC)
echo.
echo  Usage:
echo    build.bat [target] [options]
echo.
echo  Targets:
echo    all        Build everything: library + p2p_ping + p2p_server  (default)
echo    lib        Build static library only (p2p_static)
echo    ping       Build p2p_ping only  (implies lib)
echo    server     Build p2p_server only (implies lib)
echo    clean      Clean build output
echo.
echo  Options:
echo    --build-dir ^<dir^>   CMake build directory  (default: build_win)
echo    --config ^<cfg^>      Debug ^| Release         (default: Debug)
echo    --help               Show this help
echo.
echo  Examples:
echo    build.bat
echo    build.bat all
echo    build.bat ping
echo    build.bat server --config Release
echo    build.bat clean
echo    build.bat all --build-dir build_release --config Release
echo.
exit /b 0
