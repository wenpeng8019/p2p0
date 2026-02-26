cmake_minimum_required(VERSION 3.10)
project(p2p_server LANGUAGES C)

set(CMAKE_C_STANDARD 99)
if(MSVC)
    # /W3: Enable level 3 warnings
    # /wd4068: Disable warning C4068 ("unknown pragma")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /W3 /utf-8 /wd4068 /std:c11")
    add_definitions(-D_CRT_SECURE_NO_WARNINGS -D_WINSOCK_DEPRECATED_NO_WARNINGS)
else()
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Wno-unknown-pragmas")
endif()

# Run i18n.sh before building to (re)generate .LANG.h and .LANG.c
add_custom_command(
    OUTPUT
        ${CMAKE_CURRENT_SOURCE_DIR}/.LANG.h
        ${CMAKE_CURRENT_SOURCE_DIR}/.LANG.c
    COMMAND ${CMAKE_SOURCE_DIR}/i18n/i18n.sh ${CMAKE_CURRENT_SOURCE_DIR}
    DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/server.c
        ${CMAKE_SOURCE_DIR}/i18n/i18n.sh
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Generating i18n language files for p2p_server"
)

add_executable(p2p_server
    server.c
    ${CMAKE_CURRENT_SOURCE_DIR}/.LANG.c
    ${CMAKE_SOURCE_DIR}/i18n/i18n.c
)
target_include_directories(p2p_server PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${CMAKE_SOURCE_DIR}/i18n
)

# Enable i18n multi-language support
if(I18N_ENABLED)
    target_compile_definitions(p2p_server PRIVATE I18N_ENABLED)
    message(STATUS "p2p_server: i18n multi-language support ENABLED")
else()
    message(STATUS "p2p_server: i18n multi-language support DISABLED (using string literals)")
endif()

target_link_libraries(p2p_server PRIVATE ${PLATFORM_LIBS})
if(WIN32)
    target_link_libraries(p2p_server PRIVATE ws2_32)
endif()
