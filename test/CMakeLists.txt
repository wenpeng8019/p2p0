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

# NAT API 统一单元测试（独立运行，测试批量/单候选模式、状态转换）
add_executable(test_nat_api
    test_nat_api.c
    ${CMAKE_SOURCE_DIR}/src/.LANG.c
    ${CMAKE_SOURCE_DIR}/i18n/i18n.c
)
target_link_libraries(test_nat_api p2p_static)
target_include_directories(test_nat_api PRIVATE ${CMAKE_SOURCE_DIR}/i18n)
if(I18N_ENABLED)
    target_compile_definitions(test_nat_api PRIVATE I18N_ENABLED)
endif()
add_test(NAME test_nat_api COMMAND test_nat_api)

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
add_executable(test_compact_peer_info
    test_compact_peer_info.c
)
target_include_directories(test_compact_peer_info PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/stdc
)
target_link_libraries(test_compact_peer_info stdc ${TEST_PLATFORM_LIBS})

# COMPACT 生命周期协议单元测试 (ALIVE/UNREGISTER/PEER_OFF)
add_executable(test_compact_lifecycle
    test_compact_lifecycle.c
)
target_include_directories(test_compact_lifecycle PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/stdc
)
target_link_libraries(test_compact_lifecycle stdc ${TEST_PLATFORM_LIBS})

# COMPACT MSG RPC 协议单元测试
add_executable(test_compact_msg
    test_compact_msg.c
)
target_include_directories(test_compact_msg PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/stdc
)
target_link_libraries(test_compact_msg stdc ${TEST_PLATFORM_LIBS})

# COMPACT RELAY/NAT_PROBE 协议单元测试
add_executable(test_compact_relay
    test_compact_relay.c
)
target_include_directories(test_compact_relay PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/stdc
)
target_link_libraries(test_compact_relay stdc ${TEST_PLATFORM_LIBS})

# P2P Ping 客户端单元测试模版
add_executable(test_ping_template
    test_ping_template.c
)
target_include_directories(test_ping_template PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/stdc
)
target_link_libraries(test_ping_template stdc ${TEST_PLATFORM_LIBS})

# P2P Ping 双客户端连接测试模板（Alice <-> Bob）
add_executable(test_ping2_template
    test_ping2_template.c
)
target_include_directories(test_ping2_template PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/stdc
)
target_link_libraries(test_ping2_template stdc ${TEST_PLATFORM_LIBS})

# P2P Ping 消息互发测试（基于 instrument req/resp）
add_executable(test_ping_msg
    test_ping_msg.c
)
target_include_directories(test_ping_msg PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/stdc
)
target_link_libraries(test_ping_msg stdc ${TEST_PLATFORM_LIBS})

# P2P 重连测试集合（双端崩溃重连 + 单端崩溃重连）
add_executable(test_ping_reconnect
    test_ping_reconnect.c
)
target_include_directories(test_ping_reconnect PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/stdc
)
target_link_libraries(test_ping_reconnect stdc ${TEST_PLATFORM_LIBS})

# P2P ICE 候选测试集合（候选过滤验证）
add_executable(test_ping_ice
    test_ping_ice.c
)
target_include_directories(test_ping_ice PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/stdc
)
target_link_libraries(test_ping_ice stdc ${TEST_PLATFORM_LIBS})

# 添加测试
enable_testing()
add_test(NAME test_transport     COMMAND test_transport)
