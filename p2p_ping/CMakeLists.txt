cmake_minimum_required(VERSION 3.10)
project(p2p_ping LANGUAGES C)

set(CMAKE_C_STANDARD 99)
if(MSVC)
    # /W3: Enable level 3 warnings
    # /wd4068: Disable warning C4068 ("unknown pragma")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /W3 /utf-8 /wd4068")
else()
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Wno-unknown-pragmas")
endif()

add_executable(p2p_ping p2p_ping.c ping_lang.c)
target_include_directories(p2p_ping PRIVATE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(p2p_ping PRIVATE p2p_static ${DTLS_LIBRARIES} ${OPENSSL_LIBRARIES} ${SCTP_LIBRARIES})
