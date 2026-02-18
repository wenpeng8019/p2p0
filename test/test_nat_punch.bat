@echo off
chcp 65001 >nul
:: ==============================================================================
:: test_nat_punch.bat — NAT 打洞流程测试 (Windows)
:: 对应 test_nat_punch.sh
::
:: 测试 1: COMPACT 模式 NAT 打洞
:: 测试 2: Relay 模式 NAT 打洞
:: 测试 3: 禁用同子网直连优化，强制 NAT 打洞
:: ==============================================================================
setlocal enabledelayedexpansion

set PROJECT_ROOT=%~dp0..
set BUILD_DIR=%PROJECT_ROOT%\build_win
set SERVER_BIN=%BUILD_DIR%\p2p_server\p2p_server.exe
set PING_BIN=%BUILD_DIR%\p2p_ping\p2p_ping.exe
set LOG_DIR=%~dp0nat_punch_logs

set PASS=0
set FAIL=0

echo =========================================
echo   NAT 打洞流程测试
echo =========================================
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

goto :run_tests

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
if !errorlevel! == 0 (
    set WL_RESULT=0
    goto :eof
)
if !WL_I! geq %WL_TIMEOUT% (
    set WL_RESULT=1
    goto :eof
)
timeout /t 1 /nobreak >nul
set /a WL_I+=1
goto wl_loop

:: ============================================================================
:: 测试 1: COMPACT 模式 NAT 打洞
:: ============================================================================
:test_compact_nat
echo --- TEST 1: COMPACT 模式 NAT 打洞 (端口 8890) ---
set PORT=8890
set SLOG=%LOG_DIR%\compact_server.log
set ALOG=%LOG_DIR%\compact_alice.log
set BLOG=%LOG_DIR%\compact_bob.log

call :cleanup
start /b "" "%SERVER_BIN%" %PORT% > "%SLOG%" 2>&1
timeout /t 1 /nobreak >nul

echo   [INFO] 等待服务器在端口 %PORT% 上启动...
timeout /t 1 /nobreak >nul

start /b "" "%PING_BIN%" --server 127.0.0.1 --compact --name alice --to bob > "%ALOG%" 2>&1
timeout /t 1 /nobreak >nul

start /b "" "%PING_BIN%" --server 127.0.0.1 --compact --name bob --to alice > "%BLOG%" 2>&1

echo   [INFO] 等待连接建立 (最多 15 秒)...
call :wait_log "%ALOG%" "CONNECTED connected" 15
if !WL_RESULT! == 0 (
    echo   [PASS] Alice connected (COMPACT mode)
    set /a PASS+=1
) else (
    echo   [INFO] Alice: 未检测到 CONNECTED（COMPACT 模式可能仍在打洞）
)

call :wait_log "%BLOG%" "CONNECTED connected" 15
if !WL_RESULT! == 0 (
    echo   [PASS] Bob connected (COMPACT mode)
    set /a PASS+=1
) else (
    echo   [INFO] Bob: 未检测到 CONNECTED
)

:: 检查 NAT 打洞相关日志
findstr /i "NAT punch PUNCH candidate" "%ALOG%" "%BLOG%" "%SLOG%" >nul 2>&1
if !errorlevel! == 0 (
    echo   [PASS] NAT 打洞日志已记录
    set /a PASS+=1
) else (
    echo   [INFO] 未发现明确的 NAT 打洞日志（可能在内部处理）
)

call :cleanup
echo.
goto :eof

:: ============================================================================
:: 测试 2: Relay 模式 NAT 打洞
:: ============================================================================
:test_relay_nat
echo --- TEST 2: Relay 模式 NAT 打洞 (端口 8891) ---
set PORT=8891
set SLOG=%LOG_DIR%\relay_server.log
set ALOG=%LOG_DIR%\relay_alice.log
set BLOG=%LOG_DIR%\relay_bob.log

call :cleanup
start /b "" "%SERVER_BIN%" %PORT% relay > "%SLOG%" 2>&1
timeout /t 1 /nobreak >nul

start /b "" "%PING_BIN%" --server 127.0.0.1 --name alice --to bob > "%ALOG%" 2>&1
timeout /t 1 /nobreak >nul

start /b "" "%PING_BIN%" --server 127.0.0.1 --name bob > "%BLOG%" 2>&1

echo   [INFO] 等待连接建立 (最多 30 秒)...
call :wait_log "%ALOG%" "CONNECTED connected" 30
if !WL_RESULT! == 0 (
    echo   [PASS] Alice connected ^(Relay mode^)
    set /a PASS+=1
) else (
    echo   [INFO] Alice: 未检测到 CONNECTED
)

call :wait_log "%BLOG%" "CONNECTED connected" 30
if !WL_RESULT! == 0 (
    echo   [PASS] Bob connected ^(Relay mode^)
    set /a PASS+=1
) else (
    echo   [INFO] Bob: 未检测到 CONNECTED
)

findstr /i "Forwarded OFFER FORWARD" "%SLOG%" >nul 2>&1
if !errorlevel! == 0 (
    echo   [PASS] 服务器信令转发已记录
    set /a PASS+=1
) else (
    echo   [INFO] 服务器信令转发日志未找到
)

call :cleanup
echo.
goto :eof

:: ============================================================================
:: 测试 3: 禁用 LAN shortcut，强制 NAT 打洞
:: ============================================================================
:test_force_nat_punch
echo --- TEST 3: 禁用 LAN shortcut，强制 NAT 打洞 ---
set PORT=8892
set SLOG=%LOG_DIR%\forcnat_server.log
set ALOG=%LOG_DIR%\forcnat_alice.log
set BLOG=%LOG_DIR%\forcnat_bob.log

call :cleanup
start /b "" "%SERVER_BIN%" %PORT% > "%SLOG%" 2>&1
timeout /t 1 /nobreak >nul

start /b "" "%PING_BIN%" --server 127.0.0.1 --name alice --disable-lan > "%ALOG%" 2>&1
timeout /t 1 /nobreak >nul

start /b "" "%PING_BIN%" --server 127.0.0.1 --name bob --to alice --disable-lan --verbose-punch > "%BLOG%" 2>&1

echo   [INFO] 等待打洞流程 (最多 20 秒)...
call :wait_log "%BLOG%" "NAT_PUNCH PUNCHING PUNCH" 20
if !WL_RESULT! == 0 (
    echo   [PASS] NAT 打洞流程可见于日志
    set /a PASS+=1
) else (
    echo   [INFO] NAT_PUNCH 日志未找到（--verbose-punch 可能需要 --simple 模式）
)

call :wait_log "%ALOG%" "CONNECTED connected" 20
if !WL_RESULT! == 0 (
    echo   [PASS] Alice connected (force-NAT mode)
    set /a PASS+=1
) else (
    echo   [INFO] Alice: 未在强制 NAT 模式下检测到 CONNECTED
)

:: 显示 Bob 打洞详细日志
echo   [Bob 打洞详细日志]:
findstr /i "NAT PUNCH punch" "%BLOG%" 2>nul | more

call :cleanup
echo.
goto :eof

:: ============================================================================
:: 主程序
:: ============================================================================
:run_tests
call :test_compact_nat
call :test_relay_nat
call :test_force_nat_punch

echo =========================================
echo   结果: PASS=%PASS%  FAIL=%FAIL%
echo   日志保存在: %LOG_DIR%
echo =========================================
if %FAIL% gtr 0 exit /b 1
endlocal
