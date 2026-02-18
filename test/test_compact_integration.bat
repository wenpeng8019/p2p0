@echo off
chcp 65001 >nul
:: test_compact_integration.bat - COMPACT mode integration test (Windows)
:: Mirrors: test_compact_integration.sh
setlocal enabledelayedexpansion

set PROJECT_ROOT=%~dp0..
set BUILD_DIR=%PROJECT_ROOT%\build_win
set SERVER_BIN=%BUILD_DIR%\p2p_server\p2p_server.exe
set PING_BIN=%BUILD_DIR%\p2p_ping\p2p_ping.exe
set LOG_DIR=%~dp0compact_integration_logs
set PORT=8888
set PASS=0
set FAIL=0

echo =========================================
echo   COMPACT Mode Integration Test
echo =========================================
echo.

if not exist "%SERVER_BIN%" ( echo [ERROR] p2p_server.exe not found & exit /b 1 )
if not exist "%PING_BIN%"   ( echo [ERROR] p2p_ping.exe not found   & exit /b 1 )
if not exist "%LOG_DIR%" mkdir "%LOG_DIR%"

set SRV_LOG=%LOG_DIR%\server.log
set ALICE_LOG=%LOG_DIR%\alice.log
set BOB_LOG=%LOG_DIR%\bob.log

echo [INFO] Stopping old processes...
taskkill /f /im p2p_server.exe >nul 2>&1
taskkill /f /im p2p_ping.exe   >nul 2>&1
timeout /t 1 /nobreak >nul

echo [INFO] Starting signaling server (port %PORT%)...
start /b "" "%SERVER_BIN%" %PORT% > "%SRV_LOG%" 2>&1
timeout /t 2 /nobreak >nul

echo [INFO] Starting Alice (COMPACT mode, waiting for bob)...
start /b "" "%PING_BIN%" --server 127.0.0.1 --compact --name alice --to bob > "%ALICE_LOG%" 2>&1
timeout /t 2 /nobreak >nul

echo [INFO] Starting Bob (COMPACT mode, connecting to alice)...
start /b "" "%PING_BIN%" --server 127.0.0.1 --compact --name bob --to alice > "%BOB_LOG%" 2>&1

echo [INFO] Waiting 30 seconds for STUN + signaling + NAT punch...
timeout /t 30 /nobreak >nul

echo.
echo === Server Log ===
type "%SRV_LOG%" 2>nul

echo.
echo === Alice Log ===
type "%ALICE_LOG%" 2>nul

echo.
echo === Bob Log ===
type "%BOB_LOG%" 2>nul

echo.
echo === Result ===
findstr /c:"CONNECTED" "%ALICE_LOG%" >nul 2>&1
if !errorlevel! == 0 ( echo [PASS] Alice: CONNECTED & set /a PASS+=1 ) else ( echo [FAIL] Alice: not CONNECTED & set /a FAIL+=1 )

findstr /c:"CONNECTED" "%BOB_LOG%" >nul 2>&1
if !errorlevel! == 0 ( echo [PASS] Bob:   CONNECTED & set /a PASS+=1 ) else ( echo [FAIL] Bob:   not CONNECTED & set /a FAIL+=1 )

echo.
echo [INFO] Cleaning up...
taskkill /f /im p2p_server.exe >nul 2>&1
taskkill /f /im p2p_ping.exe   >nul 2>&1

echo.
echo PASS=%PASS%  FAIL=%FAIL%
echo Logs: %LOG_DIR%
echo =========================================
if %FAIL% gtr 0 exit /b 1
endlocal
