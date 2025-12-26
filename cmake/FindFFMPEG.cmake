# ==============================================================================
# FindFFMPEG.cmake
# CMake module to find FFmpeg libraries for WeaR-studio
# 
# This module defines:
#   FFMPEG_FOUND          - True if FFmpeg was found
#   FFMPEG_INCLUDE_DIRS   - FFmpeg include directories
#   FFMPEG_LIBRARIES      - FFmpeg libraries to link against
#   FFMPEG_VERSION        - FFmpeg version string
#   WeaR::FFmpeg          - Imported target for linkage (avoid Qt conflict)
#
# You can set FFMPEG_ROOT or FFMPEG_DIR environment variable or CMake variable
# to specify the FFmpeg installation directory.
# ==============================================================================

# Prevent Qt's FindFFMPEG from being used
set(CMAKE_FIND_PACKAGE_PREFER_CONFIG OFF)

# Determine FFmpeg root directory from various sources
if(NOT FFMPEG_ROOT)
    if(DEFINED ENV{FFMPEG_ROOT})
        set(FFMPEG_ROOT "$ENV{FFMPEG_ROOT}")
    elseif(DEFINED ENV{FFMPEG_DIR})
        set(FFMPEG_ROOT "$ENV{FFMPEG_DIR}")
    endif()
endif()

# Search paths
set(_FFMPEG_SEARCH_PATHS
    ${FFMPEG_ROOT}
    "C:/ffmpeg"
    "C:/Program Files/ffmpeg"
    "C:/Program Files (x86)/ffmpeg"
    "$ENV{ProgramFiles}/ffmpeg"
)

message(STATUS "Searching for FFmpeg in: ${_FFMPEG_SEARCH_PATHS}")

# ==============================================================================
# Find Include Directory
# ==============================================================================
find_path(FFMPEG_INCLUDE_DIR
    NAMES libavcodec/avcodec.h
    PATHS ${_FFMPEG_SEARCH_PATHS}
    PATH_SUFFIXES include
    DOC "FFmpeg include directory"
    NO_CMAKE_FIND_ROOT_PATH
)

if(FFMPEG_INCLUDE_DIR)
    message(STATUS "Found FFmpeg include: ${FFMPEG_INCLUDE_DIR}")
endif()

# ==============================================================================
# Find Libraries - Direct approach for Windows
# ==============================================================================
# On Windows with pre-built FFmpeg, .lib files are import libraries

# AVCODEC
find_library(FFMPEG_AVCODEC_LIBRARY
    NAMES avcodec libavcodec
    PATHS ${_FFMPEG_SEARCH_PATHS}
    PATH_SUFFIXES lib lib64
    NO_CMAKE_FIND_ROOT_PATH
)

# AVFORMAT
find_library(FFMPEG_AVFORMAT_LIBRARY
    NAMES avformat libavformat
    PATHS ${_FFMPEG_SEARCH_PATHS}
    PATH_SUFFIXES lib lib64
    NO_CMAKE_FIND_ROOT_PATH
)

# AVUTIL
find_library(FFMPEG_AVUTIL_LIBRARY
    NAMES avutil libavutil
    PATHS ${_FFMPEG_SEARCH_PATHS}
    PATH_SUFFIXES lib lib64
    NO_CMAKE_FIND_ROOT_PATH
)

# SWSCALE
find_library(FFMPEG_SWSCALE_LIBRARY
    NAMES swscale libswscale
    PATHS ${_FFMPEG_SEARCH_PATHS}
    PATH_SUFFIXES lib lib64
    NO_CMAKE_FIND_ROOT_PATH
)

# SWRESAMPLE
find_library(FFMPEG_SWRESAMPLE_LIBRARY
    NAMES swresample libswresample
    PATHS ${_FFMPEG_SEARCH_PATHS}
    PATH_SUFFIXES lib lib64
    NO_CMAKE_FIND_ROOT_PATH
)

# AVDEVICE
find_library(FFMPEG_AVDEVICE_LIBRARY
    NAMES avdevice libavdevice
    PATHS ${_FFMPEG_SEARCH_PATHS}
    PATH_SUFFIXES lib lib64
    NO_CMAKE_FIND_ROOT_PATH
)

# Collect all found libraries
set(FFMPEG_LIBRARIES "")
set(FFMPEG_INCLUDE_DIRS "${FFMPEG_INCLUDE_DIR}")

if(FFMPEG_AVCODEC_LIBRARY)
    list(APPEND FFMPEG_LIBRARIES ${FFMPEG_AVCODEC_LIBRARY})
    set(FFMPEG_AVCODEC_FOUND TRUE)
endif()

if(FFMPEG_AVFORMAT_LIBRARY)
    list(APPEND FFMPEG_LIBRARIES ${FFMPEG_AVFORMAT_LIBRARY})
    set(FFMPEG_AVFORMAT_FOUND TRUE)
endif()

if(FFMPEG_AVUTIL_LIBRARY)
    list(APPEND FFMPEG_LIBRARIES ${FFMPEG_AVUTIL_LIBRARY})
    set(FFMPEG_AVUTIL_FOUND TRUE)
endif()

if(FFMPEG_SWSCALE_LIBRARY)
    list(APPEND FFMPEG_LIBRARIES ${FFMPEG_SWSCALE_LIBRARY})
    set(FFMPEG_SWSCALE_FOUND TRUE)
endif()

if(FFMPEG_SWRESAMPLE_LIBRARY)
    list(APPEND FFMPEG_LIBRARIES ${FFMPEG_SWRESAMPLE_LIBRARY})
    set(FFMPEG_SWRESAMPLE_FOUND TRUE)
endif()

if(FFMPEG_AVDEVICE_LIBRARY)
    list(APPEND FFMPEG_LIBRARIES ${FFMPEG_AVDEVICE_LIBRARY})
    set(FFMPEG_AVDEVICE_FOUND TRUE)
endif()

# ==============================================================================
# Get FFmpeg Version
# ==============================================================================
set(FFMPEG_VERSION "unknown")
if(FFMPEG_INCLUDE_DIR AND EXISTS "${FFMPEG_INCLUDE_DIR}/libavcodec/version.h")
    file(STRINGS "${FFMPEG_INCLUDE_DIR}/libavcodec/version.h" _VERSION_MAJOR_LINE
        REGEX "^#define[ \t]+LIBAVCODEC_VERSION_MAJOR[ \t]+[0-9]+")
    if(_VERSION_MAJOR_LINE)
        string(REGEX REPLACE "^#define[ \t]+LIBAVCODEC_VERSION_MAJOR[ \t]+([0-9]+).*$" "\\1"
            FFMPEG_VERSION "${_VERSION_MAJOR_LINE}")
    endif()
endif()

# ==============================================================================
# Handle REQUIRED and QUIET arguments
# ==============================================================================
include(FindPackageHandleStandardArgs)

find_package_handle_standard_args(FFMPEG
    REQUIRED_VARS 
        FFMPEG_AVCODEC_LIBRARY
        FFMPEG_AVFORMAT_LIBRARY
        FFMPEG_AVUTIL_LIBRARY
        FFMPEG_INCLUDE_DIR
    VERSION_VAR FFMPEG_VERSION
)

# ==============================================================================
# Create Imported Target - Use WeaR:: namespace to avoid Qt conflicts
# ==============================================================================
if(FFMPEG_FOUND AND NOT TARGET WeaR::FFmpeg)
    add_library(WeaR::FFmpeg INTERFACE IMPORTED GLOBAL)
    
    set_target_properties(WeaR::FFmpeg PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${FFMPEG_INCLUDE_DIRS}"
        INTERFACE_LINK_LIBRARIES "${FFMPEG_LIBRARIES}"
    )
    
    message(STATUS "Created WeaR::FFmpeg target")
    message(STATUS "  Include: ${FFMPEG_INCLUDE_DIRS}")
    message(STATUS "  Libraries: ${FFMPEG_LIBRARIES}")
endif()

# ==============================================================================
# Mark Advanced Variables
# ==============================================================================
mark_as_advanced(
    FFMPEG_INCLUDE_DIR
    FFMPEG_AVCODEC_LIBRARY
    FFMPEG_AVFORMAT_LIBRARY
    FFMPEG_AVUTIL_LIBRARY
    FFMPEG_SWSCALE_LIBRARY
    FFMPEG_SWRESAMPLE_LIBRARY
    FFMPEG_AVDEVICE_LIBRARY
)
