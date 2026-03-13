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

# RELAY 服务端完整测试（独立运行，不依赖 p2p 库）
add_executable(test_relay_server
    test_relay_server.c
)
target_include_directories(test_relay_server PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/stdc)
target_link_libraries(test_relay_server ${TEST_PLATFORM_LIBS})

# RELAY 异步 I/O 状态机单元测试（独立运行，模拟完整状态机）
add_executable(test_relay_async_io
    test_relay_async_io.c
)
target_include_directories(test_relay_async_io PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/stdc)
target_link_libraries(test_relay_async_io ${TEST_PLATFORM_LIBS})
add_test(NAME test_relay_async_io COMMAND test_relay_async_io)

# Ring Buffer 单元测试（独立运行，测试环形缓冲区所有操作）
add_executable(test_ring_buffer
    test_ring_buffer.c
)
target_include_directories(test_ring_buffer PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/stdc)
target_link_libraries(test_ring_buffer ${TEST_PLATFORM_LIBS})
add_test(NAME test_ring_buffer COMMAND test_ring_buffer)

# Reliable 传输层单元测试（独立运行，测试 ARQ 滑动窗口机制）
add_executable(test_reliable
    test_reliable.c
)
target_include_directories(test_reliable PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/stdc)
target_link_libraries(test_reliable p2p_static ${TEST_PLATFORM_LIBS})
add_test(NAME test_reliable COMMAND test_reliable)

# PseudoTCP 拥塞控制单元测试（独立运行，测试 AIMD 拥塞控制算法）
add_executable(test_pseudotcp
    test_pseudotcp.c
)
target_include_directories(test_pseudotcp PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/stdc)
target_link_libraries(test_pseudotcp ${TEST_PLATFORM_LIBS})
add_test(NAME test_pseudotcp COMMAND test_pseudotcp)

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

# 添加测试
enable_testing()
add_test(NAME test_transport     COMMAND test_transport)
add_test(NAME test_relay_server   COMMAND test_relay_server)
