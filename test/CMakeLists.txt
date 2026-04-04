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

# P2P Ping 客户端单元测试模版 (COMPACT 模式)
add_executable(test_ping_c_template
    test_ping_c_template.c
)
target_include_directories(test_ping_c_template PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/stdc
)
target_link_libraries(test_ping_c_template stdc ${TEST_PLATFORM_LIBS})

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
add_executable(test_ping_c_sync
    test_ping_c_sync.c
)
target_include_directories(test_ping_c_sync PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/stdc
)
target_link_libraries(test_ping_c_sync stdc ${TEST_PLATFORM_LIBS})

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

# P2P Ping 客户端单元测试模版 (RELAY 模式)
add_executable(test_ping_r_template
    test_ping_r_template.c
)
target_include_directories(test_ping_r_template PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/stdc
)
target_link_libraries(test_ping_r_template stdc ${TEST_PLATFORM_LIBS})

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
add_executable(test_ping_r_sync
    test_ping_r_sync.c
)
target_include_directories(test_ping_r_sync PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/stdc
)
target_link_libraries(test_ping_r_sync stdc ${TEST_PLATFORM_LIBS})

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
endif()

# 添加测试
enable_testing()
add_test(NAME test_transport     COMMAND test_transport)
if(WITH_WSLAY)
    add_test(NAME test_ws        COMMAND test_ws)
endif()
