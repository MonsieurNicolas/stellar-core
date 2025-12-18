# CMakeLists.txt for src/ directory
# Build stellar-core binary

# ============================================================================
# Collect Source Files
# ============================================================================

# Main source files (excluding test directories)
file(GLOB_RECURSE SRC_CXX_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/bucket/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/catchup/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/crypto/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/database/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/herder/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/history/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/historywork/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/invariant/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ledger/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/main/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/overlay/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/process/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/scp/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/transactions/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/util/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/work/*.cpp
)

# Exclude test files from main sources
list(FILTER SRC_CXX_FILES EXCLUDE REGEX ".*/test/.*")
list(FILTER SRC_CXX_FILES EXCLUDE REGEX ".*/simulation/.*")

# Test source files (only if BUILD_TESTS is ON)
if(BUILD_TESTS)
    file(GLOB_RECURSE SRC_TEST_CXX_FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/*/test/*.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/test/*.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/simulation/*.cpp
    )
endif()

# ============================================================================
# XDR Compilation
# ============================================================================

# Determine which protocol directory to use
if(ENABLE_NEXT_PROTOCOL_VERSION)
    set(XDR_PROTOCOL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/protocol-next/xdr)
    set(XDR_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/protocol-next)
else()
    set(XDR_PROTOCOL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/protocol-curr/xdr)
    set(XDR_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/protocol-curr)
endif()

# Find all XDR files
file(GLOB XDR_FILES "${XDR_PROTOCOL_DIR}/*.x")

# Generate XDR headers
set(XDR_HEADERS "")
foreach(XDR_FILE ${XDR_FILES})
    get_filename_component(XDR_NAME ${XDR_FILE} NAME_WE)
    set(XDR_HEADER "${XDR_OUTPUT_DIR}/xdr/${XDR_NAME}.h")
    list(APPEND XDR_HEADERS ${XDR_HEADER})
    
    add_custom_command(
        OUTPUT ${XDR_HEADER}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${XDR_OUTPUT_DIR}/xdr
        COMMAND $<TARGET_FILE:xdrc_exe> -hh -pedantic -o ${XDR_HEADER} ${XDR_FILE}
        DEPENDS xdrc_exe ${XDR_FILE}
        COMMENT "Compiling XDR: ${XDR_NAME}.x"
    )
endforeach()

add_custom_target(xdr_headers DEPENDS ${XDR_HEADERS})

# ============================================================================
# Parser Generation (flex/bison for XDRQuery)
# ============================================================================

set(XDRQUERY_DIR ${CMAKE_CURRENT_SOURCE_DIR}/util/xdrquery)
set(XDRQUERY_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/util/xdrquery)

# Generate scanner with flex
add_custom_command(
    OUTPUT ${XDRQUERY_OUTPUT_DIR}/XDRQueryScanner.cpp
    COMMAND ${CMAKE_COMMAND} -E make_directory ${XDRQUERY_OUTPUT_DIR}
    COMMAND ${FLEX_EXECUTABLE} --outfile=${XDRQUERY_OUTPUT_DIR}/XDRQueryScanner.cpp ${XDRQUERY_DIR}/XDRQueryScanner.ll
    DEPENDS ${XDRQUERY_DIR}/XDRQueryScanner.ll
    COMMENT "Generating XDRQuery scanner with flex"
)

# Generate parser with bison
add_custom_command(
    OUTPUT ${XDRQUERY_OUTPUT_DIR}/XDRQueryParser.cpp ${XDRQUERY_OUTPUT_DIR}/XDRQueryParser.h
    COMMAND ${CMAKE_COMMAND} -E make_directory ${XDRQUERY_OUTPUT_DIR}
    COMMAND ${BISON_EXECUTABLE} --defines=${XDRQUERY_OUTPUT_DIR}/XDRQueryParser.h --output=${XDRQUERY_OUTPUT_DIR}/XDRQueryParser.cpp ${XDRQUERY_DIR}/XDRQueryParser.yy
    DEPENDS ${XDRQUERY_DIR}/XDRQueryParser.yy
    COMMENT "Generating XDRQuery parser with bison"
)

set(GENERATED_PARSER_FILES
    ${XDRQUERY_OUTPUT_DIR}/XDRQueryScanner.cpp
    ${XDRQUERY_OUTPUT_DIR}/XDRQueryParser.cpp
    ${XDRQUERY_OUTPUT_DIR}/XDRQueryParser.h
)

# ============================================================================
# Version Information
# ============================================================================

# Generate StellarCoreVersion.cpp from git
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/main/StellarCoreVersion.cpp
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/main
    COMMAND ${CMAKE_COMMAND} 
        -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
        -DTEMPLATE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/main/StellarCoreVersion.cpp.in
        -DOUTPUT_FILE=${CMAKE_CURRENT_BINARY_DIR}/main/StellarCoreVersion.cpp
        -DPROJECT_VERSION=${PROJECT_VERSION}
        -P ${CMAKE_SOURCE_DIR}/cmake/GenerateVersion.cmake
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/main/StellarCoreVersion.cpp.in
    COMMENT "Generating StellarCoreVersion.cpp"
)

# Generate XDRFilesSha256.cpp
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/main/XDRFilesSha256.cpp
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/main
    COMMAND ${CMAKE_SOURCE_DIR}/hash-xdrs.sh ${XDR_PROTOCOL_DIR}/.. > ${CMAKE_CURRENT_BINARY_DIR}/main/XDRFilesSha256.cpp
    DEPENDS ${XDR_FILES} ${CMAKE_SOURCE_DIR}/hash-xdrs.sh
    COMMENT "Generating XDRFilesSha256.cpp"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# ============================================================================
# Rust Integration
# ============================================================================

# Define Rust build directories
set(RUST_BUILD_DIR ${CMAKE_CURRENT_BINARY_DIR}/rust)
set(RUST_TARGET_DIR ${CMAKE_BINARY_DIR}/target)
set(RUST_BIN_DIR ${RUST_BUILD_DIR}/bin)

# Determine Rust profile
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(RUST_PROFILE "dev")
    set(RUST_PROFILE_ARG "")
    set(RUST_PROFILE_DIR "debug")
else()
    set(RUST_PROFILE "release")
    set(RUST_PROFILE_ARG "--release")
    set(RUST_PROFILE_DIR "release")
endif()

# Soroban protocol versions
set(ALL_SOROBAN_PROTOCOLS p21 p22 p23 p24 p25)

# Build cxxbridge tool
set(RUST_CXXBRIDGE ${RUST_BIN_DIR}/cxxbridge)
add_custom_command(
    OUTPUT ${RUST_CXXBRIDGE}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${RUST_BIN_DIR}
    COMMAND ${CMAKE_COMMAND} -E env CARGO_HTTP_MULTIPLEXING=false
        ${CARGO_EXECUTABLE} +${RUST_TOOLCHAIN_CHANNEL} install --force --locked --root ${RUST_BUILD_DIR} cxxbridge-cmd --version 1.0.68
    COMMENT "Installing cxxbridge tool"
)

# Generate Rust bridge files
set(RUST_BRIDGE_H ${CMAKE_CURRENT_SOURCE_DIR}/rust/RustBridge.h)
set(RUST_BRIDGE_CPP ${CMAKE_CURRENT_SOURCE_DIR}/rust/RustBridge.cpp)
set(RUST_BRIDGE_RS ${CMAKE_CURRENT_SOURCE_DIR}/rust/src/bridge.rs)

add_custom_command(
    OUTPUT ${RUST_BRIDGE_H}
    COMMAND ${RUST_CXXBRIDGE} ${RUST_BRIDGE_RS} --cfg test=false --header --output ${RUST_BRIDGE_H}.tmp
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${RUST_BRIDGE_H}.tmp ${RUST_BRIDGE_H}
    COMMAND ${CMAKE_COMMAND} -E remove ${RUST_BRIDGE_H}.tmp
    DEPENDS ${RUST_CXXBRIDGE} ${RUST_BRIDGE_RS}
    COMMENT "Generating RustBridge.h"
)

add_custom_command(
    OUTPUT ${RUST_BRIDGE_CPP}
    COMMAND ${RUST_CXXBRIDGE} ${RUST_BRIDGE_RS} --cfg test=false --output ${RUST_BRIDGE_CPP}.tmp
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${RUST_BRIDGE_CPP}.tmp ${RUST_BRIDGE_CPP}
    COMMAND ${CMAKE_COMMAND} -E remove ${RUST_BRIDGE_CPP}.tmp
    DEPENDS ${RUST_CXXBRIDGE} ${RUST_BRIDGE_RS}
    COMMENT "Generating RustBridge.cpp"
)

# Build Rust static library
if(UNIFIED_RUST)
    # Unified build for sanitizers
    execute_process(
        COMMAND ${RUSTC_EXECUTABLE} -vV
        OUTPUT_VARIABLE RUSTC_VERSION_OUTPUT
    )
    string(REGEX MATCH "host: ([^\n]+)" _ ${RUSTC_VERSION_OUTPUT})
    set(RUST_TARGET ${CMAKE_MATCH_1})
    
    set(LIBRUST_STELLAR_CORE ${RUST_TARGET_DIR}/${RUST_TARGET}/${RUST_PROFILE_DIR}/librust_stellar_core.a)
    
    # Build cargo features
    set(CARGO_FEATURES "--features unified")
    if(BUILD_TESTS)
        set(CARGO_FEATURES "${CARGO_FEATURES} --features testutils")
    endif()
    if(USE_TRACY)
        set(CARGO_FEATURES "${CARGO_FEATURES} --features tracy")
    endif()
    if(ENABLE_NEXT_PROTOCOL_VERSION)
        set(CARGO_FEATURES "${CARGO_FEATURES} --features next")
    endif()
    
    add_custom_command(
        OUTPUT ${LIBRUST_STELLAR_CORE}
        COMMAND ${CMAKE_COMMAND} -E env
            CC=${CMAKE_C_COMPILER}
            CXX=${CMAKE_CXX_COMPILER}
            CFLAGS=${CMAKE_C_FLAGS}
            CXXFLAGS=${CMAKE_CXX_FLAGS}
            CARGO_NET_GIT_FETCH_WITH_CLI=true
            ${CARGO_EXECUTABLE} +${RUST_TOOLCHAIN_CHANNEL} rustc
            --package stellar-core
            --target ${RUST_TARGET}
            ${CARGO_FEATURES}
            ${RUST_PROFILE_ARG}
            --locked
            --target-dir ${RUST_TARGET_DIR}
        COMMAND ranlib ${LIBRUST_STELLAR_CORE}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/src/rust
        DEPENDS ${RUST_BRIDGE_H} ${RUST_BRIDGE_CPP}
        COMMENT "Building Rust library (unified)"
    )
else()
    # Non-unified build (default, production)
    set(SOROBAN_BUILD_DIR ${RUST_BUILD_DIR}/soroban)
    set(LIBRUST_STELLAR_CORE ${RUST_TARGET_DIR}/${RUST_PROFILE_DIR}/librust_stellar_core.a)
    
    # Build each soroban protocol version separately
    set(SOROBAN_LIBS_STAMP ${RUST_BUILD_DIR}/soroban-libs.stamp)
    
    # Determine max protocol for feature flags
    list(GET ALL_SOROBAN_PROTOCOLS -1 SOROBAN_MAX_PROTOCOL)
    
    add_custom_command(
        OUTPUT ${SOROBAN_LIBS_STAMP}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${SOROBAN_BUILD_DIR}
        COMMAND ${CMAKE_COMMAND}
            -DCARGO_EXECUTABLE=${CARGO_EXECUTABLE}
            -DRUST_TOOLCHAIN_CHANNEL=${RUST_TOOLCHAIN_CHANNEL}
            -DSOROBAN_BUILD_DIR=${SOROBAN_BUILD_DIR}
            -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
            -DALL_SOROBAN_PROTOCOLS="${ALL_SOROBAN_PROTOCOLS}"
            -DSOROBAN_MAX_PROTOCOL=${SOROBAN_MAX_PROTOCOL}
            -DRUST_PROFILE_ARG=${RUST_PROFILE_ARG}
            -DUSE_TRACY=${USE_TRACY}
            -DENABLE_NEXT_PROTOCOL_VERSION=${ENABLE_NEXT_PROTOCOL_VERSION}
            -DCC=${CMAKE_C_COMPILER}
            -DCXX=${CMAKE_CXX_COMPILER}
            -DCFLAGS=${CMAKE_C_FLAGS}
            -DCXXFLAGS=${CMAKE_CXX_FLAGS}
            -P ${CMAKE_SOURCE_DIR}/cmake/BuildSorobanLibs.cmake
        COMMAND ${CMAKE_COMMAND} -E touch ${SOROBAN_LIBS_STAMP}
        DEPENDS ${RUST_BRIDGE_H} ${RUST_BRIDGE_CPP}
        COMMENT "Building soroban libraries"
    )
    
    # Build stellar-core rust crate with extern links to sorobans
    add_custom_command(
        OUTPUT ${LIBRUST_STELLAR_CORE}
        COMMAND ${CMAKE_COMMAND}
            -DCARGO_EXECUTABLE=${CARGO_EXECUTABLE}
            -DRUST_TOOLCHAIN_CHANNEL=${RUST_TOOLCHAIN_CHANNEL}
            -DRUST_TARGET_DIR=${RUST_TARGET_DIR}
            -DSOROBAN_BUILD_DIR=${SOROBAN_BUILD_DIR}
            -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
            -DALL_SOROBAN_PROTOCOLS="${ALL_SOROBAN_PROTOCOLS}"
            -DRUST_PROFILE_DIR=${RUST_PROFILE_DIR}
            -DRUST_PROFILE_ARG=${RUST_PROFILE_ARG}
            -DBUILD_TESTS=${BUILD_TESTS}
            -DUSE_TRACY=${USE_TRACY}
            -DENABLE_NEXT_PROTOCOL_VERSION=${ENABLE_NEXT_PROTOCOL_VERSION}
            -DCC=${CMAKE_C_COMPILER}
            -DCXX=${CMAKE_CXX_COMPILER}
            -DCFLAGS=${CMAKE_C_FLAGS}
            -DCXXFLAGS=${CMAKE_CXX_FLAGS}
            -P ${CMAKE_SOURCE_DIR}/cmake/BuildRustLib.cmake
        COMMAND ranlib ${LIBRUST_STELLAR_CORE}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/src/rust
        DEPENDS ${SOROBAN_LIBS_STAMP}
        COMMENT "Building Rust library (non-unified)"
    )
endif()

add_custom_target(rust_lib DEPENDS ${LIBRUST_STELLAR_CORE})

# ============================================================================
# stellar-core Executable
# ============================================================================

# Combine all source files
set(STELLAR_CORE_SOURCES
    ${SRC_CXX_FILES}
    ${GENERATED_PARSER_FILES}
    ${CMAKE_CURRENT_BINARY_DIR}/main/StellarCoreVersion.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/main/XDRFilesSha256.cpp
    ${RUST_BRIDGE_CPP}
)

if(BUILD_TESTS)
    list(APPEND STELLAR_CORE_SOURCES ${SRC_TEST_CXX_FILES})
endif()

add_executable(stellar-core ${STELLAR_CORE_SOURCES})

# Add dependencies on generated files
add_dependencies(stellar-core xdr_headers rust_lib)

# Include directories
target_include_directories(stellar-core PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
    ${XDR_OUTPUT_DIR}
    ${XDRQUERY_OUTPUT_DIR}/..
    ${STELLAR_XDR_INCLUDE_DIR}
)

# Third-party library includes (as system to suppress warnings)
target_include_directories(stellar-core SYSTEM PRIVATE
    ${CMAKE_SOURCE_DIR}/lib
    ${CMAKE_SOURCE_DIR}/lib/autocheck/include
    ${CMAKE_SOURCE_DIR}/lib/cereal/include
    ${CMAKE_SOURCE_DIR}/lib/util
    ${CMAKE_SOURCE_DIR}/lib/fmt/include
    ${CMAKE_SOURCE_DIR}/lib/soci/src/core
    ${CMAKE_SOURCE_DIR}/lib/tracy/public/tracy
    ${CMAKE_SOURCE_DIR}/lib/spdlog/include
    ${CMAKE_SOURCE_DIR}/lib/asio/asio/include
    ${CMAKE_SOURCE_DIR}/src/rust/src
)

# Link libraries
target_link_libraries(stellar-core PRIVATE
    stellar_3rdparty
    xdrpp
    Threads::Threads
    ${LIBRUST_STELLAR_CORE}
    ${CMAKE_DL_LIBS}
)

# Link libsodium
if(USE_INTERNAL_LIBSODIUM)
    target_link_libraries(stellar-core PRIVATE sodium)
    target_include_directories(stellar-core PRIVATE ${LIBSODIUM_INCLUDE_DIR})
else()
    target_include_directories(stellar-core PRIVATE ${LIBSODIUM_INCLUDE_DIRS})
    target_link_directories(stellar-core PRIVATE ${LIBSODIUM_LIBRARY_DIRS})
    target_link_libraries(stellar-core PRIVATE ${LIBSODIUM_LIBRARIES})
endif()

# Link PostgreSQL if enabled
if(USE_POSTGRES)
    target_include_directories(stellar-core PRIVATE ${LIBPQ_INCLUDE_DIRS})
    target_link_directories(stellar-core PRIVATE ${LIBPQ_LIBRARY_DIRS})
    target_link_libraries(stellar-core PRIVATE ${LIBPQ_LIBRARIES})
endif()

# Link libunwind if available
if(HAVE_LIBUNWIND AND NOT PLATFORM_MACOS)
    target_include_directories(stellar-core PRIVATE ${LIBUNWIND_INCLUDE_DIRS})
    target_link_directories(stellar-core PRIVATE ${LIBUNWIND_LIBRARY_DIRS})
    target_link_libraries(stellar-core PRIVATE ${LIBUNWIND_LIBRARIES})
endif()

# Link libmedida if internal
if(HAVE_INTERNAL_MEDIDA)
    target_link_libraries(stellar-core PRIVATE stellar_medida)
endif()

# Compiler warnings
target_compile_options(stellar-core PRIVATE ${STELLAR_WARNING_FLAGS})

# ============================================================================
# Test Data Files
# ============================================================================

if(BUILD_TESTS)
    set(TESTDATA_DIR ${CMAKE_CURRENT_BINARY_DIR}/testdata)
    file(MAKE_DIRECTORY ${TESTDATA_DIR})
    
    # Copy test configuration files
    set(TEST_CFG_FILES
        stellar-core_example.cfg
        stellar-core_standalone.cfg
        stellar-core_testnet.cfg
    )
    
    foreach(CFG_FILE ${TEST_CFG_FILES})
        if(EXISTS ${CMAKE_SOURCE_DIR}/docs/${CFG_FILE})
            configure_file(
                ${CMAKE_SOURCE_DIR}/docs/${CFG_FILE}
                ${TESTDATA_DIR}/${CFG_FILE}
                COPYONLY
            )
        endif()
    endforeach()
endif()

# ============================================================================
# Installation
# ============================================================================

install(TARGETS stellar-core
    RUNTIME DESTINATION bin
)
