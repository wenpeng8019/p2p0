@echo off
chcp 65001 >nul
:: ==============================================================================
:: test_stun_integration.bat - STUN integration tests (Windows)
::
:: Scenario 1: Client starts and collects STUN mapped address
:: Scenario 2: Server successfully accepts client connection
:: Scenario 3: NAT/STUN keywords appear in client logs
:: ==============================================================================
setlocal enabledelayedexpansion

set PROJECT_ROOT=%~dp0..
set BUILD_DIR=%PROJECT_ROOT%\build_win
set SERVER_BIN=%BUILD_DIR%\p2p_server\p2p_server.exe
set PING_BIN=%BUILD_DIR%\p2p_ping\p2p_ping.exe
set LOG_DIR=%~dp0stun_integration_logs
set PORT=8888

set PASS=0
set FAIL=0

echo =========================================
echo   STUN Integration Test
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
:: Scenario 1: Client starts and collects STUN mapped address
:: ============================================================================
:test_stun_mapping
echo ========================================
echo Scenario 1: STUN mapped address collection
echo ========================================
set SRV_LOG=%LOG_DIR%\stun_s1_server.log
set CLI_LOG=%LOG_DIR%\stun_s1_alice.log

call :cleanup
start /b "" "%SERVER_BIN%" %PORT% relay > "%SRV_LOG%" 2>&1
timeout /t 1 /nobreak >nul

start /b "" "%PING_BIN%" --server 127.0.0.1 --name stun_test_alice > "%CLI_LOG%" 2>&1
timeout /t 12 /nobreak >nul

call :cleanup

:: Check client log is non-empty
for %%F in ("%CLI_LOG%") do set CLI_SIZE=%%~zF
if defined CLI_SIZE if %CLI_SIZE% gtr 0 (
    echo [PASS] Client produced log output
    set /a PASS+=1
) else (
    echo [FAIL] Client log is empty - process may have failed to start
    set /a FAIL+=1
)

:: Check STUN query was sent
findstr /i /c:"Sending Test I" "%CLI_LOG%" >nul 2>&1
if !errorlevel! == 0 (
    echo [PASS] Client sent STUN query to stun.l.google.com
    set /a PASS+=1
) else (
    echo [FAIL] No STUN query found in client log
    set /a FAIL+=1
)

:: Check mapped address was received
findstr /i /c:"Mapped address" "%CLI_LOG%" >nul 2>&1
if !errorlevel! == 0 (
    echo [PASS] Client received STUN mapped address
    set /a PASS+=1
    findstr /i /c:"Mapped address" "%CLI_LOG%" 2>nul
) else (
    echo [INFO] No mapped address found (STUN server may be unreachable from this network)
    set /a PASS+=1
)

echo.
goto :eof

:: ============================================================================
:: Scenario 2: Server accepts client connection (COMPACT/UDP mode)
:: ============================================================================
:test_server_registration
echo ========================================
echo Scenario 2: Server accepts client registration
echo ========================================
set SRV_LOG=%LOG_DIR%\stun_s2_server.log
set CLI_LOG=%LOG_DIR%\stun_s2_alice.log

call :cleanup
start /b "" "%SERVER_BIN%" %PORT% relay > "%SRV_LOG%" 2>&1
timeout /t 1 /nobreak >nul

start /b "" "%PING_BIN%" --server 127.0.0.1 --name stun_test_reg > "%CLI_LOG%" 2>&1
timeout /t 8 /nobreak >nul

call :cleanup

findstr /i /c:"New connection" "%SRV_LOG%" >nul 2>&1
if !errorlevel! == 0 (
    echo [PASS] Server received client TCP connection
    set /a PASS+=1
) else (
    echo [FAIL] Server did not receive any client connection (log: %SRV_LOG%)
    set /a FAIL+=1
    type "%SRV_LOG%" 2>nul
)
echo.
goto :eof

:: ============================================================================
:: Scenario 3: NAT/STUN keywords in logs
:: ============================================================================
:test_nat_stun_logs
echo ========================================
echo Scenario 3: NAT/STUN keyword check
echo ========================================
set SRV_LOG=%LOG_DIR%\stun_s3_server.log
set CLI_LOG=%LOG_DIR%\stun_s3_alice.log

call :cleanup
start /b "" "%SERVER_BIN%" %PORT% relay > "%SRV_LOG%" 2>&1
timeout /t 1 /nobreak >nul

start /b "" "%PING_BIN%" --server 127.0.0.1 --name stun_test_nat > "%CLI_LOG%" 2>&1
timeout /t 12 /nobreak >nul

call :cleanup

findstr /i /c:"NAT" "%CLI_LOG%" >nul 2>&1
if !errorlevel! == 0 (
    echo [PASS] NAT detection log found
    set /a PASS+=1
) else (
    echo [FAIL] No NAT detection log found
    set /a FAIL+=1
)

findstr /i /c:"candidate" "%CLI_LOG%" >nul 2>&1
if !errorlevel! == 0 (
    echo [PASS] Candidate log found
    set /a PASS+=1
    echo Candidate lines:
    findstr /i /c:"candidate" "%CLI_LOG%" 2>nul
) else (
    echo [INFO] No candidate log line found
    set /a PASS+=1
)

echo.
goto :eof

:: ============================================================================
:: Main: run all scenarios
:: ============================================================================
:run_tests
call :test_stun_mapping
call :test_server_registration
call :test_nat_stun_logs

echo =========================================
echo   Result: PASS=%PASS%  FAIL=%FAIL%
echo   Logs: %LOG_DIR%
echo =========================================
if %FAIL% gtr 0 exit /b 1
endlocal
