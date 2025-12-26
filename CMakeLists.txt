# ==============================================================================
# WeaR-studio - Production-Grade Streaming Software
# Root CMakeLists.txt
# ==============================================================================
cmake_minimum_required(VERSION 3.21)

project(WeaRStudio 
    VERSION 0.1
    DESCRIPTION "Production-grade streaming and recording software"
    LANGUAGES CXX
)

# ==============================================================================
# C++ Standard Configuration
# ==============================================================================
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Enable automatic MOC, UIC, RCC for Qt
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# ==============================================================================
# Build Configuration
# ==============================================================================
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Default to Release build if not specified
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif()

# ==============================================================================
# IMPORTANT: Set our module path FIRST to override Qt's FindFFMPEG
# ==============================================================================
list(INSERT CMAKE_MODULE_PATH 0 "${CMAKE_SOURCE_DIR}/cmake")

# FFmpeg path for our custom finder
set(FFMPEG_ROOT "C:/ffmpeg" CACHE PATH "FFmpeg installation directory")

# ==============================================================================
# MSVC Compiler Flags
# ==============================================================================
if(MSVC)
    # Enable parallel compilation
    add_compile_options(/MP)
    
    # Warning level 4 with some disabled
    add_compile_options(/W4 /wd4100 /wd4189)
    
    # Enable conformance mode
    add_compile_options(/permissive-)
    
    # Enable C++20 coroutines (for WinRT async)
    add_compile_options(/await:strict)
    
    # UTF-8 source and execution charset
    add_compile_options(/utf-8)
    
    # Release optimizations
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        add_compile_options(/O2 /Oi /GL)
        add_link_options(/LTCG)
    endif()
    
    # Debug information
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_compile_options(/Zi /Od)
    endif()
endif()

# ==============================================================================
# Windows Definitions
# ==============================================================================
if(WIN32)
    add_definitions(-DWIN32_LEAN_AND_MEAN)
    add_definitions(-DNOMINMAX)
    add_definitions(-D_CRT_SECURE_NO_WARNINGS)
    add_definitions(-DUNICODE -D_UNICODE)
    
    # Minimum Windows version: Windows 10 1903 (for Graphics Capture API)
    add_definitions(-D_WIN32_WINNT=0x0A00)
    add_definitions(-DWINVER=0x0A00)
    add_definitions(-DNTDDI_VERSION=0x0A000005)
endif()

# ==============================================================================
# FFmpeg Configuration (BEFORE Qt to avoid module conflicts)
# ==============================================================================
find_package(FFMPEG REQUIRED)

if(FFMPEG_FOUND)
    message(STATUS "Found FFmpeg: ${FFMPEG_VERSION}")
    message(STATUS "  Include dirs: ${FFMPEG_INCLUDE_DIRS}")
    message(STATUS "  Libraries: ${FFMPEG_LIBRARIES}")
else()
    message(FATAL_ERROR "FFmpeg not found. Please set FFMPEG_ROOT to your FFmpeg installation directory.")
endif()

# ==============================================================================
# Qt 6.10 Configuration
# ==============================================================================
# Hint for Qt installation path
set(CMAKE_PREFIX_PATH "C:/Qt/6.10.1/msvc2022_64" ${CMAKE_PREFIX_PATH})

find_package(Qt6 6.10 REQUIRED COMPONENTS
    Core
    Widgets
    Gui
    Network
    Multimedia
    OpenGL
    OpenGLWidgets
    Quick
)

# Print Qt version for verification
message(STATUS "Found Qt ${Qt6_VERSION} at ${Qt6_DIR}")

# ==============================================================================
# Global Include Directories
# ==============================================================================
include_directories(
    ${CMAKE_SOURCE_DIR}/core
    ${CMAKE_SOURCE_DIR}/ui
)

# ==============================================================================
# Output Directories
# ==============================================================================
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# ==============================================================================
# Subdirectories
# ==============================================================================
add_subdirectory(core)
add_subdirectory(ui)
add_subdirectory(plugins)

# ==============================================================================
# Summary
# ==============================================================================
message(STATUS "")
message(STATUS "=== WeaR-studio Build Configuration ===")
message(STATUS "Version:        ${PROJECT_VERSION}")
message(STATUS "Build type:     ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ Standard:   ${CMAKE_CXX_STANDARD}")
message(STATUS "Qt Version:     ${Qt6_VERSION}")
message(STATUS "FFmpeg:         ${FFMPEG_ROOT}")
message(STATUS "========================================")
message(STATUS "")
