# CMakeLists.txt for lib/libsodium
# Build libsodium cryptographic library when system version is not suitable

# Source directory for libsodium (separate from CMake file location)
set(LIBSODIUM_SOURCE_DIR ${CMAKE_SOURCE_DIR}/lib/libsodium)

# Generate version.h from the template in sodium/ subdirectory
set(SODIUM_VERSION_STRING "1.0.18")
set(SODIUM_LIBRARY_VERSION_MAJOR 10)
set(SODIUM_LIBRARY_VERSION_MINOR 3)

# Create sodium subdirectory in build dir
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/sodium)

file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/sodium/version.h
"#ifndef sodium_version_H
#define sodium_version_H

#include \"export.h\"

#define SODIUM_VERSION_STRING \"${SODIUM_VERSION_STRING}\"

#define SODIUM_LIBRARY_VERSION_MAJOR ${SODIUM_LIBRARY_VERSION_MAJOR}
#define SODIUM_LIBRARY_VERSION_MINOR ${SODIUM_LIBRARY_VERSION_MINOR}

#ifdef __cplusplus
extern \"C\" {
#endif

SODIUM_EXPORT
const char *sodium_version_string(void);

SODIUM_EXPORT
int         sodium_library_version_major(void);

SODIUM_EXPORT
int         sodium_library_version_minor(void);

SODIUM_EXPORT
int         sodium_library_minimal(void);

#ifdef __cplusplus
}
#endif

#endif
")

# Gather all source files
file(GLOB_RECURSE LIBSODIUM_SOURCES
    ${LIBSODIUM_SOURCE_DIR}/src/libsodium/*.c
)

# Exclude asm files that might cause issues on some platforms
list(FILTER LIBSODIUM_SOURCES EXCLUDE REGEX ".*aesni.*")
list(FILTER LIBSODIUM_SOURCES EXCLUDE REGEX ".*sse.*")
list(FILTER LIBSODIUM_SOURCES EXCLUDE REGEX ".*avx.*")

# Check for architecture-specific optimizations
include(CheckCSourceCompiles)

# Detect if we're on x86_64 with SSE
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
    set(HAVE_AMD64_ASM 1)
endif()

add_library(sodium STATIC ${LIBSODIUM_SOURCES})

target_include_directories(sodium PUBLIC
    ${LIBSODIUM_SOURCE_DIR}/src/libsodium/include
    ${LIBSODIUM_SOURCE_DIR}/src/libsodium/include/sodium
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}/sodium
)

target_include_directories(sodium PRIVATE
    ${LIBSODIUM_SOURCE_DIR}/src/libsodium
    ${LIBSODIUM_SOURCE_DIR}/src/libsodium/include/sodium
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}/sodium
)

# Platform-specific defines
target_compile_definitions(sodium PRIVATE
    SODIUM_STATIC
    CONFIGURED=1
    DEV_MODE=0
)

# Use portable/ref implementations for maximum compatibility
target_compile_definitions(sodium PRIVATE
    HAVE_WEAK_SYMBOLS
    NATIVE_LITTLE_ENDIAN
)

# Suppress warnings for third-party code
target_compile_options(sodium PRIVATE -w)

# Export the library for use by parent
set(LIBSODIUM_LIBRARY sodium PARENT_SCOPE)
set(LIBSODIUM_INCLUDE_DIR ${LIBSODIUM_SOURCE_DIR}/src/libsodium/include PARENT_SCOPE)
