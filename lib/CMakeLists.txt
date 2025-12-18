# CMakeLists.txt for lib/ directory
# Build third-party static libraries

# Common include directories for libraries that need them
set(LIB_COMMON_INCLUDES
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/util
    ${CMAKE_CURRENT_SOURCE_DIR}/asio/asio/include
    ${CMAKE_CURRENT_SOURCE_DIR}/fmt/include
    ${CMAKE_CURRENT_SOURCE_DIR}/tracy/public/tracy
    ${CMAKE_CURRENT_SOURCE_DIR}/xdrpp  # For xdrpp/types.h
    ${CMAKE_CURRENT_BINARY_DIR}/xdrpp  # For xdrpp/build_endian.h (generated)
    ${CMAKE_CURRENT_SOURCE_DIR}/spdlog/include  # For spdlog/spdlog.h
)

# ============================================================================
# sqlite3 (always bundled)
# ============================================================================

set(SQLITE3_SOURCES
    sqlite/sqlite3.c
    sqlite/carray.c
)

add_library(stellar_sqlite3 STATIC ${SQLITE3_SOURCES})
target_include_directories(stellar_sqlite3 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/sqlite
)
target_compile_definitions(stellar_sqlite3 PRIVATE
    SQLITE_CORE
    SQLITE_OMIT_LOAD_EXTENSION=1
)
# Disable warnings for third-party code
target_compile_options(stellar_sqlite3 PRIVATE -w)

# ============================================================================
# SOCI (database abstraction)
# ============================================================================

file(GLOB SOCI_CORE_SOURCES
    soci/src/core/*.cpp
)

file(GLOB SOCI_SQLITE3_SOURCES
    soci/src/backends/sqlite3/*.cpp
)

set(SOCI_SOURCES ${SOCI_CORE_SOURCES} ${SOCI_SQLITE3_SOURCES})

if(USE_POSTGRES)
    file(GLOB SOCI_PG_SOURCES
        soci/src/backends/postgresql/*.cpp
    )
    list(APPEND SOCI_SOURCES ${SOCI_PG_SOURCES})
endif()

add_library(stellar_soci STATIC ${SOCI_SOURCES})
target_include_directories(stellar_soci PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/soci/src/core
    ${CMAKE_CURRENT_SOURCE_DIR}/soci/src/backends/sqlite3
)
target_include_directories(stellar_soci PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/sqlite
)
if(USE_POSTGRES)
    target_include_directories(stellar_soci PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/soci/src/backends/postgresql
    )
    target_include_directories(stellar_soci PRIVATE ${LIBPQ_INCLUDE_DIRS})
    target_link_directories(stellar_soci PRIVATE ${LIBPQ_LIBRARY_DIRS})
endif()
target_compile_options(stellar_soci PRIVATE -w)

# ============================================================================
# util library (http, httpthreaded, util)
# ============================================================================

file(GLOB UTIL_SOURCES
    util/*.cpp
    util/*.c
    http/*.cpp
    httpthreaded/*.cpp
)

add_library(stellar_util STATIC ${UTIL_SOURCES})
target_include_directories(stellar_util PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/util
    ${CMAKE_CURRENT_SOURCE_DIR}/http
    ${CMAKE_CURRENT_SOURCE_DIR}/httpthreaded
)
target_include_directories(stellar_util PRIVATE
    ${LIB_COMMON_INCLUDES}
    ${CMAKE_SOURCE_DIR}/src  # For util/asio.h
)
target_compile_options(stellar_util PRIVATE -w)

# ============================================================================
# ASIO (compiled as single translation unit)
# ============================================================================

add_library(stellar_asio STATIC asio.cpp)
target_include_directories(stellar_asio PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/asio/asio/include
)
target_include_directories(stellar_asio PRIVATE
    ${LIB_COMMON_INCLUDES}
    ${CMAKE_SOURCE_DIR}/src  # For util/asio.h
)
target_compile_definitions(stellar_asio PUBLIC
    ASIO_SEPARATE_COMPILATION=1
    ASIO_STANDALONE
)
target_compile_options(stellar_asio PRIVATE -w)
target_link_libraries(stellar_asio PUBLIC Threads::Threads)

# ============================================================================
# JSON library
# ============================================================================

file(GLOB JSON_SOURCES
    json/*.cpp
)

add_library(stellar_json STATIC ${JSON_SOURCES})
target_include_directories(stellar_json PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/json
)
target_compile_options(stellar_json PRIVATE -w)

# ============================================================================
# spdlog (compiled as single translation unit)
# ============================================================================

if(USE_SPDLOG)
    add_library(stellar_spdlog STATIC spdlog.cpp)
    target_include_directories(stellar_spdlog PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/spdlog/include
    )
    target_include_directories(stellar_spdlog PRIVATE ${LIB_COMMON_INCLUDES})
    target_compile_options(stellar_spdlog PRIVATE -w)
endif()

# ============================================================================
# libmedida (metrics library, if present)
# ============================================================================

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/libmedida/src/medida/medida.h")
    file(GLOB_RECURSE MEDIDA_SOURCES
        libmedida/src/medida/*.cc
        libmedida/src/medida/*.cpp
    )
    
    add_library(stellar_medida STATIC ${MEDIDA_SOURCES})
    target_include_directories(stellar_medida PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/libmedida/src
    )
    target_include_directories(stellar_medida PRIVATE ${LIB_COMMON_INCLUDES})
    target_compile_options(stellar_medida PRIVATE -w)
    set(HAVE_INTERNAL_MEDIDA TRUE PARENT_SCOPE)
else()
    set(HAVE_INTERNAL_MEDIDA FALSE PARENT_SCOPE)
endif()

# ============================================================================
# Combined 3rdparty library (convenience target)
# ============================================================================

add_library(stellar_3rdparty INTERFACE)
target_link_libraries(stellar_3rdparty INTERFACE
    stellar_sqlite3
    stellar_soci
    stellar_util
    stellar_asio
    stellar_json
)
if(USE_SPDLOG)
    target_link_libraries(stellar_3rdparty INTERFACE stellar_spdlog)
endif()
