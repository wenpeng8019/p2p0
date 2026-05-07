cmake_minimum_required(VERSION 3.10)
project(p2p_server LANGUAGES C)

set(CMAKE_C_STANDARD 99)
if(MSVC)
    # /W3: 启用三级警告
    # /wd4068: 禁用 C4068 警告 ("unknown pragma")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /W3 /utf-8 /wd4068 /std:c11")
    add_definitions(-D_CRT_SECURE_NO_WARNINGS -D_WINSOCK_DEPRECATED_NO_WARNINGS)
else()
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Wno-unknown-pragmas")
endif()

# i18n 代码生成: Debug → 基于 SID 的稳定 ID; Release → --ndebug 紧凑 ID
# I18N_ENABLED=ON:  跟踪源文件变化，修改时重新生成
# I18N_ENABLED=OFF: 文件不存在时生成一次（配置时）
set(_server_i18n_outputs
    ${CMAKE_CURRENT_SOURCE_DIR}/.LANG.h
    ${CMAKE_CURRENT_SOURCE_DIR}/.LANG.c
)
if(I18N_CN)
    list(APPEND _server_i18n_outputs ${CMAKE_CURRENT_SOURCE_DIR}/LANG.cn.h)
endif()

if(I18N_ENABLED)
    add_custom_command(
        OUTPUT ${_server_i18n_outputs}
        COMMAND ${I18N_SCRIPT} ${CMAKE_CURRENT_SOURCE_DIR} --name server ${I18N_IMPORT_FLAG} ${I18N_NDEBUG_FLAG}
        DEPENDS
            ${CMAKE_CURRENT_SOURCE_DIR}/server.c
            ${I18N_SCRIPT}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Generating i18n language files for p2p_server"
    )
    add_custom_target(p2p_server_i18n_gen DEPENDS ${_server_i18n_outputs})
else()
    set(_need_gen OFF)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.LANG.h" OR NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.LANG.c")
        set(_need_gen ON)
    endif()
    if(I18N_CN AND NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/LANG.cn.h")
        set(_need_gen ON)
    endif()
    if(_need_gen)
        message(STATUS "为 p2p_server 生成 i18n 文件（一次性，I18N_ENABLED=OFF）")
        execute_process(
            COMMAND ${I18N_SCRIPT} ${CMAKE_CURRENT_SOURCE_DIR} --name server ${I18N_IMPORT_FLAG} ${I18N_NDEBUG_FLAG}
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            RESULT_VARIABLE _i18n_result
        )
        if(NOT _i18n_result EQUAL 0)
            message(WARNING "i18n generation failed for p2p_server (exit ${_i18n_result})")
        endif()
    endif()
endif()

add_executable(p2p_server
    server.c
    ${CMAKE_CURRENT_SOURCE_DIR}/.LANG.c
    ${CMAKE_SOURCE_DIR}/i18n/i18n.c
        common.h
        custom_tcp.c
        custom_tcp.h
        custom_ws.c
        custom_ws.h
        p2p_relay.c
        p2p_relay.h
        p2p_compact.c
        p2p_compact.h
        p2p_wss.c
        p2p_wss.h
)
target_include_directories(p2p_server PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_SOURCE_DIR}/i18n
    ${CMAKE_SOURCE_DIR}/stdc
)

# 启用 i18n 多语言支持
if(I18N_ENABLED)
    target_compile_definitions(p2p_server PRIVATE I18N_ENABLED)
    add_dependencies(p2p_server p2p_server_i18n_gen)
    message(STATUS "p2p_server: i18n 多语言支持已启用")
else()
    message(STATUS "p2p_server: i18n 多语言支持已禁用（使用字符串字面量）")
endif()

target_link_libraries(p2p_server PRIVATE stdc ${PLATFORM_LIBS})
if(WIN32)
    target_link_libraries(p2p_server PRIVATE ws2_32)
endif()

# --- wslay WebSocket 可选支持 ---
if(WITH_WSLAY)
    target_link_libraries(p2p_server PRIVATE ${WSLAY_LIBRARIES})
    target_include_directories(p2p_server PRIVATE ${WSLAY_INCLUDE_DIRS})
    target_compile_definitions(p2p_server PRIVATE WITH_WSLAY)

    message(STATUS "p2p_server: ws_server WebSocket 服务器已启用")
endif()
