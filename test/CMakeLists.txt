cmake_minimum_required(VERSION 3.10)

# Windows 下独立测试 target 需要链接 ws2_32
if(WIN32)
    set(TEST_PLATFORM_LIBS ws2_32)
else()
    set(TEST_PLATFORM_LIBS "")
endif()

# 测试可执行文件
add_executable(test_transport
    test_transport.c
    ${CMAKE_SOURCE_DIR}/src/.LANG.c
    ${CMAKE_SOURCE_DIR}/i18n/i18n.c
)

# 链接静态库
target_link_libraries(test_transport
    p2p_static
)

# Include i18n headers
target_include_directories(test_transport PRIVATE ${CMAKE_SOURCE_DIR}/i18n)

# Enable i18n for test
if(I18N_ENABLED)
    target_compile_definitions(test_transport PRIVATE I18N_ENABLED)
endif()

# COMPACT REGISTER 协议单元测试
add_executable(test_compact_register
    test_compact_register.c
)
target_include_directories(test_compact_register PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/stdc
)
target_link_libraries(test_compact_register stdc ${TEST_PLATFORM_LIBS})

# COMPACT PEER_INFO 协议单元测试
add_executable(test_compact_sync
    test_compact_sync.c
)
target_include_directories(test_compact_sync PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/stdc
)
target_link_libraries(test_compact_sync stdc ${TEST_PLATFORM_LIBS})

# COMPACT 生命周期协议单元测试 (ALIVE/UNREGISTER/FIN)
add_executable(test_compact_lifecycle
    test_compact_lifecycle.c
)
target_include_directories(test_compact_lifecycle PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/stdc
)
target_link_libraries(test_compact_lifecycle stdc ${TEST_PLATFORM_LIBS})

# COMPACT RPC 协议单元测试
add_executable(test_compact_rpc
    test_compact_rpc.c
)
target_include_directories(test_compact_rpc PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/stdc
)
target_link_libraries(test_compact_rpc stdc ${TEST_PLATFORM_LIBS})

# COMPACT RELAY/NAT_PROBE 协议单元测试
add_executable(test_compact_relay
    test_compact_relay.c
)
target_include_directories(test_compact_relay PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/stdc
)
target_link_libraries(test_compact_relay stdc ${TEST_PLATFORM_LIBS})

# P2P Ping 客户端单元测试模版（通用，不区分信令模式）
add_executable(test_ping_template
    test_ping_template.c
)
target_include_directories(test_ping_template PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/stdc
)
target_link_libraries(test_ping_template stdc ${TEST_PLATFORM_LIBS})

# P2P Ping 双客户端连接测试 (COMPACT 模式, Alice <-> Bob)
add_executable(test_ping_c_connect
    test_ping_c_connect.c
)
target_include_directories(test_ping_c_connect PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/stdc
)
target_link_libraries(test_ping_c_connect stdc ${TEST_PLATFORM_LIBS})

# P2P Ping 消息互发测试 (COMPACT 模式, 基于 instrument req/resp)
add_executable(test_ping_c_msg
    test_ping_c_msg.c
)
target_include_directories(test_ping_c_msg PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/stdc
)
target_link_libraries(test_ping_c_msg stdc ${TEST_PLATFORM_LIBS})

# P2P 重连测试集合 (COMPACT 模式, 双端崩溃重连 + 单端崩溃重连)
add_executable(test_ping_c_reconnect
    test_ping_c_reconnect.c
)
target_include_directories(test_ping_c_reconnect PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/stdc
)
target_link_libraries(test_ping_c_reconnect stdc ${TEST_PLATFORM_LIBS})

# P2P ICE 候选测试集合 (COMPACT 模式, 候选过滤验证)
add_executable(test_ping_c_filter
    test_ping_c_filter.c
)
target_include_directories(test_ping_c_filter PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/stdc
)
target_link_libraries(test_ping_c_filter stdc ${TEST_PLATFORM_LIBS})

# RELAY 协议注册/上线单元测试
add_executable(test_relay_register
    test_relay_register.c
)
target_include_directories(test_relay_register PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/stdc
)
target_link_libraries(test_relay_register stdc ${TEST_PLATFORM_LIBS})

# RELAY 协议生命周期单元测试 (ALIVE/FIN)
add_executable(test_relay_lifecycle
    test_relay_lifecycle.c
)
target_include_directories(test_relay_lifecycle PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/stdc
)
target_link_libraries(test_relay_lifecycle stdc ${TEST_PLATFORM_LIBS})

# RELAY 数据中继协议单元测试 (DATA/ACK/CRYPTO)
add_executable(test_relay_data
    test_relay_data.c
)
target_include_directories(test_relay_data PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/stdc
)
target_link_libraries(test_relay_data stdc ${TEST_PLATFORM_LIBS})

# RELAY RPC 协议单元测试
add_executable(test_relay_rpc
    test_relay_rpc.c
)
target_include_directories(test_relay_rpc PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/stdc
)
target_link_libraries(test_relay_rpc stdc ${TEST_PLATFORM_LIBS})

# P2P Ping 双客户端连接测试 (RELAY 模式, Alice <-> Bob)
add_executable(test_ping_r_connect
    test_ping_r_connect.c
)
target_include_directories(test_ping_r_connect PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/stdc
)
target_link_libraries(test_ping_r_connect stdc ${TEST_PLATFORM_LIBS})

# P2P Ping 消息互发测试 (RELAY 模式, 基于 instrument req/resp)
add_executable(test_ping_r_msg
    test_ping_r_msg.c
)
target_include_directories(test_ping_r_msg PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/stdc
)
target_link_libraries(test_ping_r_msg stdc ${TEST_PLATFORM_LIBS})

# P2P 重连测试集合 (RELAY 模式, 双端崩溃重连 + 单端崩溃重连)
add_executable(test_ping_r_reconnect
    test_ping_r_reconnect.c
)
target_include_directories(test_ping_r_reconnect PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/stdc
)
target_link_libraries(test_ping_r_reconnect stdc ${TEST_PLATFORM_LIBS})

# P2P ICE 候选测试集合 (RELAY 模式, 候选过滤验证)
add_executable(test_ping_r_filter
    test_ping_r_filter.c
)
target_include_directories(test_ping_r_filter PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/stdc
)
target_link_libraries(test_ping_r_filter stdc ${TEST_PLATFORM_LIBS})

# P2P Ping 双客户端连接测试 (PUBSUB 模式, Alice <-> Bob, GitHub Gist 信令)
add_executable(test_ping_g_connect
    test_ping_g_connect.c
)
target_include_directories(test_ping_g_connect PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/stdc
)
target_link_libraries(test_ping_g_connect stdc ${TEST_PLATFORM_LIBS})

# P2P PUBSUB 模式重连测试 (双端崩溃 + 单端崩溃, GitHub Gist 信令)
add_executable(test_ping_g_reconnect
    test_ping_g_reconnect.c
)
target_include_directories(test_ping_g_reconnect PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/stdc
)
target_link_libraries(test_ping_g_reconnect stdc ${TEST_PLATFORM_LIBS})
# WebSocket server + client 集成测试
if(WITH_WSLAY)
    add_executable(test_ws
        test_ws.c
    )
    target_include_directories(test_ws PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/p2p_server
        ${WSLAY_INCLUDE_DIRS}
    )
    target_link_libraries(test_ws
        ws_server
        p2p_static
        ${TEST_PLATFORM_LIBS}
    )
    target_compile_definitions(test_ws PRIVATE WITH_WSLAY)

    # p2p_server --ws 集成测试（启动真实 server 子进程）
    add_executable(test_ws_server_integration
        test_ws_server_integration.c
    )
    target_include_directories(test_ws_server_integration PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/p2p_server
        ${CMAKE_SOURCE_DIR}/stdc
        ${WSLAY_INCLUDE_DIRS}
    )
    target_link_libraries(test_ws_server_integration
        p2p_static
        ${TEST_PLATFORM_LIBS}
    )
    target_compile_definitions(test_ws_server_integration PRIVATE WITH_WSLAY)
    # register_* 是测试框架的占位函数，从不被调用，抑制 -Wunused-function 警告
    target_compile_options(test_ws_server_integration PRIVATE
        $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-Wno-unused-function>
    )

    # WebSocket 浏览器端对端手动演示（非自动化测试，不加入 CTest）
    # 用法：./test/test_ws_browser
    add_executable(test_ws_browser
        test_ws_browser.c
    )
    target_include_directories(test_ws_browser PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/p2p_server
        ${CMAKE_SOURCE_DIR}/stdc
        ${WSLAY_INCLUDE_DIRS}
    )
    target_link_libraries(test_ws_browser
        p2p_static
        ${TEST_PLATFORM_LIBS}
    )
    target_compile_definitions(test_ws_browser PRIVATE WITH_WSLAY)
endif()

# 添加测试
enable_testing()

# --- 独立单元测试（无外部依赖） ---
add_test(NAME test_transport     COMMAND test_transport)
if(WITH_WSLAY)
    add_test(NAME test_ws                    COMMAND test_ws)
    add_test(NAME test_ws_server_integration COMMAND test_ws_server_integration)
endif()

# --- COMPACT 协议测试（需要 p2p_server） ---
set(SERVER_PATH $<TARGET_FILE:p2p_server>)

add_test(NAME test_compact_register  COMMAND test_compact_register  ${SERVER_PATH})
add_test(NAME test_compact_sync      COMMAND test_compact_sync      ${SERVER_PATH})
add_test(NAME test_compact_lifecycle COMMAND test_compact_lifecycle  ${SERVER_PATH})
add_test(NAME test_compact_rpc       COMMAND test_compact_rpc       ${SERVER_PATH})
add_test(NAME test_compact_relay     COMMAND test_compact_relay      ${SERVER_PATH})

# --- RELAY 协议测试（需要 p2p_server） ---
add_test(NAME test_relay_register    COMMAND test_relay_register    ${SERVER_PATH})
add_test(NAME test_relay_lifecycle   COMMAND test_relay_lifecycle   ${SERVER_PATH})
add_test(NAME test_relay_data        COMMAND test_relay_data        ${SERVER_PATH})
add_test(NAME test_relay_rpc         COMMAND test_relay_rpc         ${SERVER_PATH})

# --- 端到端测试 COMPACT（需要 p2p_ping + p2p_server） ---
set(PING_PATH $<TARGET_FILE:p2p_ping>)

add_test(NAME test_ping_template     COMMAND test_ping_template     ${PING_PATH} ${SERVER_PATH})
add_test(NAME test_ping_c_connect    COMMAND test_ping_c_connect    ${PING_PATH} ${SERVER_PATH})
add_test(NAME test_ping_c_msg        COMMAND test_ping_c_msg        ${PING_PATH} ${SERVER_PATH})
add_test(NAME test_ping_c_filter     COMMAND test_ping_c_filter     ${PING_PATH} ${SERVER_PATH})
add_test(NAME test_ping_c_reconnect  COMMAND test_ping_c_reconnect  ${PING_PATH} ${SERVER_PATH})

# --- 端到端测试 RELAY（需要 p2p_ping + p2p_server） ---
add_test(NAME test_ping_r_connect    COMMAND test_ping_r_connect    ${PING_PATH} ${SERVER_PATH})
add_test(NAME test_ping_r_msg        COMMAND test_ping_r_msg        ${PING_PATH} ${SERVER_PATH})
add_test(NAME test_ping_r_filter     COMMAND test_ping_r_filter     ${PING_PATH} ${SERVER_PATH})
add_test(NAME test_ping_r_reconnect  COMMAND test_ping_r_reconnect  ${PING_PATH} ${SERVER_PATH})

# --- 端到端测试 PUBSUB（需要 p2p_ping + GitHub Token/Gist） ---
add_test(NAME test_ping_g_connect    COMMAND test_ping_g_connect    ${PING_PATH})
add_test(NAME test_ping_g_reconnect  COMMAND test_ping_g_reconnect  ${PING_PATH})

# --- 测试标签（用于分组运行：ctest -L unit / ctest -L protocol / ctest -L e2e） ---
set_tests_properties(test_transport PROPERTIES LABELS "unit")
if(WITH_WSLAY)
    set_tests_properties(test_ws test_ws_server_integration PROPERTIES LABELS "unit")
endif()

set_tests_properties(
    test_compact_register test_compact_sync test_compact_lifecycle
    test_compact_rpc test_compact_relay
    test_relay_register test_relay_lifecycle test_relay_data test_relay_rpc
    PROPERTIES LABELS "protocol"
)

set_tests_properties(
    test_ping_template test_ping_c_connect test_ping_c_msg
    test_ping_c_filter test_ping_c_reconnect
    test_ping_r_connect test_ping_r_msg test_ping_r_filter test_ping_r_reconnect
    test_ping_g_connect test_ping_g_reconnect
    PROPERTIES LABELS "e2e"
)

# 协议测试超时（启动子进程，需要额外裕量）
set_tests_properties(
    test_compact_register test_compact_sync test_compact_lifecycle
    test_compact_rpc test_compact_relay
    test_relay_register test_relay_lifecycle test_relay_data test_relay_rpc
    PROPERTIES TIMEOUT 30
)

# e2e 测试超时（内部有 30s connect timeout，需要更多裕量）
set_tests_properties(
    test_ping_template test_ping_c_connect test_ping_c_msg
    test_ping_c_filter test_ping_c_reconnect
    test_ping_r_connect test_ping_r_msg test_ping_r_filter test_ping_r_reconnect
    PROPERTIES TIMEOUT 120
)

# PUBSUB e2e 测试超时（Gist HTTP 轮询延迟大，reconnect 需要两轮连接）
set_tests_properties(
    PROPERTIES TIMEOUT 120
)

# PUBSUB e2e 测试超时（Gist HTTP 轮询延迟大，reconnect 需要两轮连接）
set_tests_properties(
    test_ping_g_connect
    PROPERTIES TIMEOUT 180
)
set_tests_properties(
    test_ping_g_reconnect
    PROPERTIES TIMEOUT 360
)

# PUBSUB 信令单元测试（单文件编译，fake 外部依赖）
add_executable(test_pubsub
    ${CMAKE_SOURCE_DIR}/src/p2p_signal_pubsub.c
    ${CMAKE_SOURCE_DIR}/src/p2p_http.c
    ${CMAKE_SOURCE_DIR}/src/p2p_crypto.c
    ${CMAKE_SOURCE_DIR}/src/p2p_ice.c
    ${CMAKE_SOURCE_DIR}/src/.LANG.c
    ${CMAKE_SOURCE_DIR}/i18n/i18n.c
)
target_compile_definitions(test_pubsub PRIVATE TEST_PUBSUB)
target_include_directories(test_pubsub PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/stdc
    ${CMAKE_SOURCE_DIR}/i18n
    ${CMAKE_SOURCE_DIR}/test
)
target_link_libraries(test_pubsub stdc ${TEST_PLATFORM_LIBS})
target_compile_options(test_pubsub PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-Wno-unused-function>
)

add_test(NAME test_pubsub COMMAND test_pubsub)
set_tests_properties(test_pubsub PROPERTIES LABELS "unit")
