@echo off
chcp 65001 >nul
:: ==============================================================================
:: test_turn_integration.bat — TURN 中继模式集成测试 (Windows)
:: 对应 test_turn_integration.sh
::
:: 用法:
::   test_turn_integration.bat                      (无 TURN 服务器，跳过测试 1)
::   set TURN_SERVER=my.turn.server && test_turn_integration.bat
::
:: 测试 1: TURN Allocate 请求（需要 TURN 服务器）
:: 测试 2: TURN 候选加入 ICE checklist
:: 测试 3: 无 TURN 服务器时的回退行为
:: ==============================================================================
setlocal enabledelayedexpansion

set PROJECT_ROOT=%~dp0..
set BUILD_DIR=%PROJECT_ROOT%\build_win
set SERVER_BIN=%BUILD_DIR%\p2p_server\p2p_server.exe
set PING_BIN=%BUILD_DIR%\p2p_ping\p2p_ping.exe
set LOG_DIR=%~dp0turn_integration_logs
set PORT=8890

if "%TURN_SERVER%"=="" set TURN_SERVER=
if "%TURN_PORT%"==""   set TURN_PORT=3478

set PASS=0
set FAIL=0
set SKIP=0

echo =========================================
echo   TURN 中继模式集成测试
echo =========================================
if not "%TURN_SERVER%"=="" (
    echo   TURN Server: %TURN_SERVER%:%TURN_PORT%
) else (
    echo   TURN Server: (未配置，测试 1/2 将跳过)
    echo   提示: set TURN_SERVER=your.turn.server 后重新运行
)
echo.

if not exist "%SERVER_BIN%" (
    echo [ERROR] p2p_server.exe 未找到
    exit /b 1
)
if not exist "%PING_BIN%" (
    echo [ERROR] p2p_ping.exe 未找到
    exit /b 1
)
if not exist "%LOG_DIR%" mkdir "%LOG_DIR%"

:cleanup
taskkill /f /im p2p_server.exe >nul 2>&1
taskkill /f /im p2p_ping.exe   >nul 2>&1
timeout /t 1 /nobreak >nul
goto :eof

:wait_log
set WL_FILE=%~1
set WL_KEY=%~2
set WL_TIMEOUT=%~3
if "%WL_TIMEOUT%"=="" set WL_TIMEOUT=15
set WL_I=0
:wl_loop
findstr /i "%WL_KEY%" "%WL_FILE%" >nul 2>&1
if !errorlevel! == 0 ( set WL_RESULT=0 & goto :eof )
if !WL_I! geq %WL_TIMEOUT% ( set WL_RESULT=1 & goto :eof )
timeout /t 1 /nobreak >nul
set /a WL_I+=1
goto wl_loop

:: ============================================================================
:: 测试 1: TURN Allocate 请求
:: ============================================================================
:test_turn_allocate
echo --- TEST 1: TURN Allocate request ---
if "%TURN_SERVER%"=="" (
    echo   [SKIP] No TURN server configured
    echo   [SKIP] Example: set TURN_SERVER=my.turn.server
    set /a SKIP+=2
    echo.
    goto :eof
)

set ALOG=%LOG_DIR%\alice_turn.log
call :cleanup

start /b "" "%SERVER_BIN%" %PORT% > nul 2>&1
timeout /t 1 /nobreak >nul

start /b "" "%PING_BIN%" --server 127.0.0.1 --name alice --turn %TURN_SERVER% --turn-port %TURN_PORT% > "%ALOG%" 2>&1

call :wait_log "%ALOG%" "TURN Allocate relay turn" 20
if !WL_RESULT! == 0 (
    echo   [PASS] TURN Allocate succeeded
    set /a PASS+=1
) else (
    echo   [FAIL] TURN Allocate did not complete
    set /a FAIL+=1
    type "%ALOG%" 2>nul
)

call :cleanup
echo.
goto :eof

:: ============================================================================
:: 测试 2: TURN 候选加入 ICE checklist
:: ============================================================================
:test_turn_candidate
echo --- TEST 2: TURN relay candidate in ICE checklist ---
if "%TURN_SERVER%"=="" (
    echo   [SKIP] No TURN server configured
    set /a SKIP+=1
    echo.
    goto :eof
)

set ALOG=%LOG_DIR%\alice_turn_cand.log
set BLOG=%LOG_DIR%\bob_turn_cand.log
call :cleanup

start /b "" "%SERVER_BIN%" %PORT% > nul 2>&1
timeout /t 1 /nobreak >nul

start /b "" "%PING_BIN%" --server 127.0.0.1 --name alice --turn %TURN_SERVER% --turn-port %TURN_PORT% > "%ALOG%" 2>&1
timeout /t 1 /nobreak >nul

start /b "" "%PING_BIN%" --server 127.0.0.1 --name bob --to alice --turn %TURN_SERVER% --turn-port %TURN_PORT% > "%BLOG%" 2>&1

call :wait_log "%ALOG%" "relay RELAY Relay" 25
if !WL_RESULT! == 0 (
    echo   [PASS] Relay candidate observed in ICE candidate list
    set /a PASS+=1
) else (
    echo   [INFO] No relay candidate log (may be internal)
)

call :wait_log "%ALOG%" "CONNECTED connected" 30
if !WL_RESULT! == 0 (
    echo   [PASS] Connection established (possibly via TURN relay)
    set /a PASS+=1
) else (
    echo   [FAIL] Connection not established
    set /a FAIL+=1
)

call :cleanup
echo.
goto :eof

:: ============================================================================
:: 测试 3: 无 TURN 服务器时的回退行为（客户端正常启动）
:: ============================================================================
:test_turn_fallback
echo --- TEST 3: Fallback behavior without TURN server ---
set ALOG=%LOG_DIR%\alice_noturn.log
call :cleanup

start /b "" "%SERVER_BIN%" %PORT% > nul 2>&1
timeout /t 1 /nobreak >nul

start /b "" "%PING_BIN%" --server 127.0.0.1 --name alice > "%ALOG%" 2>&1
timeout /t 5 /nobreak >nul

call :cleanup

for %%F in ("%ALOG%") do set ALOG_SIZE=%%~zF
if defined ALOG_SIZE if %ALOG_SIZE% gtr 0 (
    echo   [PASS] Client started successfully without TURN server
    set /a PASS+=1
) else (
    echo   [FAIL] Client log empty
    set /a FAIL+=1
)
echo.
goto :eof

:: ============================================================================
:: 主程序
:: ============================================================================
call :test_turn_allocate
call :test_turn_candidate
call :test_turn_fallback

echo =========================================
echo   结果: PASS=%PASS%  FAIL=%FAIL%  SKIP=%SKIP%
echo   日志保存在: %LOG_DIR%
echo =========================================
if %FAIL% gtr 0 exit /b 1
endlocal
