# FindFFMPEG.cmake
# CMake module to find FFmpeg libraries

find_path(FFMPEG_INCLUDE_DIR NAMES libavcodec/avcodec.h)
find_library(AVCODEC_LIBRARY NAMES avcodec)
find_library(AVFORMAT_LIBRARY NAMES avformat)
find_library(AVUTIL_LIBRARY NAMES avutil)
find_library(SWSCALE_LIBRARY NAMES swscale)

set(FFMPEG_LIBRARIES ${AVCODEC_LIBRARY} ${AVFORMAT_LIBRARY} ${AVUTIL_LIBRARY} ${SWSCALE_LIBRARY})
set(FFMPEG_INCLUDE_DIRS ${FFMPEG_INCLUDE_DIR})

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(FFMPEG DEFAULT_MSG FFMPEG_LIBRARIES FFMPEG_INCLUDE_DIRS)

mark_as_advanced(FFMPEG_INCLUDE_DIR FFMPEG_LIBRARIES)
