@echo off
chcp 65001 >nul
:: ==============================================================================
:: test_all.bat — P2P 全量集成测试入口 (Windows)
:: 对应 test_all.sh
::
:: 用法:
::   test_all.bat              — 显示菜单，选择要运行的测试
::   test_all.bat all          — 运行全部集成测试（跳过 PUBSUB，除非已配置 token）
::   test_all.bat compact      — 仅 COMPACT 集成测试
::   test_all.bat relay        — 仅 Relay 集成测试
::   test_all.bat stun         — 仅 STUN 集成测试
::   test_all.bat ice          — 仅 ICE 集成测试
::   test_all.bat turn         — 仅 TURN 集成测试
::   test_all.bat nat          — 仅 NAT 打洞测试
::   test_all.bat pubsub       — 仅 PUBSUB 测试（需要 GitHub token）
::   test_all.bat unit         — 运行 ctest 单元测试
:: ==============================================================================
setlocal enabledelayedexpansion

set PROJECT_ROOT=%~dp0..
set BUILD_DIR=%PROJECT_ROOT%\build_win
set TEST_DIR=%~dp0
set SERVER_BIN=%BUILD_DIR%\p2p_server\p2p_server.exe
set PING_BIN=%BUILD_DIR%\p2p_ping\p2p_ping.exe

set CMD=%~1

echo =========================================
echo   P2P Zero — 全量集成测试
echo =========================================
echo.

:: 检查可执行文件
if not exist "%SERVER_BIN%" (
    echo [ERROR] p2p_server.exe 未找到: %SERVER_BIN%
    echo 请先运行: cmake --build build_win
    exit /b 1
)
if not exist "%PING_BIN%" (
    echo [ERROR] p2p_ping.exe 未找到: %PING_BIN%
    echo 请先运行: cmake --build build_win
    exit /b 1
)
echo [OK] 可执行文件已就绪
echo.

if "%CMD%"=="" goto :menu
if /i "%CMD%"=="all"     goto :run_all
if /i "%CMD%"=="compact" goto :run_compact
if /i "%CMD%"=="relay"   goto :run_relay
if /i "%CMD%"=="stun"    goto :run_stun
if /i "%CMD%"=="ice"     goto :run_ice
if /i "%CMD%"=="turn"    goto :run_turn
if /i "%CMD%"=="nat"     goto :run_nat
if /i "%CMD%"=="pubsub"  goto :run_pubsub
if /i "%CMD%"=="unit"    goto :run_unit
echo [ERROR] 未知参数: %CMD%
goto :menu

:: ============================================================================
:: 交互菜单
:: ============================================================================
:menu
echo 请选择要运行的测试:
echo.
echo   1) 单元测试 (ctest，9 个 .c 测试)
echo   2) COMPACT 集成测试
echo   3) Relay 集成测试 (所有场景)
echo   4) STUN 集成测试
echo   5) ICE 集成测试
echo   6) TURN 集成测试
echo   7) NAT 打洞测试
echo   8) PUBSUB 集成测试 (需要 GitHub Token)
echo   9) 全部集成测试 (2-7，跳过 PUBSUB)
echo   0) 退出
echo.
set /p CHOICE=请输入选项 [0-9]: 

if "%CHOICE%"=="1" goto :run_unit
if "%CHOICE%"=="2" goto :run_compact
if "%CHOICE%"=="3" goto :run_relay
if "%CHOICE%"=="4" goto :run_stun
if "%CHOICE%"=="5" goto :run_ice
if "%CHOICE%"=="6" goto :run_turn
if "%CHOICE%"=="7" goto :run_nat
if "%CHOICE%"=="8" goto :run_pubsub
if "%CHOICE%"=="9" goto :run_all
if "%CHOICE%"=="0" goto :exit
echo 无效选项，请重新输入
goto :menu

:: ============================================================================
:: 各测试入口
:: ============================================================================
:run_unit
echo.
echo === 单元测试 (ctest) ===
cd /d "%BUILD_DIR%"
ctest --output-on-failure -V
cd /d "%TEST_DIR%"
goto :done

:run_compact
echo.
echo === COMPACT 集成测试 ===
call "%TEST_DIR%test_compact_integration.bat"
goto :done

:run_relay
echo.
echo === Relay 集成测试 ===
call "%TEST_DIR%test_relay_integration.bat" all
goto :done

:run_stun
echo.
echo === STUN 集成测试 ===
call "%TEST_DIR%test_stun_integration.bat"
goto :done

:run_ice
echo.
echo === ICE 集成测试 ===
call "%TEST_DIR%test_ice_integration.bat"
goto :done

:run_turn
echo.
echo === TURN 集成测试 ===
call "%TEST_DIR%test_turn_integration.bat"
goto :done

:run_nat
echo.
echo === NAT 打洞测试 ===
call "%TEST_DIR%test_nat_punch.bat"
goto :done

:run_pubsub
echo.
echo === PUBSUB 集成测试 ===
call "%TEST_DIR%test_pubsub_integration.bat"
goto :done

:run_all
echo.
echo === 运行全部集成测试（单元 + COMPACT + Relay + STUN + ICE + TURN + NAT）===
echo.
call :run_unit
call :run_compact
call :run_relay
call :run_stun
call :run_ice
call :run_turn
call :run_nat
if not "%P2P_GITHUB_TOKEN%"=="" (
    call :run_pubsub
) else (
    echo [INFO] 跳过 PUBSUB 测试 (未设置 P2P_GITHUB_TOKEN)
)
goto :done

:done
echo.
echo =========================================
echo   全部指定测试已完成
echo =========================================
:exit
endlocal
