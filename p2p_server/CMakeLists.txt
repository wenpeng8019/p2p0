cmake_minimum_required(VERSION 3.10)
project(p2p_server LANGUAGES C)

set(CMAKE_C_STANDARD 99)
if(MSVC)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /W3")
    add_definitions(-D_CRT_SECURE_NO_WARNINGS -D_WINSOCK_DEPRECATED_NO_WARNINGS)
else()
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra")
endif()

add_executable(p2p_server server.c)
target_include_directories(p2p_server PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)
target_link_libraries(p2p_server PRIVATE ${PLATFORM_LIBS})
if(WIN32)
    target_link_libraries(p2p_server PRIVATE ws2_32)
endif()
