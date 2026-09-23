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
    # Locate the Windows SDK installation used for C++/WinRT headers.
    if(DEFINED ENV{WindowsSdkDir} AND NOT "$ENV{WindowsSdkDir}" STREQUAL "")
        file(TO_CMAKE_PATH "$ENV{WindowsSdkDir}" WINDOWS_SDK_DIR)
    else()
        set(WINDOWS_SDK_DIR
            "$ENV{ProgramFiles(x86)}/Windows Kits/10"
        )
    endif()

    # Prefer the SDK selected by the active Visual Studio generator, but only
    # when it actually contains the required C++/WinRT Direct3D interop header.
    set(CPPWINRT_INCLUDE_DIR "")
    if(DEFINED CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION)
        set(_selected_cppwinrt
            "${WINDOWS_SDK_DIR}/Include/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/cppwinrt"
        )
        if(EXISTS
            "${_selected_cppwinrt}/windows.graphics.directx.direct3d11.interop.h"
        )
            set(CPPWINRT_INCLUDE_DIR "${_selected_cppwinrt}")
        endif()
    endif()

    # Otherwise choose the newest installed SDK that contains the required
    # header. This avoids assuming a particular SDK version is installed.
    if(NOT CPPWINRT_INCLUDE_DIR)
        file(GLOB _cppwinrt_candidates
            "${WINDOWS_SDK_DIR}/Include/*/cppwinrt"
        )
        list(SORT _cppwinrt_candidates COMPARE NATURAL ORDER DESCENDING)

        foreach(candidate IN LISTS _cppwinrt_candidates)
            if(EXISTS
                "${candidate}/windows.graphics.directx.direct3d11.interop.h"
            )
                set(CPPWINRT_INCLUDE_DIR "${candidate}")
                break()
            endif()
        endforeach()
    endif()

    if(NOT CPPWINRT_INCLUDE_DIR)
        message(FATAL_ERROR
            "Could not find Windows SDK C++/WinRT headers under: "
            "${WINDOWS_SDK_DIR}/Include"
        )
    endif()

    target_include_directories(core PRIVATE "${CPPWINRT_INCLUDE_DIR}")
    message(STATUS
        "C++/WinRT headers found at: ${CPPWINRT_INCLUDE_DIR}"
    )

    target_link_libraries(core
        PUBLIC
            # Desktop Window Manager (for window enumeration)
            dwmapi

            # Direct3D 11 (for GPU capture and rendering)
            d3d11
            dxgi
            d3dcompiler
            dbghelp

            # Windows Runtime
            windowsapp
            RuntimeObject

            # COM / shell libraries
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

    # MSVC-specific C++/WinRT compiler options. Do not pass /flags to GNU.
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
