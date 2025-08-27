cmake_minimum_required(VERSION 3.16)
project(WeaRStudio VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 COMPONENTS Widgets REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavcodec libavformat libavutil libswscale)

add_subdirectory(core)
add_subdirectory(ui)
add_subdirectory(plugins)

# Optionally add docs and cmake folders
