# ==============================================================================
# WeaR-studio Core Library
# core/CMakeLists.txt
# ==============================================================================

# Core library sources
set(CORE_SOURCES
    AppDiagnostics.cpp
    AppDiagnostics.h
    CaptureManager.cpp
    CaptureManager.h
    EncoderManager.cpp
    EncoderManager.h
    StreamManager.cpp
    StreamManager.h
    SceneManager.cpp
    SceneManager.h
    Scene.cpp
    Scene.h
    SceneItem.cpp
    SceneItem.h
    SceneTransition.cpp
    SceneTransition.h
    AudioCaptureSource.cpp
    AudioCaptureSource.h
    AudioMixer.cpp
    AudioMixer.h
    RecordingManager.cpp
    RecordingManager.h
    IRhiFilter.h
    BuiltinRhiFilters.cpp
    BuiltinRhiFilters.h
    RhiCompositor.cpp
    RhiCompositor.h
    PluginManager.cpp
    PluginManager.h
)

# Interface headers (for plugin system)
set(CORE_INTERFACE_HEADERS
    IPlugin.h
    ISource.h
    IFilter.h
)

# Create the core static library
add_library(core STATIC
    ${CORE_SOURCES}
    ${CORE_INTERFACE_HEADERS}
)

# Alias for cleaner usage
add_library(WeaRStudio::Core ALIAS core)

# ==============================================================================
# Include Directories
# ==============================================================================
target_include_directories(core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${FFMPEG_INCLUDE_DIRS}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/private
)

# ==============================================================================
# Qt6 Dependencies
# ==============================================================================
target_link_libraries(core
    PUBLIC
        Qt6::Core
        Qt6::Widgets
        Qt6::Gui
        Qt6::Multimedia
        Qt6::OpenGL
        Qt6::OpenGLWidgets
        Qt6::GuiPrivate
)

# ==============================================================================
# FFmpeg Dependencies
# ==============================================================================
target_link_libraries(core
    PUBLIC
        WeaR::FFmpeg
)

# ==============================================================================
# Windows-Specific Dependencies
# ==============================================================================
if(WIN32)
    # Find Windows SDK for C++/WinRT headers
    # C++/WinRT is included in Windows SDK 10.0.17134.0 and later
    if(DEFINED ENV{WindowsSdkDir})
        set(WINDOWS_SDK_DIR "$ENV{WindowsSdkDir}")
    else()
        set(WINDOWS_SDK_DIR "C:/Program Files (x86)/Windows Kits/10")
    endif()
    
    # Resolve the Windows SDK C++/WinRT header directory from the
    # active Visual Studio toolchain/environment, with a fallback scan.
    if(CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION)
        set(WINDOWS_SDK_VERSION "${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}")
    elseif(DEFINED ENV{WindowsSDKVersion})
        string(REGEX REPLACE "\\\\$" "" WINDOWS_SDK_VERSION "$ENV{WindowsSDKVersion}")
    else()
        file(GLOB SDK_VERSIONS
            "${WINDOWS_SDK_DIR}/Include/*/cppwinrt"
        )
        if(NOT SDK_VERSIONS)
            message(FATAL_ERROR
                "Could not find the Windows SDK C++/WinRT headers."
            )
        endif()
        list(SORT SDK_VERSIONS COMPARE NATURAL ORDER DESCENDING)
        list(GET SDK_VERSIONS 0 CPPWINRT_INCLUDE_DIR)
    endif()

    if(NOT DEFINED CPPWINRT_INCLUDE_DIR)
        set(CPPWINRT_INCLUDE_DIR
            "${WINDOWS_SDK_DIR}/Include/${WINDOWS_SDK_VERSION}/cppwinrt"
        )
    endif()

    if(NOT EXISTS
        "${CPPWINRT_INCLUDE_DIR}/windows.graphics.directx.direct3d11.interop.h"
    )
        message(FATAL_ERROR
            "C++/WinRT Direct3D interop headers were not found at "
            "${CPPWINRT_INCLUDE_DIR}"
        )
    endif()

    target_include_directories(core PRIVATE "${CPPWINRT_INCLUDE_DIR}")
    message(STATUS "C++/WinRT headers found at: ${CPPWINRT_INCLUDE_DIR}")

    target_link_libraries(core
        PUBLIC
            # Desktop Window Manager (for window enumeration)
            dwmapi
            
            # Direct3D 11 (for GPU capture and rendering)
            d3d11
            dxgi
            d3dcompiler
            dbghelp
            
            # Windows Runtime (for Graphics Capture API)
            windowsapp
            RuntimeObject
            
            # Additional Windows libraries
            ole32
            oleaut32
            uuid
            
            # Media Foundation (for webcam capture)
            mf
            mfplat
            mfreadwrite
            mfuuid
            
            # Windows Imaging Component
            windowscodecs
            
            # Shell API (for display scaling)
            shcore
    )
    
    # Enable C++/WinRT compiler options only for MSVC.
    # GNU builds must not receive MSVC-style /flags.
    if(MSVC)
        target_compile_options(core PRIVATE
            /await:strict
            /EHsc
            /bigobj
        )
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(core PRIVATE
            -municode
        )
    endif()
    
    # Define for Windows Runtime support
    target_compile_definitions(core PRIVATE
        WINRT_LEAN_AND_MEAN
        _SILENCE_ALL_CXX17_DEPRECATION_WARNINGS
    )
endif()

# ==============================================================================
# Compile Definitions
# ==============================================================================
target_compile_definitions(core
    PRIVATE
        # FFmpeg deprecation warnings
        FF_API_OLD_ENCODE_AUDIO=0
        
        # Enable hardware acceleration detection
        WEAR_ENABLE_NVENC=1
        WEAR_ENABLE_AMF=1
        WEAR_ENABLE_QSV=1
    PUBLIC
        # Export/Import macros for potential DLL usage
        $<$<BOOL:${BUILD_SHARED_LIBS}>:WEAR_CORE_EXPORTS>
)

# ==============================================================================
# Compile Features
# ==============================================================================
target_compile_features(core PUBLIC cxx_std_20)

# ==============================================================================
# Precompiled Headers (for faster compilation)
# ==============================================================================
if(MSVC)
    target_precompile_headers(core PRIVATE
        <QObject>
        <QString>
        <QImage>
        <QMutex>
        <QThread>
        <memory>
        <vector>
        <functional>
    )
endif()

# ==============================================================================
# Installation
# ==============================================================================
install(TARGETS core
    EXPORT WeaRStudioTargets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
    INCLUDES DESTINATION include/wear/core
)

install(FILES ${CORE_INTERFACE_HEADERS}
    DESTINATION include/wear/core
)
