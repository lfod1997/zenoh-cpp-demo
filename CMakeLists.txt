cmake_minimum_required(VERSION 3.25)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set install path to somewhere non-system
set(ideal_install_prefix "${CMAKE_BINARY_DIR}/installed")
if (NOT CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT AND CMAKE_INSTALL_PREFIX AND NOT CMAKE_INSTALL_PREFIX STREQUAL ideal_install_prefix)
    message(WARNING "Overwriting non-default `CMAKE_INSTALL_PREFIX` (${CMAKE_INSTALL_PREFIX})!")
endif ()
set(CMAKE_INSTALL_PREFIX ${ideal_install_prefix} CACHE PATH "Fixed install path." FORCE)
message(STATUS "CMAKE_INSTALL_PREFIX = ${CMAKE_INSTALL_PREFIX}")

# Ensure Rust tools are installed
find_program(CARGO cargo REQUIRED)

# Dependencies
include(FetchContent)
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

# Fetch all dependencies
FetchContent_MakeAvailable(ZenohC ZenohCpp)

# Local install (adapted from zenohc's CMakeLists.txt)
if (BUILD_SHARED_LIBS)
    set(cargo_binary_dir_debug "${zenohc_BINARY_DIR}/debug/target/debug")
    set(cargo_binary_dir_release "${zenohc_BINARY_DIR}/release/target/release")
    if (WIN32)
        set(dylib_file_debug "zenohc.dll")
        set(dylib_file_release "zenohc.dll")
    elseif (APPLE)
        set(dylib_file_debug "libzenohc.dylib")
        set(dylib_file_release "libzenohc.dylib")
    else () # UNIX
        set(dylib_file_debug "libzenohc.so")
        set(dylib_file_release "libzenohc.so")
    endif ()
    set(dylib_full_path "$<IF:$<CONFIG:Debug>,${cargo_binary_dir_debug}/${dylib_file_debug},${cargo_binary_dir_release}/${dylib_file_release}>")
    install(FILES ${dylib_full_path} DESTINATION "${CMAKE_INSTALL_PREFIX}/bin")
endif ()

project(ZenohCppDemo)

add_executable(chat chat.cpp)
target_link_libraries(chat zenohc::lib zenohcxx::zenohc)

target_compile_definitions(chat PRIVATE $<$<CONFIG:Debug>:DEBUG>)

if (BUILD_SHARED_LIBS)
    add_custom_command(
        TARGET chat
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy ${dylib_full_path} ${CMAKE_BINARY_DIR}
        COMMENT "Copying shared library"
    )
endif ()
