cmake_minimum_required(VERSION 3.15)
project(mp4tag VERSION 1.0.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ---------- Sources ----------
set(MP4TAG_SOURCES
    src/mp4tag.c
    src/mp4/mp4_atoms.c
    src/mp4/mp4_parser.c
    src/mp4/mp4_tags.c
    src/io/file_io.c
    src/util/buffer.c
    src/util/string_util.c
)

add_library(mp4tag STATIC ${MP4TAG_SOURCES})

target_include_directories(mp4tag
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Strict warnings
target_compile_options(mp4tag PRIVATE
    -Wall -Wextra -Wpedantic -Wno-unused-parameter
)

# ---------- Apple platform settings ----------
if(APPLE)
    set_target_properties(mp4tag PROPERTIES
        XCODE_ATTRIBUTE_CLANG_ENABLE_MODULES YES
    )
    if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "Minimum macOS version")
    elseif(CMAKE_SYSTEM_NAME STREQUAL "iOS")
        set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING "Minimum iOS version")
    endif()
endif()

# ---------- Install ----------
include(GNUInstallDirs)

install(TARGETS mp4tag
    EXPORT mp4tagTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(DIRECTORY include/mp4tag
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(EXPORT mp4tagTargets
    FILE mp4tagTargets.cmake
    NAMESPACE mp4tag::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/mp4tag
)

# ---------- Tests (optional) ----------
option(MP4TAG_BUILD_TESTS "Build tests" OFF)
if(MP4TAG_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
