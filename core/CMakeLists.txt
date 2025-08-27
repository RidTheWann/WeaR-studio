add_library(core STATIC
    CaptureManager.cpp
    CaptureManager.h
    EncoderManager.cpp
    EncoderManager.h
    StreamManager.cpp
    StreamManager.h
    SceneManager.cpp
    SceneManager.h
    PluginManager.cpp
    PluginManager.h
)

target_include_directories(core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(core PUBLIC PkgConfig::FFMPEG Qt6::Widgets)
