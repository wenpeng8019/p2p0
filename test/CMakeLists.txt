cmake_minimum_required(VERSION 3.10)

# 测试可执行文件
add_executable(test_transport
    test_transport.c
)

# 链接静态库
target_link_libraries(test_transport
    p2p_static
)

# SIMPLE 服务端完整测试（独立运行，不依赖 p2p 库）
add_executable(test_compact_server
    test_compact_server.c
)

# Relay 服务端完整测试（独立运行，不依赖 p2p 库）
add_executable(test_relay_server
    test_relay_server.c
)

# SIMPLE 协议完整测试（独立运行，测试协议本身）
add_executable(test_compact_protocol
    test_compact_protocol.c
)

# 添加测试
enable_testing()
add_test(NAME test_transport COMMAND test_transport)
add_test(NAME test_compact_server COMMAND test_compact_server)
add_test(NAME test_relay_server COMMAND test_relay_server)
add_test(NAME test_compact_protocol COMMAND test_compact_protocol)
