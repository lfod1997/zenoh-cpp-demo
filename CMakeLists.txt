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
find_program(JAVA java REQUIRED)
execute_process(COMMAND ${JAVA} --version)
find_file(FASTDDSGEN_JAR
    NAMES "fastddsgen.jar"
    PATHS "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/fastddsgen"
    REQUIRED
    DOC "Path to the Fast DDS-Gen Java binary (fastddsgen.jar)."
)
execute_process(COMMAND ${JAVA} -jar ${FASTDDSGEN_JAR} -version)

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

# Repo: https://github.com/eProsima/Fast-CDR.git
# Versioning (only relevant if you're interfacing ROS2):
# 1. Go to https://github.com/ros2/ros2/blob/TAG/ros2.repos where TAG is the git tag from which your ROS2 is built
# 2. Look for `eProsima/Fast-CDR` in the opened file to the version to be used here:
find_package(fastcdr REQUIRED CONFIG)

# Declare a custom target that compiles all schemas
set(schema_inc_dir "${CMAKE_CURRENT_SOURCE_DIR}/schema")
set(schema_gen_dir "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(GLOB schema_src "${CMAKE_CURRENT_SOURCE_DIR}/schema/*.idl")
set(schema_gen_suffixes ".hpp" "CdrAux.hpp" "CdrAux.ipp")
set(schema_gen "")
foreach (suffix IN LISTS schema_gen_suffixes)
    foreach (idl_path IN LISTS schema_src)
        string(REPLACE ${CMAKE_CURRENT_SOURCE_DIR} ${schema_gen_dir} _tmp_str ${idl_path})
        string(REGEX REPLACE "\\.idl$" ${suffix} _tmp_str ${_tmp_str})
        list(APPEND schema_gen ${_tmp_str})
    endforeach ()
endforeach ()
add_custom_command(
    DEPENDS ${schema_src}
    OUTPUT ${schema_gen}
    COMMAND ${JAVA} -jar ${FASTDDSGEN_JAR} -replace -language c++ -typeros2 -I ${schema_inc_dir} -d ${schema_gen_dir} ${schema_src}
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" # fastddsgen preserves relative directories
    COMMENT "Compiling schemas"
    VERBATIM
)
add_custom_target(compile_schemas DEPENDS ${schema_gen})

# Wrap generated headers to an interface library
add_library(schemas INTERFACE)
add_dependencies(schemas compile_schemas)
target_include_directories(schemas INTERFACE ${schema_gen_dir})
target_link_libraries(schemas INTERFACE fastcdr)

# Main executable
add_executable(chat
    src/chat.cpp
    src/ansi_sequences.h
    src/utf8_builder.hpp
)
target_link_libraries(chat schemas zenohc::lib zenohcxx::zenohc mio::mio)

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
