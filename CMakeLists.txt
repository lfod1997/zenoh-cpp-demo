cmake_minimum_required(VERSION 3.25)

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

# Ensure tools
find_program(CARGO cargo REQUIRED)
execute_process(COMMAND ${CARGO} --version)
find_program(FLATC flatc REQUIRED)
execute_process(COMMAND ${FLATC} --version)

# Set install path to somewhere non-system
set(ideal_install_prefix "${CMAKE_BINARY_DIR}/installed")
if (NOT CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT AND CMAKE_INSTALL_PREFIX AND NOT CMAKE_INSTALL_PREFIX STREQUAL ideal_install_prefix)
    message(WARNING "Overwriting non-default `CMAKE_INSTALL_PREFIX` (${CMAKE_INSTALL_PREFIX})!")
endif ()
set(CMAKE_INSTALL_PREFIX ${ideal_install_prefix} CACHE PATH "Fixed install path." FORCE)
message(STATUS "CMAKE_INSTALL_PREFIX = ${CMAKE_INSTALL_PREFIX}")

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
FetchContent_Declare(
    FlatBuffers
    GIT_REPOSITORY https://github.com/google/flatbuffers.git
    GIT_TAG "v25.12.19-2026-02-06-03fffb2"
    GIT_SHALLOW TRUE
)
FetchContent_Declare(
    Mio
    GIT_REPOSITORY https://github.com/vimpunk/mio.git
    GIT_TAG "8b6b7d878c89e81614d05edca7936de41ccdd2da"
    GIT_SHALLOW TRUE
)

# Fetch all dependencies
FetchContent_MakeAvailable(ZenohC ZenohCpp)
set(FLATBUFFERS_BUILD_TESTS FALSE CACHE INTERNAL "" FORCE)
set(FLATBUFFERS_BUILD_FLATC FALSE CACHE INTERNAL "" FORCE)
FetchContent_MakeAvailable(FlatBuffers)
FetchContent_MakeAvailable(Mio)

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

# Declare a custom target that compiles all schemas
set(schema_src_dir "${CMAKE_CURRENT_SOURCE_DIR}/schema")
set(schema_gen_dir "${CMAKE_CURRENT_BINARY_DIR}/generated/schema")
file(GLOB schema_src "${schema_src_dir}/*.fbs")
set(schema_gen ${schema_src})
list(TRANSFORM schema_gen REPLACE "\\.fbs$" ".gen.h")
add_custom_command(
    DEPENDS ${schema_src}
    OUTPUT ${schema_gen}
    COMMAND ${FLATC} --cpp --filename-suffix "" -I ${schema_src_dir} -o ${schema_gen_dir} ${schema_src}
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Compiling schemas"
    VERBATIM
)
add_custom_target(compile_schemas DEPENDS ${schema_gen})

# Wrap generated headers to an interface library
add_library(schemas INTERFACE)
add_dependencies(schemas compile_schemas)
target_include_directories(schemas INTERFACE "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(schemas INTERFACE flatbuffers)

# Main executable
add_executable(chat
    src/chat.cpp
    src/ansi_sequences.h
    src/utf8_builder.cpp
    src/utf8_builder.h
)
target_link_libraries(chat schemas zenohc::lib zenohcxx::zenohc mio::mio)

# Define the macro "DEBUG" if building with debug profile
target_compile_definitions(chat PRIVATE $<$<CONFIG:Debug>:DEBUG>)

if (BUILD_SHARED_LIBS)
    # Copy shared libraries after building
    add_custom_command(
        TARGET chat
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy ${dylib_full_path} ${CMAKE_BINARY_DIR}
        COMMENT "Copying shared library"
    )
endif ()
