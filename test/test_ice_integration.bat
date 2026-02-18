@echo off
chcp 65001 >nul
:: ==============================================================================
:: test_ice_integration.bat - ICE mode end-to-end integration tests (Windows)
::
:: Scenario 1: Basic ICE connection (both peers online)
:: Scenario 2: ICE with LAN shortcut disabled (force NAT punch path)
:: Scenario 3: ICE candidate gathering (Host / Srflx)
:: ==============================================================================
setlocal enabledelayedexpansion

set PROJECT_ROOT=%~dp0..
set BUILD_DIR=%PROJECT_ROOT%\build_win
set SERVER_BIN=%BUILD_DIR%\p2p_server\p2p_server.exe
set PING_BIN=%BUILD_DIR%\p2p_ping\p2p_ping.exe
set LOG_DIR=%~dp0ice_integration_logs
set PORT=8888

set PASS=0
set FAIL=0

echo =========================================
echo   ICE Integration Test
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

goto :run_tests

:: Cleanup helper
:cleanup
taskkill /f /im p2p_server.exe >nul 2>&1
taskkill /f /im p2p_ping.exe   >nul 2>&1
timeout /t 1 /nobreak >nul
goto :eof

:: ============================================================================
:: Helper: wait for keyword in log file
:: Args: %1=file %2=keyword %3=timeout_sec
:: Result: WL_RESULT=0 (found) or 1 (timeout)
:: ============================================================================
:wait_log
set WL_FILE=%~1
set WL_KEY=%~2
set WL_TIMEOUT=%~3
if "%WL_TIMEOUT%"=="" set WL_TIMEOUT=20
set WL_I=0
echo [DEBUG] Waiting for "%WL_KEY%" in %WL_FILE% (timeout=%WL_TIMEOUT%s)
:wl_loop
findstr /i /c:"%WL_KEY%" "%WL_FILE%" >nul 2>&1
if !errorlevel! == 0 (
    set WL_RESULT=0
    echo [DEBUG] Found "%WL_KEY%" after %WL_I%s
    goto :eof
)
if !WL_I! geq %WL_TIMEOUT% (
    set WL_RESULT=1
    echo [DEBUG] Timeout waiting for "%WL_KEY%" after %WL_I%s
    goto :eof
)
timeout /t 1 /nobreak >nul
set /a WL_I+=1
goto wl_loop

:: ============================================================================
:: Scenario 1: Basic ICE connection (both peers online)
:: ============================================================================
:test_ice_basic
echo ========================================
echo Scenario 1: Basic ICE connection (both online)
echo ========================================
set SLOG=%LOG_DIR%\s1_server.log
set ALOG=%LOG_DIR%\s1_alice.log
set BLOG=%LOG_DIR%\s1_bob.log

call :cleanup
start /b "" "%SERVER_BIN%" %PORT% relay > "%SLOG%" 2>&1
timeout /t 1 /nobreak >nul

start /b "" "%PING_BIN%" --server 127.0.0.1 --name alice --to bob > "%ALOG%" 2>&1
timeout /t 1 /nobreak >nul

start /b "" "%PING_BIN%" --server 127.0.0.1 --name bob > "%BLOG%" 2>&1

call :wait_log "%ALOG%" "CONNECTED" 90
if !WL_RESULT! == 0 (
    echo [PASS] Alice reached CONNECTED state
    set /a PASS+=1
) else (
    echo [FAIL] Alice did not reach CONNECTED ^(timeout^)
    set /a FAIL+=1
    echo Alice log tail:
    type "%ALOG%" 2>nul
)

call :wait_log "%BLOG%" "CONNECTED" 90
if !WL_RESULT! == 0 (
    echo [PASS] Bob reached CONNECTED state
    set /a PASS+=1
) else (
    echo [FAIL] Bob did not reach CONNECTED ^(timeout^)
    set /a FAIL+=1
    echo Bob log tail:
    type "%BLOG%" 2>nul
)

findstr /i /c:"logged in" "%SLOG%" >nul 2>&1
if !errorlevel! == 0 (
    echo [PASS] Server logged peer registration
    set /a PASS+=1
) else (
    echo [INFO] Server signaling log not found
)

call :cleanup
echo.
goto :eof

:: ============================================================================
:: Scenario 2: ICE with LAN shortcut disabled (force NAT punch)
:: ============================================================================
:test_ice_force_nat
echo ========================================
echo Scenario 2: ICE with LAN shortcut disabled
echo ========================================
set SLOG=%LOG_DIR%\s2_server.log
set ALOG=%LOG_DIR%\s2_alice.log
set BLOG=%LOG_DIR%\s2_bob.log

call :cleanup
start /b "" "%SERVER_BIN%" %PORT% relay > "%SLOG%" 2>&1
timeout /t 1 /nobreak >nul

start /b "" "%PING_BIN%" --server 127.0.0.1 --name alice --to bob --disable-lan > "%ALOG%" 2>&1
timeout /t 1 /nobreak >nul

start /b "" "%PING_BIN%" --server 127.0.0.1 --name bob --disable-lan > "%BLOG%" 2>&1

call :wait_log "%ALOG%" "CONNECTED" 90
if !WL_RESULT! == 0 (
    echo [PASS] Alice connected ^(LAN shortcut disabled^)
    set /a PASS+=1
) else (
    echo [FAIL] Alice did not connect in force-NAT mode
    set /a FAIL+=1
    echo Alice log tail:
    type "%ALOG%" 2>nul
)

call :wait_log "%BLOG%" "CONNECTED" 90
if !WL_RESULT! == 0 (
    echo [PASS] Bob connected ^(LAN shortcut disabled^)
    set /a PASS+=1
) else (
    echo [FAIL] Bob did not connect in force-NAT mode
    set /a FAIL+=1
    echo Bob log tail:
    type "%BLOG%" 2>nul
)

call :cleanup
echo.
goto :eof

:: ============================================================================
:: Scenario 3: ICE candidate gathering (Host + Srflx)
:: ============================================================================
:test_ice_candidate_gathering
echo ========================================
echo Scenario 3: ICE candidate gathering
echo ========================================
set ALOG=%LOG_DIR%\s3_alice.log

call :cleanup
start /b "" "%SERVER_BIN%" %PORT% relay > nul 2>&1
timeout /t 1 /nobreak >nul

start /b "" "%PING_BIN%" --server 127.0.0.1 --name alice_gather > "%ALOG%" 2>&1
timeout /t 12 /nobreak >nul

call :cleanup

findstr /i /c:"candidate" "%ALOG%" >nul 2>&1
if !errorlevel! == 0 (
    echo [PASS] ICE candidate^(s^) collected
    set /a PASS+=1
    findstr /i /c:"candidate" "%ALOG%" 2>nul
) else (
    echo [INFO] No explicit candidate log found
)

findstr /i /c:"Srflx" "%ALOG%" >nul 2>&1
if !errorlevel! == 0 (
    echo [PASS] STUN server-reflexive candidate gathered
    set /a PASS+=1
) else (
    echo [INFO] No Srflx candidate (no external STUN or LAN-only test)
)

echo.
goto :eof

:: ============================================================================
:: Main: run all scenarios
:: ============================================================================
:run_tests
call :test_ice_basic
call :test_ice_force_nat
call :test_ice_candidate_gathering

echo =========================================
echo   Result: PASS=%PASS%  FAIL=%FAIL%
echo   Logs: %LOG_DIR%
echo =========================================
if %FAIL% gtr 0 exit /b 1
endlocal
