cmake_minimum_required(VERSION 3.10)

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

# RELAY 服务端完整测试（独立运行，不依赖 p2p 库）
add_executable(test_relay_server
    test_relay_server.c
)

# COMPACT 协议完整测试（独立运行，测试协议本身）
add_executable(test_compact_protocol
    test_compact_protocol.c
)

# RELAY 协议完整测试（独立运行，测试 CONNECT_ACK 三状态逻辑）
add_executable(test_relay_protocol
    test_relay_protocol.c
)

# PUBSUB 协议完整测试（独立运行，测试 DES 加密、JSON 解析）
add_executable(test_pubsub_protocol
    test_pubsub_protocol.c
)

# ICE 协议完整测试（独立运行，测试优先级、候选、checklist）
add_executable(test_ice_protocol
    test_ice_protocol.c
)
add_test(NAME test_ice_protocol COMMAND test_ice_protocol)

# STUN 协议完整测试（独立运行，测试包格式、XOR 解码、NAT 类型检测逻辑）
add_executable(test_stun_protocol
    test_stun_protocol.c
)
add_test(NAME test_stun_protocol COMMAND test_stun_protocol)

# TURN 协议完整测试（独立运行，测试 Allocate/响应）
add_executable(test_turn_protocol
    test_turn_protocol.c
)
add_test(NAME test_turn_protocol COMMAND test_turn_protocol)

# 添加测试
enable_testing()
add_test(NAME test_transport COMMAND test_transport)
add_test(NAME test_compact_server COMMAND test_compact_server)
add_test(NAME test_relay_server COMMAND test_relay_server)
add_test(NAME test_compact_protocol COMMAND test_compact_protocol)
add_test(NAME test_relay_protocol COMMAND test_relay_protocol)
add_test(NAME test_pubsub_protocol COMMAND test_pubsub_protocol)
