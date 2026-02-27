cmake_minimum_required(VERSION 3.10)
project(p2p VERSION 0.1.0 LANGUAGES C)

# --- Options ---
option(WITH_DTLS    "Enable MbedTLS DTLS support" OFF)
option(WITH_OPENSSL "Enable OpenSSL DTLS support" OFF)
option(WITH_SCTP    "Enable usrsctp support" OFF)
option(THREADED     "Enable internal threading" ON)
option(I18N_ENABLED "Enable i18n multi-language support" ON)

# --- Platform Detection ---
if(WIN32)
    message(STATUS "Platform: Windows")
    # Winsock2 is linked via #pragma comment in p2p_platform.h (MSVC),
    # but we also add it explicitly for MinGW / cross-compilers.
    set(PLATFORM_LIBS ws2_32 winhttp iphlpapi)
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
    # /W3: Enable level 3 warnings
    # /wd4068: Disable warning C4068 ("unknown pragma")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /W3 /utf-8 /wd4068 /std:c11")
    # Disable "unsafe" CRT warnings that MSVC emits for standard POSIX functions
    add_definitions(-D_CRT_SECURE_NO_WARNINGS -D_WINSOCK_DEPRECATED_NO_WARNINGS)
else()
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Wno-unknown-pragmas -Wpedantic")
endif()

# --- Include Directories ---
include_directories(include)

# --- i18n submodule check ---
if(NOT EXISTS "${CMAKE_SOURCE_DIR}/i18n/i18n.h")
    message(FATAL_ERROR
        "i18n submodule not found. Please run:\n"
        "  git submodule update --init\n"
        "then re-run cmake.")
endif()
include_directories(i18n)

# --- stdc submodule check ---
if(NOT EXISTS "${CMAKE_SOURCE_DIR}/stdc/stdc.h")
    message(FATAL_ERROR
        "stdc submodule not found. Please run:\n"
        "  git submodule update --init\n"
        "then re-run cmake.")
endif()
add_subdirectory(stdc)

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
    src/p2p_http.c
)

# --- i18n code generation for p2p core library ---
add_custom_command(
    OUTPUT
        ${CMAKE_SOURCE_DIR}/src/.LANG.h
        ${CMAKE_SOURCE_DIR}/src/.LANG.c
    COMMAND ${CMAKE_SOURCE_DIR}/i18n/i18n.sh ${CMAKE_SOURCE_DIR}/src
    DEPENDS
        ${LIB_SRCS}
        ${CMAKE_SOURCE_DIR}/i18n/i18n.sh
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Generating i18n language files for p2p core library"
)

# When I18N_ENABLED, compile the generated table + runtime into the library
#
# ARCHITECTURE NOTE: p2p_static itself does NOT link i18n.c
# (p2p_ping / p2p_server each supply their own independent i18n instance).
# The library sources use LA_W/LA_S/LA_F markers that expand to string literals
# by default; with I18N_ENABLED they call lang_str() which is resolved at
# link time by the consuming executable's i18n.c.
#
# To enable runtime translation for library logs, set I18N_ENABLED=ON and
# ensure the consuming executable supplies a unified lang table that covers
# both its own strings and the library's IDs (future architecture work).
if(I18N_ENABLED)
    message(STATUS "p2p core: I18N_ENABLED — library strings use LA_X markers (ID-based lookup at link time)")
else()
    message(STATUS "p2p core: API markers only — LA_X expands to string literals")
endif()

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
target_include_directories(p2p_static PRIVATE src)
target_link_libraries(p2p_static stdc ${PLATFORM_LIBS})
if(I18N_ENABLED)
    target_compile_definitions(p2p_static PRIVATE I18N_ENABLED)
endif()
if(THREADED)
    target_link_libraries(p2p_static Threads::Threads)
endif()

# Run i18n.sh on src/ to assign IDs in source files and generate src/.LANG.h/.LANG.c.
# p2p_static depends on this target so i18n.sh runs before any source is compiled.
# (src/.LANG.h is only #include'd when I18N_ENABLED, so it is only mandatory then,
#  but running i18n.sh unconditionally keeps source IDs fresh.)
add_custom_target(p2p_core_i18n_gen DEPENDS
    ${CMAKE_SOURCE_DIR}/src/.LANG.h
    ${CMAKE_SOURCE_DIR}/src/.LANG.c
)
add_dependencies(p2p_static p2p_core_i18n_gen)

# --- Signaling Server ---
add_subdirectory(p2p_server)

# --- p2p_ping Utility ---
add_subdirectory(p2p_ping)

# --- Tests ---
enable_testing()
add_subdirectory(test)
