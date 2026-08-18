cmake_minimum_required(VERSION 3.25)

# =====================
# Compiler Preparation
# =====================
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if (WIN32)
    if (CMAKE_GENERATOR MATCHES "Ninja")
        # Can't find out if Ninja is using MSVC, just set flags for both and bear with the warnings
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /EHsc /utf-8 -finput-charset=UTF-8 -fexec-charset=UTF-8")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /EHsc /utf-8 -finput-charset=UTF-8 -fexec-charset=UTF-8")
    elseif (MSVC)
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /EHsc /utf-8")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /EHsc /utf-8")
    else ()
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -finput-charset=UTF-8 -fexec-charset=UTF-8")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -finput-charset=UTF-8 -fexec-charset=UTF-8")
    endif ()
endif ()

# =======================================
# Tool Discovery & Dependency Resolution
# =======================================
# Ensure tools
find_program(CARGO cargo REQUIRED)
execute_process(COMMAND ${CARGO} --version)

# Set install path to somewhere non-system
set(ideal_install_prefix "${CMAKE_BINARY_DIR}/installed")
if (NOT CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT AND CMAKE_INSTALL_PREFIX AND NOT CMAKE_INSTALL_PREFIX STREQUAL ideal_install_prefix)
    message(WARNING "Overwriting non-default `CMAKE_INSTALL_PREFIX` (${CMAKE_INSTALL_PREFIX})!")
endif ()
set(CMAKE_INSTALL_PREFIX ${ideal_install_prefix} CACHE PATH "Fixed install path." FORCE)
message(STATUS "CMAKE_INSTALL_PREFIX = ${CMAKE_INSTALL_PREFIX}")

# Version-controlled Dependencies
include(FetchContent)
set(FETCHCONTENT_UPDATES_DISCONNECTED TRUE CACHE INTERNAL "" FORCE)
FetchContent_Declare(
    ZenohC
    GIT_REPOSITORY https://github.com/eclipse-zenoh/zenoh-c.git
    GIT_TAG "1.9.0"
    GIT_SHALLOW TRUE
)
FetchContent_Declare(
    ZenohCpp
    GIT_REPOSITORY https://github.com/eclipse-zenoh/zenoh-cpp.git
    GIT_TAG "1.9.0"
    GIT_SHALLOW TRUE
    GIT_SUBMODULES "" # The repo contains zenohc as a submodule (unused in cmake)
)
FetchContent_Declare(
    Mio
    GIT_REPOSITORY https://github.com/vimpunk/mio.git
    GIT_TAG "8b6b7d878c89e81614d05edca7936de41ccdd2da"
    GIT_SHALLOW TRUE
)

# Fetch all dependencies
message("Fetching Zenoh")
FetchContent_MakeAvailable(ZenohC ZenohCpp)
message("Fetching Mio")
FetchContent_MakeAvailable(Mio)

# ===================
# Project Definition
# ===================
project(ZenohCppDemo)

# Repo: https://github.com/eclipse-cyclonedds/cyclonedds.git
# Versioning: 11.0.1 (e54e991f)
find_package(CycloneDDS REQUIRED)
# Repo: https://github.com/eclipse-cyclonedds/cyclonedds-cxx.git
# Versioning: 11.0.1 (20ccaa51)
find_package(CycloneDDS-CXX REQUIRED)

# Declare a custom target that compiles all schemas
set(schema_inc_dir "${CMAKE_CURRENT_SOURCE_DIR}/schema")
set(schema_gen_dir "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(GLOB schema_src "${CMAKE_CURRENT_SOURCE_DIR}/schema/*.idl")
set(schema_gen_suffixes ".hpp" ".cpp")
set(schema_gen "")
foreach (suffix IN LISTS schema_gen_suffixes)
    foreach (idl_path IN LISTS schema_src)
        string(REPLACE ${CMAKE_CURRENT_SOURCE_DIR} ${schema_gen_dir} _tmp_str ${idl_path})
        string(REGEX REPLACE "\\.idl$" ${suffix} _tmp_str ${_tmp_str})
        list(APPEND schema_gen ${_tmp_str})
    endforeach ()
endforeach ()
idlcxx_generate(
    TARGET schemas
    FILES ${schema_src}
    INCLUDES ${schema_inc_dir}
    BASE_DIR ${CMAKE_CURRENT_SOURCE_DIR}
    OUTPUT_DIR ${schema_gen_dir}
    WARNINGS no-implicit-extensibility
)
add_dependencies(schemas schema_src)

# Main executable
add_executable(chat
    src/chat.cpp
    src/ansi_sequences.h
    src/utf8_builder.hpp
)
target_link_libraries(chat schemas zenohc::lib zenohcxx::zenohc mio::mio CycloneDDS-CXX::ddscxx)

# Define the macro "DEBUG" if building with debug profile
target_compile_definitions(chat PRIVATE $<$<CONFIG:Debug>:DEBUG>)

# ============================
# Post Build and Installation
# ============================
set(_target chat)
if (BUILD_SHARED_LIBS)
    # Copy shared libraries after building
    add_custom_command(
        TARGET ${_target}
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_RUNTIME_DLLS:${_target}>" "$<TARGET_FILE_DIR:${_target}>"
        COMMAND_EXPAND_LISTS
        COMMENT "Copying shared library"
    )
    # Install shared libraries
    install(FILES "$<TARGET_RUNTIME_DLLS:${_target}>" DESTINATION "${CMAKE_INSTALL_PREFIX}/bin")
endif ()

# Install executable
install(FILES "$<TARGET_FILE:${_target}>" DESTINATION "${CMAKE_INSTALL_PREFIX}/bin")
