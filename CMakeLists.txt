# CMake build system for stellar-core
# Minimum CMake version for modern C++ support and FetchContent
cmake_minimum_required(VERSION 3.16)

project(stellar-core VERSION 0.1 LANGUAGES C CXX)

# ============================================================================
# Build Options
# ============================================================================

option(USE_POSTGRES "Enable PostgreSQL support" ON)
option(BUILD_TESTS "Build test suite" ON)
option(USE_SPDLOG "Enable spdlog logging" ON)
option(USE_TRACY "Enable Tracy profiler" OFF)
option(USE_TRACY_MEMORY_TRACKING "Enable Tracy memory tracking (slow)" OFF)
option(ENABLE_NEXT_PROTOCOL_VERSION "Enable next protocol version (UNSAFE FOR PRODUCTION)" OFF)
option(USE_LIBUNWIND "Enable libunwind for backtraces" ON)
option(ENABLE_ASAN "Enable Address Sanitizer" OFF)
option(ENABLE_TSAN "Enable Thread Sanitizer" OFF)
option(ENABLE_MSAN "Enable Memory Sanitizer" OFF)
option(ENABLE_UBSAN "Enable Undefined Behavior Sanitizer" OFF)
option(ENABLE_CCACHE "Use ccache for compilation" OFF)
option(UNIFIED_RUST "Build rust crates as single cargo library (for sanitizers)" OFF)

# ============================================================================
# C++ Standard and Compiler Settings
# ============================================================================

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Export compile_commands.json for IDE support
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Position independent code for static libraries
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ============================================================================
# Detect Platform
# ============================================================================

if(APPLE)
    set(PLATFORM_MACOS TRUE)
elseif(UNIX)
    set(PLATFORM_LINUX TRUE)
elseif(WIN32)
    set(PLATFORM_WINDOWS TRUE)
endif()

# ============================================================================
# Compiler Warnings
# ============================================================================

set(STELLAR_WARNING_FLAGS
    -Wall
    -Wno-unused-command-line-argument
    -Qunused-arguments
    -Wno-unused-local-typedef
    -Wno-unknown-warning-option
    -Werror=unused-result
)

# Check if using libc++
if(CMAKE_CXX_FLAGS MATCHES "-stdlib=libc\\+\\+")
    set(USING_LIBCXX TRUE)
endif()

# ============================================================================
# Sanitizers
# ============================================================================

if(ENABLE_ASAN)
    message(STATUS "Enabling Address Sanitizer")
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address)
    if(NOT UNIFIED_RUST)
        message(WARNING "ASAN works best with UNIFIED_RUST=ON")
    endif()
endif()

if(ENABLE_TSAN)
    if(ENABLE_ASAN OR ENABLE_MSAN OR ENABLE_UBSAN)
        message(FATAL_ERROR "Cannot enable multiple sanitizers at once")
    endif()
    if(NOT UNIFIED_RUST)
        message(FATAL_ERROR "TSAN requires UNIFIED_RUST=ON")
    endif()
    message(STATUS "Enabling Thread Sanitizer")
    add_compile_options(-fsanitize=thread -fno-omit-frame-pointer)
    add_link_options(-fsanitize=thread)
endif()

if(ENABLE_MSAN)
    if(ENABLE_ASAN OR ENABLE_TSAN OR ENABLE_UBSAN)
        message(FATAL_ERROR "Cannot enable multiple sanitizers at once")
    endif()
    message(STATUS "Enabling Memory Sanitizer")
    add_compile_options(-fsanitize=memory -fsanitize-memory-track-origins=2 -fsanitize-memory-use-after-dtor -fno-omit-frame-pointer)
    add_link_options(-fsanitize=memory -fsanitize-memory-track-origins=2 -fsanitize-memory-use-after-dtor)
    add_compile_definitions(MSAN_ENABLED)
endif()

if(ENABLE_UBSAN)
    if(ENABLE_ASAN OR ENABLE_TSAN OR ENABLE_MSAN)
        message(FATAL_ERROR "Cannot enable multiple sanitizers at once")
    endif()
    message(STATUS "Enabling Undefined Behavior Sanitizer")
    add_compile_options(-fsanitize=undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=undefined)
endif()

# ============================================================================
# ccache
# ============================================================================

if(ENABLE_CCACHE)
    find_program(CCACHE_PROGRAM ccache)
    if(CCACHE_PROGRAM)
        set(CMAKE_C_COMPILER_LAUNCHER ${CCACHE_PROGRAM})
        set(CMAKE_CXX_COMPILER_LAUNCHER ${CCACHE_PROGRAM})
        message(STATUS "Using ccache: ${CCACHE_PROGRAM}")
    else()
        message(FATAL_ERROR "ccache enabled but not found")
    endif()
endif()

# ============================================================================
# Find Required Tools
# ============================================================================

find_package(PkgConfig REQUIRED)
find_program(BISON_EXECUTABLE bison REQUIRED)
find_program(FLEX_EXECUTABLE flex REQUIRED)

# Find Rust toolchain
find_program(CARGO_EXECUTABLE cargo REQUIRED)
find_program(RUSTC_EXECUTABLE rustc REQUIRED)

# Read Rust toolchain channel from rust-toolchain.toml
if(EXISTS "${CMAKE_SOURCE_DIR}/rust-toolchain.toml")
    file(READ "${CMAKE_SOURCE_DIR}/rust-toolchain.toml" RUST_TOOLCHAIN_CONTENT)
    string(REGEX MATCH "channel *= *\"([^\"]+)\"" _ ${RUST_TOOLCHAIN_CONTENT})
    set(RUST_TOOLCHAIN_CHANNEL "${CMAKE_MATCH_1}")
    message(STATUS "Rust toolchain channel: ${RUST_TOOLCHAIN_CHANNEL}")
else()
    set(RUST_TOOLCHAIN_CHANNEL "stable")
endif()

# ============================================================================
# Find Dependencies
# ============================================================================

# pthread
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

# libsodium - try system first, fall back to internal
pkg_check_modules(LIBSODIUM libsodium>=1.0.17)
if(NOT LIBSODIUM_FOUND OR ENABLE_ASAN OR ENABLE_TSAN OR ENABLE_MSAN OR ENABLE_UBSAN)
    message(STATUS "Using internal libsodium")
    set(USE_INTERNAL_LIBSODIUM TRUE)
else()
    message(STATUS "Using system libsodium: ${LIBSODIUM_VERSION}")
    set(USE_INTERNAL_LIBSODIUM FALSE)
endif()

# PostgreSQL (optional)
if(USE_POSTGRES)
    pkg_check_modules(LIBPQ libpq)
    if(NOT LIBPQ_FOUND)
        message(WARNING "PostgreSQL not found, disabling")
        set(USE_POSTGRES OFF)
    else()
        message(STATUS "PostgreSQL enabled")
    endif()
endif()

# libunwind (optional)
if(USE_LIBUNWIND)
    if(PLATFORM_MACOS)
        # macOS has libunwind built-in
        set(HAVE_LIBUNWIND TRUE)
        message(STATUS "Using platform-native libunwind")
    else()
        pkg_check_modules(LIBUNWIND libunwind)
        if(LIBUNWIND_FOUND)
            set(HAVE_LIBUNWIND TRUE)
            message(STATUS "Using system libunwind")
        else()
            message(STATUS "libunwind not found, disabling")
            set(HAVE_LIBUNWIND FALSE)
        endif()
    endif()
else()
    set(HAVE_LIBUNWIND FALSE)
endif()

# Tracy dependencies (if GUI enabled)
if(USE_TRACY)
    if(ENABLE_ASAN)
        message(FATAL_ERROR "--enable-asan is not compatible with --enable-tracy")
    endif()
endif()

# ============================================================================
# Configure Header
# ============================================================================

# Create config.h
configure_file(
    "${CMAKE_SOURCE_DIR}/cmake/config.h.cmake.in"
    "${CMAKE_BINARY_DIR}/config.h"
    @ONLY
)

# ============================================================================
# Global Include Directories (minimal - only what's needed globally)
# ============================================================================

# These are kept minimal; stellar-core specific includes are in src/CMakeLists.txt
include_directories(
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_BINARY_DIR}
)

# Protocol-specific XDR headers
if(ENABLE_NEXT_PROTOCOL_VERSION)
    set(STELLAR_XDR_INCLUDE_DIR ${CMAKE_BINARY_DIR}/src/protocol-next)
else()
    set(STELLAR_XDR_INCLUDE_DIR ${CMAKE_BINARY_DIR}/src/protocol-curr)
endif()

# ============================================================================
# Global Compile Definitions
# ============================================================================

add_compile_definitions(
    FMT_HEADER_ONLY=1
    ASIO_SEPARATE_COMPILATION=1
    ASIO_STANDALONE
    CEREAL_THREAD_SAFE
)

if(USE_POSTGRES)
    add_compile_definitions(USE_POSTGRES=1)
endif()

if(BUILD_TESTS)
    add_compile_definitions(BUILD_TESTS=1)
endif()

if(USE_SPDLOG)
    add_compile_definitions(USE_SPDLOG)
endif()

if(USE_TRACY)
    add_compile_definitions(
        USE_TRACY
        TRACY_ENABLE
        TRACY_ON_DEMAND
        TRACY_NO_BROADCAST
        TRACY_ONLY_LOCALHOST
        TRACY_ONLY_IPV4
        TRACY_DELAYED_INIT
    )
endif()

if(ENABLE_NEXT_PROTOCOL_VERSION)
    add_compile_definitions(ENABLE_NEXT_PROTOCOL_VERSION_UNSAFE_FOR_PRODUCTION)
endif()

if(HAVE_LIBUNWIND)
    add_compile_definitions(HAVE_LIBUNWIND=1)
endif()

# ============================================================================
# Subdirectories
# ============================================================================

# Build third-party libraries
add_subdirectory(lib)

# Build xdrpp (xdrc compiler and library)
add_subdirectory(Builds/lib/xdrpp)

# Build libsodium if needed
if(USE_INTERNAL_LIBSODIUM)
    add_subdirectory(Builds/lib/libsodium)
endif()

# Build stellar-core
add_subdirectory(src)

# ============================================================================
# Summary
# ============================================================================

message(STATUS "")
message(STATUS "stellar-core build configuration:")
message(STATUS "  Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  C compiler: ${CMAKE_C_COMPILER}")
message(STATUS "  C++ compiler: ${CMAKE_CXX_COMPILER}")
message(STATUS "  PostgreSQL: ${USE_POSTGRES}")
message(STATUS "  Tests: ${BUILD_TESTS}")
message(STATUS "  spdlog: ${USE_SPDLOG}")
message(STATUS "  Tracy: ${USE_TRACY}")
message(STATUS "  libunwind: ${HAVE_LIBUNWIND}")
message(STATUS "  Next protocol: ${ENABLE_NEXT_PROTOCOL_VERSION}")
message(STATUS "  Internal libsodium: ${USE_INTERNAL_LIBSODIUM}")
message(STATUS "  Rust channel: ${RUST_TOOLCHAIN_CHANNEL}")
message(STATUS "")
