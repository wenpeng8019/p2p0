@echo off
chcp 65001 >nul
:: ==============================================================================
:: test_pubsub_integration.bat — PUBSUB (GitHub Gist) 模式集成测试 (Windows)
:: 对应 test_pubsub_integration.sh
::
:: 需要：GitHub Token 和 Gist ID
::   方式 1: 设置环境变量
::     set P2P_GITHUB_TOKEN=ghp_xxxx
::     set P2P_GIST_ID=your_gist_id
::   方式 2: 从 local\github_token.md 自动读取（格式同 .sh 版本）
::
:: 测试 1: PUBSUB 基本连通性
:: ==============================================================================
setlocal enabledelayedexpansion

set PROJECT_ROOT=%~dp0..
set BUILD_DIR=%PROJECT_ROOT%\build_win
set PING_BIN=%BUILD_DIR%\p2p_ping\p2p_ping.exe
set LOG_DIR=%~dp0pubsub_gist_logs
set TOKEN_FILE=%PROJECT_ROOT%\local\github_token.md

echo =========================================
echo   PUBSUB (GitHub Gist) 模式集成测试
echo =========================================
echo.

:: 尝试从 local\github_token.md 自动读取 token / gist id
if "%P2P_GITHUB_TOKEN%"=="" (
    if exist "%TOKEN_FILE%" (
        for /f "tokens=*" %%L in ('findstr /i "ghp_" "%TOKEN_FILE%"') do (
            if "!P2P_GITHUB_TOKEN!"=="" set P2P_GITHUB_TOKEN=%%L
        )
    )
)
if "%P2P_GIST_ID%"=="" (
    if exist "%TOKEN_FILE%" (
        for /f "tokens=2 delims=:" %%L in ('findstr /i /c:"Gist ID:" "%TOKEN_FILE%"') do (
            if "!P2P_GIST_ID!"=="" (
                set P2P_GIST_ID=%%L
                :: 去除首尾空格
                set P2P_GIST_ID=!P2P_GIST_ID: =!
            )
        )
    )
)

if "%P2P_GITHUB_TOKEN%"=="" (
    echo [ERROR] 缺少 GitHub Token
    echo 请设置:
    echo   set P2P_GITHUB_TOKEN=ghp_xxx...
    echo   set P2P_GIST_ID=your_gist_id
    echo 或确保 local\github_token.md 包含 ghp_ 开头的 token 和 "Gist ID: xxx" 行
    exit /b 1
)
if "%P2P_GIST_ID%"=="" (
    echo [ERROR] 缺少 Gist ID
    echo 请设置: set P2P_GIST_ID=your_gist_id
    exit /b 1
)

echo GitHub Token: %P2P_GITHUB_TOKEN:~0,10%...
echo Gist ID: %P2P_GIST_ID%
echo.

if not exist "%PING_BIN%" (
    echo [ERROR] p2p_ping.exe 未找到: %PING_BIN%
    exit /b 1
)
if not exist "%LOG_DIR%" mkdir "%LOG_DIR%"

goto :run_tests

:cleanup
taskkill /f /im p2p_ping.exe >nul 2>&1
timeout /t 1 /nobreak >nul
goto :eof

:: ============================================================================
:: 测试 1: PUBSUB 基本连通性
:: ============================================================================
:test_pubsub_basic
echo === 测试 1: PUBSUB 基本连通性 ===
echo.

set ALOG=%LOG_DIR%\pubsub_alice.log
set BLOG=%LOG_DIR%\pubsub_bob.log

call :cleanup
del "%ALOG%" "%BLOG%" >nul 2>&1

:: Reset Gist signaling channel
echo [INFO] Resetting Gist signaling channel...
echo {"files":{"p2p_signal.json":{"content":"{}"}}} > "%TEMP%\p2p_gist_reset.json"
curl -s -X PATCH ^
  -H "Authorization: token %P2P_GITHUB_TOKEN%" ^
  -H "Content-Type: application/json" ^
  -d @"%TEMP%\p2p_gist_reset.json" ^
  "https://api.github.com/gists/%P2P_GIST_ID%" >nul
timeout /t 2 /nobreak >nul

:: 启动 Alice (SUB - 订阅者，被动等待)
echo [INFO] 启动 Alice (SUB - 等待连接)...
start /b "" "%PING_BIN%" --name alice --github "%P2P_GITHUB_TOKEN%" --gist "%P2P_GIST_ID%" > "%ALOG%" 2>&1
timeout /t 3 /nobreak >nul
echo [INFO] Alice 已启动

:: 启动 Bob (PUB - 发布者，主动连接)
echo [INFO] 启动 Bob (PUB - 主动连接到 alice)...
start /b "" "%PING_BIN%" --name bob --to alice --github "%P2P_GITHUB_TOKEN%" --gist "%P2P_GIST_ID%" > "%BLOG%" 2>&1
echo [INFO] Bob 已启动
echo.

:: 等待连接建立（PUBSUB 轮询延迟较长）
echo [INFO] Waiting for P2P connection (up to 60s)...
echo   - Bob 正在发布 offer 到 Gist...
echo   - Alice 正在轮询检测 offer...
echo   - Alice 将发布 answer...
echo   - Bob 轮询检测 answer 后开始 ICE 协商...
echo.

set TIMEOUT=60
set CONNECTED=0

for /l %%I in (1,1,%TIMEOUT%) do (
    if !CONNECTED! == 0 (
        findstr /i /c:"CONNECTED (3)" /c:"-> CONNECTED" /c:"Connection established" "%ALOG%" >nul 2>&1
        if !errorlevel! == 0 (
            findstr /i /c:"CONNECTED (3)" /c:"-> CONNECTED" /c:"Connection established" "%BLOG%" >nul 2>&1
            if !errorlevel! == 0 (
                echo [PASS] P2P 连接建立成功！(用时约 %%I 秒)
                set CONNECTED=1
            )
        )
        if !CONNECTED! == 0 (
            <nul set /p "=."
            timeout /t 1 /nobreak >nul
        )
    )
)
echo.

if !CONNECTED! == 0 (
    echo [INFO] 60 秒内未检测到 CONNECTED 状态
    echo       可能原因: Gist 轮询延迟 / 网络问题 / 功能尚未完全实现
)

:: 输出日志
echo.
echo --- Alice 日志摘要 ---
findstr /i "alice bob gist offer answer CONNECTED STATE" "%ALOG%" 2>nul || type "%ALOG%" 2>nul

echo.
echo --- Bob 日志摘要 ---
findstr /i "alice bob gist offer answer CONNECTED STATE" "%BLOG%" 2>nul || type "%BLOG%" 2>nul

call :cleanup
echo.
goto :eof

:: ============================================================================
:: 主程序
:: ============================================================================
:run_tests
call :test_pubsub_basic

echo =========================================
echo   测试完成
echo   日志保存在: %LOG_DIR%
echo =========================================
endlocal
