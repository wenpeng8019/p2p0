cmake_minimum_required(VERSION 3.10)
project(p2p VERSION 0.1.0 LANGUAGES C)

# --- 构建选项 ---
option(WITH_DTLS    "启用 MbedTLS DTLS 支持" OFF)
option(WITH_OPENSSL "启用 OpenSSL DTLS 支持" OFF)
option(WITH_SCTP    "启用 usrsctp 支持" OFF)
option(THREADED     "启用内部线程" ON)
option(I18N_ENABLED "启用 i18n 多语言支持" ON)
option(I18N_CN      "生成中文翻译头文件 (LANG.cn.h)" ON)

# --- 平台检测 ---
if(WIN32)
    message(STATUS "平台: Windows")
    # Winsock2 在 p2p_platform.h 中通过 #pragma comment 链接 (MSVC)，
    # 但为 MinGW / 交叉编译器显式添加。
    set(PLATFORM_LIBS ws2_32 winhttp iphlpapi)
elseif(APPLE)
    message(STATUS "平台: macOS")
    set(PLATFORM_LIBS "")
elseif(UNIX)
    message(STATUS "平台: Linux/POSIX")
    # 旧版 glibc (< 2.17) 中 clock_gettime 在 librt
    find_library(RT_LIB rt)
    if(RT_LIB)
        set(PLATFORM_LIBS ${RT_LIB})
    else()
        set(PLATFORM_LIBS "")
    endif()
endif()

# --- 编译器配置 ---
set(CMAKE_C_STANDARD 99)
if(MSVC)
    # /W3: 启用三级警告
    # /wd4068: 禁用 C4068 警告 ("unknown pragma")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /W3 /utf-8 /wd4068 /std:c11")
    # 禁用 MSVC 对标准 POSIX 函数发出的 "不安全" CRT 警告
    add_definitions(-D_CRT_SECURE_NO_WARNINGS -D_WINSOCK_DEPRECATED_NO_WARNINGS)
else()
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Wno-unknown-pragmas -Wpedantic")
endif()

# --- 包含目录 ---
include_directories(include)

# --- i18n 子模块检查 ---
if(NOT EXISTS "${CMAKE_SOURCE_DIR}/i18n/i18n.h")
    message(FATAL_ERROR
        "i18n submodule not found. Please run:\n"
        "  git submodule update --init\n"
        "then re-run cmake.")
endif()
include_directories(i18n)

# --- stdc 子模块检查 ---
if(NOT EXISTS "${CMAKE_SOURCE_DIR}/stdc/stdc.h")
    message(FATAL_ERROR
        "stdc submodule not found. Please run:\n"
        "  git submodule update --init\n"
        "then re-run cmake.")
endif()
add_subdirectory(stdc)

# --- 源文件 ---
set(LIB_SRCS
    src/p2p_udp.c
    src/p2p_nat.c
    src/p2p_trans_reliable.c
    src/p2p_stream.c
    src/p2p_route.c
    src/p2p_probe.c
    src/p2p.c
    src/p2p_stun.c
    src/p2p_ice.c
    src/p2p_turn.c
    src/p2p_tcp_punch.c
    src/p2p_crypto.c
    src/p2p_trans_pseudotcp.c
    src/p2p_signal_relay.c
    src/p2p_signal_pubsub.c
    src/p2p_signal_compact.c
    src/p2p_http.c
    src/p2p_path_manager.c
    src/p2p_channel.c
    src/p2p_instrument.c
    src/p2p_instrument.h
)

# --- i18n --ndebug 自动检测 ---
# Debug 构建 → 基于 SID 的稳定 ID（默认）
# Release/RelWithDebInfo/MinSizeRel → 紧凑连续 ID (--ndebug)
set(I18N_NDEBUG_FLAG "")
if(CMAKE_BUILD_TYPE)
    string(TOUPPER "${CMAKE_BUILD_TYPE}" _bt)
    if(NOT _bt STREQUAL "DEBUG")
        set(I18N_NDEBUG_FLAG "--ndebug")
    endif()
endif()

# 跨平台 i18n 命令
if(WIN32)
    set(I18N_SCRIPT "${CMAKE_SOURCE_DIR}/i18n/i18n.bat")
else()
    set(I18N_SCRIPT "${CMAKE_SOURCE_DIR}/i18n/i18n.sh")
endif()

# --import cn 中文翻译
set(I18N_IMPORT_FLAG "")
if(I18N_CN)
    set(I18N_IMPORT_FLAG "--import" "cn")
endif()

# --- p2p 核心库 i18n 代码生成 ---
# I18N_ENABLED=ON:  跟踪源文件变化，修改时重新生成（构建时）
# I18N_ENABLED=OFF: 文件不存在时生成一次（配置时）
if(I18N_ENABLED)
    # 开发模式：源文件变化时重新生成
    add_custom_command(
        OUTPUT
            ${CMAKE_SOURCE_DIR}/src/.LANG.h
            ${CMAKE_SOURCE_DIR}/src/.LANG.c
        COMMAND ${I18N_SCRIPT} src --name p2p ${I18N_NDEBUG_FLAG} ${I18N_IMPORT_FLAG}
        DEPENDS
            ${LIB_SRCS}
            ${I18N_SCRIPT}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Generating i18n language files for p2p core library"
    )
else()
    # 禁用模式：仅当文件不存在时生成（配置时检查）
    if(NOT EXISTS "${CMAKE_SOURCE_DIR}/src/.LANG.h" OR NOT EXISTS "${CMAKE_SOURCE_DIR}/src/.LANG.c")
        message(STATUS "为 p2p 核心生成 i18n 文件（一次性，I18N_ENABLED=OFF）")
        execute_process(
            COMMAND ${I18N_SCRIPT} src --name p2p ${I18N_NDEBUG_FLAG} ${I18N_IMPORT_FLAG}
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            RESULT_VARIABLE _i18n_result
        )
        if(NOT _i18n_result EQUAL 0)
            message(WARNING "i18n generation failed for p2p core (exit ${_i18n_result})")
        endif()
    endif()
endif()

# 架构说明：p2p_static 本身不链接 i18n.c
# （p2p_ping / p2p_server 各自提供独立的 i18n 实例）。
# 库源码使用 LA_W/LA_S/LA_F 标记，默认展开为字符串字面量；
# 启用 I18N_ENABLED 时调用 lang_str()，在链接时由可执行文件的 i18n.c 解析。
if(I18N_ENABLED)
    message(STATUS "p2p 核心: I18N_ENABLED — 库字符串使用 LA_X 标记（链接时 ID 查找）")
else()
    message(STATUS "p2p 核心: 仅 API 标记 — LA_X 展开为字符串字面量")
endif()

if(THREADED)
    add_definitions(-DP2P_THREADED)
    list(APPEND LIB_SRCS src/p2p_thread.c)
    find_package(Threads REQUIRED)
endif()

# --- MbedTLS 集成 ---
if(WITH_DTLS)
    add_definitions(-DWITH_DTLS)
    list(APPEND LIB_SRCS src/p2p_dtls_mbedtls.c)
    
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/mbedtls/CMakeLists.txt")
        # 构建内置 mbedtls
        set(ENABLE_TESTING OFF CACHE BOOL "")
        set(ENABLE_PROGRAMS OFF CACHE BOOL "")
        add_subdirectory(third_party/mbedtls EXCLUDE_FROM_ALL)
        include_directories(third_party/mbedtls/include)
        set(DTLS_LIBRARIES mbedtls mbedx509 mbedcrypto)
    else()
        # 回退到系统 mbedtls（主要针对 macOS/Homebrew）
        find_path(MBED_INC mbedtls/ssl.h PATHS /opt/homebrew/include)
        find_library(MBED_LIB mbedtls PATHS /opt/homebrew/lib)
        find_library(MBED_X509 mbedx509 PATHS /opt/homebrew/lib)
        find_library(MBED_CRYPTO mbedcrypto PATHS /opt/homebrew/lib)
        include_directories(${MBED_INC})
        set(DTLS_LIBRARIES ${MBED_LIB} ${MBED_X509} ${MBED_CRYPTO})
    endif()
endif()

# --- OpenSSL 集成 ---
if(WITH_OPENSSL)
    add_definitions(-DWITH_OPENSSL)
    list(APPEND LIB_SRCS src/p2p_dtls_openssl.c)
    find_package(OpenSSL REQUIRED)
    include_directories(${OPENSSL_INCLUDE_DIR})
    set(OPENSSL_LIBRARIES OpenSSL::SSL OpenSSL::Crypto)
endif()

# --- usrsctp 集成 ---
if(WITH_SCTP)
    add_definitions(-DWITH_SCTP)
    list(APPEND LIB_SRCS src/p2p_trans_sctp.c)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/usrsctp/CMakeLists.txt")
        set(sctp_build_shared OFF CACHE BOOL "")
        set(sctp_build_programs OFF CACHE BOOL "")
        add_subdirectory(third_party/usrsctp EXCLUDE_FROM_ALL)
        include_directories(third_party/usrsctp/usrsctplib)
        set(SCTP_LIBRARIES usrsctp)
    endif()
endif()

# --- 目标定义 ---

# i18n 扫描辅助目标：确保条件编译的源文件也进入 compile_commands.json
# i18n.sh 通过 compile_commands.json 获取每个文件的编译参数来预处理
# 只包含条件编译的文件（其余已在 p2p_static 中），不参与实际链接
add_library(p2p_i18n_extra OBJECT EXCLUDE_FROM_ALL
    src/p2p_dtls_mbedtls.c
    src/p2p_dtls_openssl.c
    src/p2p_trans_sctp.c
)
target_include_directories(p2p_i18n_extra PRIVATE
    src
    ${CMAKE_SOURCE_DIR}/stdc
    third_party/mbedtls/include
    third_party/usrsctp/usrsctplib
)
target_compile_definitions(p2p_i18n_extra PRIVATE WITH_DTLS WITH_OPENSSL WITH_SCTP)
# OpenSSL 头文件路径（系统包）
find_path(_OPENSSL_INC openssl/ssl.h PATHS /opt/homebrew/include /usr/include /usr/local/include)
if(_OPENSSL_INC)
    target_include_directories(p2p_i18n_extra PRIVATE ${_OPENSSL_INC})
endif()

add_library(p2p_static STATIC ${LIB_SRCS})
target_include_directories(p2p_static PRIVATE src)
target_link_libraries(p2p_static stdc ${PLATFORM_LIBS})
if(WITH_SCTP)
    target_link_libraries(p2p_static ${SCTP_LIBRARIES})
endif()
if(I18N_ENABLED)
    target_compile_definitions(p2p_static PRIVATE I18N_ENABLED)
endif()
if(THREADED)
    target_link_libraries(p2p_static Threads::Threads)
endif()

# i18n.sh 在编译任何源文件前生成 src/.LANG.h/.LANG.c
# Debug → 基于 SID 的稳定 ID；Release → 紧凑连续 ID (--ndebug)
# I18N_ENABLED=ON: 通过 custom_target 跟踪源文件变化
# I18N_ENABLED=OFF: 文件已在配置时生成
if(I18N_ENABLED)
    add_custom_target(p2p_core_i18n_gen DEPENDS
        ${CMAKE_SOURCE_DIR}/src/.LANG.h
        ${CMAKE_SOURCE_DIR}/src/.LANG.c
    )
    add_dependencies(p2p_static p2p_core_i18n_gen)
endif()

# --- 信令服务器 ---
add_subdirectory(p2p_server)

# --- p2p_ping 工具 ---
add_subdirectory(p2p_ping)

# --- 测试 ---
enable_testing()
add_subdirectory(test)
