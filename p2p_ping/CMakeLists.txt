cmake_minimum_required(VERSION 3.10)
project(p2p_ping LANGUAGES C)

set(CMAKE_C_STANDARD 99)
if(MSVC)
    # /W3: 启用三级警告
    # /wd4068: 禁用 C4068 警告 ("unknown pragma")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /W3 /utf-8 /wd4068")
else()
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Wno-unknown-pragmas")
endif()

# i18n 代码生成: Debug → 基于 SID 的稳定 ID; Release → --ndebug 紧凑 ID
# I18N_ENABLED=ON:  跟踪源文件变化，修改时重新生成
# I18N_ENABLED=OFF: 文件不存在时生成一次（配置时）
set(_ping_i18n_outputs
    ${CMAKE_CURRENT_SOURCE_DIR}/.LANG.h
    ${CMAKE_CURRENT_SOURCE_DIR}/.LANG.c
)
if(I18N_CN)
    list(APPEND _ping_i18n_outputs ${CMAKE_CURRENT_SOURCE_DIR}/LANG.cn.h)
endif()

if(I18N_ENABLED)
    add_custom_command(
        OUTPUT ${_ping_i18n_outputs}
        COMMAND ${I18N_SCRIPT} ${CMAKE_CURRENT_SOURCE_DIR} --name ping ${I18N_IMPORT_FLAG} ${I18N_NDEBUG_FLAG}
        DEPENDS
            ${CMAKE_CURRENT_SOURCE_DIR}/p2p_ping.c
            ${I18N_SCRIPT}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Generating i18n language files for p2p_ping"
    )
    add_custom_target(p2p_ping_i18n_gen DEPENDS ${_ping_i18n_outputs})
else()
    set(_need_gen OFF)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.LANG.h" OR NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.LANG.c")
        set(_need_gen ON)
    endif()
    if(I18N_CN AND NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/LANG.cn.h")
        set(_need_gen ON)
    endif()
    if(_need_gen)
        message(STATUS "为 p2p_ping 生成 i18n 文件（一次性，I18N_ENABLED=OFF）")
        execute_process(
            COMMAND ${I18N_SCRIPT} ${CMAKE_CURRENT_SOURCE_DIR} --name ping ${I18N_IMPORT_FLAG} ${I18N_NDEBUG_FLAG}
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            RESULT_VARIABLE _i18n_result
        )
        if(NOT _i18n_result EQUAL 0)
            message(WARNING "i18n generation failed for p2p_ping (exit ${_i18n_result})")
        endif()
    endif()
endif()

add_executable(p2p_ping 
    p2p_ping.c 
    ${CMAKE_CURRENT_SOURCE_DIR}/.LANG.c
    ${CMAKE_SOURCE_DIR}/src/.LANG.c
    ${CMAKE_SOURCE_DIR}/i18n/i18n.c
)
target_include_directories(p2p_ping PRIVATE 
    ${CMAKE_SOURCE_DIR} 
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/i18n
    ${CMAKE_SOURCE_DIR}/stdc
)

# 启用 i18n 多语言支持
if(I18N_ENABLED)
    target_compile_definitions(p2p_ping PRIVATE I18N_ENABLED)
    add_dependencies(p2p_ping p2p_ping_i18n_gen)
    message(STATUS "p2p_ping: i18n 多语言支持已启用")
else()
    message(STATUS "p2p_ping: i18n 多语言支持已禁用（使用字符串字面量）")
endif()

target_link_libraries(p2p_ping PRIVATE 
    p2p_static 
    stdc
    ${DTLS_LIBRARIES} 
    ${OPENSSL_LIBRARIES} 
    ${SCTP_LIBRARIES}
)
