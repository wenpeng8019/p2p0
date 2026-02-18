cmake_minimum_required(VERSION 3.10)
project(p2p VERSION 0.1.0 LANGUAGES C)

# --- Options ---
option(WITH_DTLS    "Enable MbedTLS DTLS support" OFF)
option(WITH_OPENSSL "Enable OpenSSL DTLS support" OFF)
option(WITH_SCTP    "Enable usrsctp support" OFF)
option(THREADED     "Enable internal threading" ON)

# --- Platform Detection ---
if(WIN32)
    message(STATUS "Platform: Windows")
    # Winsock2 is linked via #pragma comment in p2p_platform.h (MSVC),
    # but we also add it explicitly for MinGW / cross-compilers.
    set(PLATFORM_LIBS ws2_32 winhttp)
elseif(APPLE)
    message(STATUS "Platform: macOS")
    set(PLATFORM_LIBS "")
elseif(UNIX)
    message(STATUS "Platform: Linux/POSIX")
    # clock_gettime lives in librt on older glibc (< 2.17)
    find_library(RT_LIB rt)
    if(RT_LIB)
        set(PLATFORM_LIBS ${RT_LIB})
    else()
        set(PLATFORM_LIBS "")
    endif()
endif()

# --- Compiler Configuration ---
set(CMAKE_C_STANDARD 99)
if(MSVC)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /W3 /utf-8 /std:c11")
    # Disable "unsafe" CRT warnings that MSVC emits for standard POSIX functions
    add_definitions(-D_CRT_SECURE_NO_WARNINGS -D_WINSOCK_DEPRECATED_NO_WARNINGS)
else()
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Wpedantic")
endif()

# --- Include Directories ---
include_directories(include)
include_directories(src)

# --- Source Files ---
set(LIB_SRCS
    src/p2p_udp.c
    src/p2p_nat.c
    src/p2p_trans_reliable.c
    src/p2p_stream.c
    src/p2p_route.c
    src/p2p.c
    src/p2p_stun.c
    src/p2p_ice.c
    src/p2p_turn.c
    src/p2p_tcp_punch.c
    src/p2p_crypto.c
    src/p2p_trans_pseudotcp.c
    src/p2p_signal_relay.c
    src/p2p_crypto_extra.c
    src/p2p_signal_pubsub.c
    src/p2p_signal_compact.c
    src/p2p_log.c
    src/p2p_http.c
)

if(THREADED)
    add_definitions(-DP2P_THREADED)
    list(APPEND LIB_SRCS src/p2p_thread.c)
    find_package(Threads REQUIRED)
endif()

# --- MbedTLS Integration ---
if(WITH_DTLS)
    add_definitions(-DWITH_DTLS)
    list(APPEND LIB_SRCS src/p2p_trans_mbedtls.c)
    
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/mbedtls/CMakeLists.txt")
        # Build vendored mbedtls
        set(ENABLE_TESTING OFF CACHE BOOL "")
        set(ENABLE_PROGRAMS OFF CACHE BOOL "")
        add_subdirectory(third_party/mbedtls EXCLUDE_FROM_ALL)
        include_directories(third_party/mbedtls/include)
        set(DTLS_LIBRARIES mbedtls mbedx509 mbedcrypto)
    else()
        # Fallback to system mbedtls (macOS/Homebrew focus)
        find_path(MBED_INC mbedtls/ssl.h PATHS /opt/homebrew/include)
        find_library(MBED_LIB mbedtls PATHS /opt/homebrew/lib)
        find_library(MBED_X509 mbedx509 PATHS /opt/homebrew/lib)
        find_library(MBED_CRYPTO mbedcrypto PATHS /opt/homebrew/lib)
        include_directories(${MBED_INC})
        set(DTLS_LIBRARIES ${MBED_LIB} ${MBED_X509} ${MBED_CRYPTO})
    endif()
endif()

# --- OpenSSL Integration ---
if(WITH_OPENSSL)
    add_definitions(-DWITH_OPENSSL)
    list(APPEND LIB_SRCS src/p2p_trans_openssl.c)
    find_package(OpenSSL REQUIRED)
    include_directories(${OPENSSL_INCLUDE_DIR})
    set(OPENSSL_LIBRARIES OpenSSL::SSL OpenSSL::Crypto)
endif()

# --- usrsctp Integration ---
if(WITH_SCTP)
    add_definitions(-DWITH_SCTP)
    list(APPEND LIB_SRCS src/p2p_trans_sctp.c)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/usrsctp/CMakeLists.txt")
        set(sctp_build_shared OFF CACHE BOOL "")
        set(sctp_build_programs OFF CACHE BOOL "")
        add_subdirectory(third_party/usrsctp EXCLUDE_FROM_ALL)
        include_directories(third_party/usrsctp/usrsctplib)
        set(SCTP_LIBRARIES usrsctp)
    endif()
endif()

# --- Target Definition ---
add_library(p2p_static STATIC ${LIB_SRCS})
target_link_libraries(p2p_static ${PLATFORM_LIBS})
if(THREADED)
    target_link_libraries(p2p_static Threads::Threads)
endif()

# --- Signaling Server ---
add_subdirectory(p2p_server)

# --- p2p_ping Utility ---
add_subdirectory(p2p_ping)

# --- Tests ---
enable_testing()
add_subdirectory(test)
