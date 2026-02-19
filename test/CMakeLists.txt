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
)

# 链接静态库
target_link_libraries(test_transport
    p2p_static
)

# COMPACT 服务端完整测试（独立运行，不依赖 p2p 库）
add_executable(test_compact_server
    test_compact_server.c
)
target_link_libraries(test_compact_server ${TEST_PLATFORM_LIBS})

# RELAY 服务端完整测试（独立运行，不依赖 p2p 库）
add_executable(test_relay_server
    test_relay_server.c
)
target_link_libraries(test_relay_server ${TEST_PLATFORM_LIBS})

# COMPACT 协议完整测试（独立运行，测试协议本身）
add_executable(test_compact_protocol
    test_compact_protocol.c
)
target_link_libraries(test_compact_protocol ${TEST_PLATFORM_LIBS})

# RELAY 协议完整测试（独立运行，测试 CONNECT_ACK 三状态逻辑）
add_executable(test_relay_protocol
    test_relay_protocol.c
)
target_link_libraries(test_relay_protocol ${TEST_PLATFORM_LIBS})

# PUBSUB 协议完整测试（独立运行，测试 DES 加密、JSON 解析）
add_executable(test_pubsub_protocol
    test_pubsub_protocol.c
)
target_link_libraries(test_pubsub_protocol ${TEST_PLATFORM_LIBS})

# ICE 协议完整测试（独立运行，测试优先级、候选、checklist）
add_executable(test_ice_protocol
    test_ice_protocol.c
)
target_link_libraries(test_ice_protocol ${TEST_PLATFORM_LIBS})
add_test(NAME test_ice_protocol COMMAND test_ice_protocol)

# STUN 协议完整测试（独立运行，测试包格式、XOR 解码、NAT 类型检测逻辑）
add_executable(test_stun_protocol
    test_stun_protocol.c
)
target_link_libraries(test_stun_protocol ${TEST_PLATFORM_LIBS})
add_test(NAME test_stun_protocol COMMAND test_stun_protocol)

# TURN 协议完整测试（独立运行，测试 Allocate/响应）
add_executable(test_turn_protocol
    test_turn_protocol.c
)
target_link_libraries(test_turn_protocol ${TEST_PLATFORM_LIBS})
add_test(NAME test_turn_protocol COMMAND test_turn_protocol)

# RELAY 异步 I/O 状态机单元测试（独立运行，模拟完整状态机）
add_executable(test_relay_async_io
    test_relay_async_io.c
)
target_link_libraries(test_relay_async_io ${TEST_PLATFORM_LIBS})
add_test(NAME test_relay_async_io COMMAND test_relay_async_io)

# Ring Buffer 单元测试（独立运行，测试环形缓冲区所有操作）
add_executable(test_ring_buffer
    test_ring_buffer.c
)
target_link_libraries(test_ring_buffer ${TEST_PLATFORM_LIBS})
add_test(NAME test_ring_buffer COMMAND test_ring_buffer)

# Reliable 传输层单元测试（独立运行，测试 ARQ 滑动窗口机制）
add_executable(test_reliable
    test_reliable.c
)
target_link_libraries(test_reliable ${TEST_PLATFORM_LIBS})
add_test(NAME test_reliable COMMAND test_reliable)

# PseudoTCP 拥塞控制单元测试（独立运行，测试 AIMD 拥塞控制算法）
add_executable(test_pseudotcp
    test_pseudotcp.c
)
target_link_libraries(test_pseudotcp ${TEST_PLATFORM_LIBS})
add_test(NAME test_pseudotcp COMMAND test_pseudotcp)

# 添加测试
enable_testing()
add_test(NAME test_transport     COMMAND test_transport)
add_test(NAME test_compact_server COMMAND test_compact_server)
add_test(NAME test_relay_server   COMMAND test_relay_server)
add_test(NAME test_compact_protocol COMMAND test_compact_protocol)
add_test(NAME test_relay_protocol COMMAND test_relay_protocol)
add_test(NAME test_pubsub_protocol COMMAND test_pubsub_protocol)
