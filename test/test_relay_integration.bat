@echo off
chcp 65001 >nul
:: ==============================================================================
:: test_relay_integration.bat - RELAY mode integration tests (Windows)
::
:: Usage: test_relay_integration.bat [scenario]
::   scenario: all | online | offline | trickle
::   default: online
::
:: Test scenarios:
::   online   - Both peers online, direct OFFER/FORWARD relay
::   offline  - Peer offline candidate caching (Bob sends first, Alice comes online later)
::   trickle  - Trickle ICE (both online, STUN candidates added after initial connect)
:: ==============================================================================
setlocal enabledelayedexpansion

set PROJECT_ROOT=%~dp0..
set BUILD_DIR=%PROJECT_ROOT%\build_win
set SERVER_BIN=%BUILD_DIR%\p2p_server\p2p_server.exe
set PING_BIN=%BUILD_DIR%\p2p_ping\p2p_ping.exe
set LOG_DIR=%~dp0relay_logs
set PORT=8888
set HOST=127.0.0.1
set PASS_COUNT=0
set FAIL_COUNT=0

set SCENARIO=%~1
if "%SCENARIO%"=="" set SCENARIO=online

echo =========================================
echo   RELAY Mode Integration Test [%SCENARIO%]
echo =========================================
echo.

if not exist "%SERVER_BIN%" (
    echo [ERROR] p2p_server.exe not found: %SERVER_BIN%
    exit /b 1
)
if not exist "%PING_BIN%" (
    echo [ERROR] p2p_ping.exe not found: %PING_BIN%
    exit /b 1
)

if not exist "%LOG_DIR%" mkdir "%LOG_DIR%"

if /i "%SCENARIO%"=="online"  goto run_online
if /i "%SCENARIO%"=="offline" goto run_offline
if /i "%SCENARIO%"=="trickle" goto run_trickle
if /i "%SCENARIO%"=="all"     goto run_all

echo [ERROR] Unknown scenario: %SCENARIO%
echo Usage: %~nx0 [all^|online^|offline^|trickle]
exit /b 1

:: ============================================================================
:: Helper: kill test processes
:: ============================================================================
:cleanup
taskkill /f /im p2p_server.exe >nul 2>&1
taskkill /f /im p2p_ping.exe   >nul 2>&1
timeout /t 1 /nobreak >nul
goto :eof

:: ============================================================================
:: Helper: start relay server
:: ============================================================================
:start_server
echo [INFO] Starting Relay server (port %PORT%)...
call :cleanup
start /b "" "%SERVER_BIN%" %PORT% relay > "%LOG_DIR%\server_%SCENARIO%.log" 2>&1
timeout /t 2 /nobreak >nul
echo [OK]  Server started
goto :eof

:: ============================================================================
:: Helper: check log for keyword
:: ============================================================================
:check_log
findstr /i /c:"%~2" "%~1" >nul 2>&1
if !errorlevel! == 0 (
    echo [PASS] %~3
    set /a PASS_COUNT+=1
) else (
    echo [FAIL] %~3  (keyword: '%~2')
    set /a FAIL_COUNT+=1
)
goto :eof

:: ============================================================================
:: Helper: wait for keyword in log (up to N seconds)
:: ============================================================================
:wait_log
set WL_FILE=%~1
set WL_KEY=%~2
set WL_TIMEOUT=%~3
if "%WL_TIMEOUT%"=="" set WL_TIMEOUT=20
set WL_I=0
:wait_log_loop
findstr /i /c:"%WL_KEY%" "%WL_FILE%" >nul 2>&1
if !errorlevel! == 0 (
    echo [OK]  Found '%WL_KEY%' after !WL_I!s in %~nx1
    set WL_RESULT=0
    goto :eof
)
if !WL_I! geq %WL_TIMEOUT% (
    echo [WARN] Timeout ^(%WL_TIMEOUT%s^): '%WL_KEY%' not found in %~nx1
    set WL_RESULT=1
    goto :eof
)
timeout /t 1 /nobreak >nul
set /a WL_I+=1
goto wait_log_loop

:: ============================================================================
:: Scenario: online - both peers online, direct relay
:: ============================================================================
:run_online
set SCENARIO=online
set PASS_COUNT=0
set FAIL_COUNT=0
echo --- Scenario: online (both peers online) ---
echo.

call :start_server

echo [INFO] Starting Alice (passive, relay mode)...
start /b "" "%PING_BIN%" --server %HOST% --name alice > "%LOG_DIR%\alice_%SCENARIO%.log" 2>&1
timeout /t 2 /nobreak >nul

echo [INFO] Starting Bob (active, connecting to alice)...
start /b "" "%PING_BIN%" --server %HOST% --name bob --to alice > "%LOG_DIR%\bob_%SCENARIO%.log" 2>&1

echo [INFO] Waiting up to 30 seconds for CONNECTED...
call :wait_log "%LOG_DIR%\alice_%SCENARIO%.log" "CONNECTED" 30
call :wait_log "%LOG_DIR%\bob_%SCENARIO%.log"   "CONNECTED" 15

call :check_log "%LOG_DIR%\alice_%SCENARIO%.log" "CONNECTED" "Alice reached CONNECTED"
call :check_log "%LOG_DIR%\bob_%SCENARIO%.log"   "CONNECTED" "Bob reached CONNECTED"

echo.
echo === Server Log ===
type "%LOG_DIR%\server_%SCENARIO%.log" 2>nul
echo.
echo === Alice Log ===
type "%LOG_DIR%\alice_%SCENARIO%.log" 2>nul
echo.
echo === Bob Log ===
type "%LOG_DIR%\bob_%SCENARIO%.log" 2>nul
echo.

echo PASS=%PASS_COUNT%  FAIL=%FAIL_COUNT%
echo Logs: %LOG_DIR%
echo =========================================

call :cleanup
if "%RUNNING_ALL%"=="" ( if %FAIL_COUNT% gtr 0 ( exit /b 1 ) else ( exit /b 0 ) )
goto :eof

:: ============================================================================
:: Scenario: offline - Bob sends candidates, Alice comes online later
:: ============================================================================
:run_offline
set SCENARIO=offline
set PASS_COUNT=0
set FAIL_COUNT=0
echo --- Scenario: offline (Bob caches candidates, Alice joins later) ---
echo.

call :start_server

echo [INFO] Starting Bob only (Alice is offline)...
start /b "" "%PING_BIN%" --server %HOST% --name bob --to alice > "%LOG_DIR%\bob_%SCENARIO%.log" 2>&1
echo [INFO] Waiting 8 seconds for Bob to register and cache candidates...
timeout /t 8 /nobreak >nul

call :check_log "%LOG_DIR%\server_%SCENARIO%.log" "Cached\|offline\|CONNECT_ACK" "Server receives Bob candidates"

echo [INFO] Starting Alice (comes online - should receive cached OFFER)...
start /b "" "%PING_BIN%" --server %HOST% --name alice > "%LOG_DIR%\alice_%SCENARIO%.log" 2>&1

echo [INFO] Waiting 25 seconds for connection...
call :wait_log "%LOG_DIR%\alice_%SCENARIO%.log" "CONNECTED" 25
call :wait_log "%LOG_DIR%\bob_%SCENARIO%.log"   "CONNECTED" 15

call :check_log "%LOG_DIR%\alice_%SCENARIO%.log" "CONNECTED" "Alice reached CONNECTED (offline cache)"
call :check_log "%LOG_DIR%\bob_%SCENARIO%.log"   "CONNECTED" "Bob reached CONNECTED"

echo.
echo === Server Log ===
type "%LOG_DIR%\server_%SCENARIO%.log" 2>nul
echo.
echo PASS=%PASS_COUNT%  FAIL=%FAIL_COUNT%
echo Logs: %LOG_DIR%
echo =========================================

call :cleanup
if "%RUNNING_ALL%"=="" ( if %FAIL_COUNT% gtr 0 ( exit /b 1 ) else ( exit /b 0 ) )
goto :eof

:: ============================================================================
:: Scenario: trickle - both online, trickle ICE
:: ============================================================================
:run_trickle
set SCENARIO=trickle
set PASS_COUNT=0
set FAIL_COUNT=0
echo --- Scenario: trickle (Trickle ICE, both online) ---
echo.

call :start_server

echo [INFO] Starting Alice...
start /b "" "%PING_BIN%" --server %HOST% --name alice > "%LOG_DIR%\alice_%SCENARIO%.log" 2>&1
timeout /t 1 /nobreak >nul

echo [INFO] Starting Bob...
start /b "" "%PING_BIN%" --server %HOST% --name bob --to alice > "%LOG_DIR%\bob_%SCENARIO%.log" 2>&1

echo [INFO] Waiting 30 seconds...
call :wait_log "%LOG_DIR%\alice_%SCENARIO%.log" "CONNECTED" 30
call :wait_log "%LOG_DIR%\bob_%SCENARIO%.log"   "CONNECTED" 15

call :check_log "%LOG_DIR%\alice_%SCENARIO%.log" "CONNECTED" "Alice reached CONNECTED (trickle)"
call :check_log "%LOG_DIR%\bob_%SCENARIO%.log"   "CONNECTED" "Bob reached CONNECTED (trickle)"

echo.
echo PASS=%PASS_COUNT%  FAIL=%FAIL_COUNT%
echo Logs: %LOG_DIR%
echo =========================================

call :cleanup
if "%RUNNING_ALL%"=="" ( if %FAIL_COUNT% gtr 0 ( exit /b 1 ) else ( exit /b 0 ) )
goto :eof

:: ============================================================================
:: all - run all scenarios
:: ============================================================================
:run_all
set RUNNING_ALL=1
echo === Running all Relay scenarios ===
echo.
call :run_online
echo.
call :run_offline
echo.
call :run_trickle
echo.
echo =========================================
echo   All Relay scenarios complete
echo   Logs: %LOG_DIR%
echo =========================================
goto :eof
