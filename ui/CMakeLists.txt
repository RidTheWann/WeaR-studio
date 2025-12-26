# ==============================================================================
# WeaR-studio UI Application
# ui/CMakeLists.txt
# ==============================================================================

# UI sources
set(UI_SOURCES
    main.cpp
    WeaRApp.cpp
    WeaRApp.h
    MainWindow.cpp
    MainWindow.h
    PreviewWidget.cpp
    PreviewWidget.h
)

# Create the main executable
add_executable(WeaRStudio WIN32
    ${UI_SOURCES}
)

# ==============================================================================
# Qt6 Dependencies
# ==============================================================================
target_link_libraries(WeaRStudio
    PRIVATE
        # Core library
        core
        
        # Qt6 modules
        Qt6::Core
        Qt6::Widgets
        Qt6::Gui
        Qt6::Network
        Qt6::Multimedia
        Qt6::OpenGL
        Qt6::OpenGLWidgets
        Qt6::Quick
)

# ==============================================================================
# Include Directories
# ==============================================================================
target_include_directories(WeaRStudio
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/core
)

# ==============================================================================
# Compile Features
# ==============================================================================
target_compile_features(WeaRStudio PRIVATE cxx_std_20)

# ==============================================================================
# Windows Application Properties
# ==============================================================================
if(WIN32)
    # Set application properties
    set_target_properties(WeaRStudio PROPERTIES
        WIN32_EXECUTABLE TRUE
        VS_DEBUGGER_ENVIRONMENT "PATH=${CMAKE_PREFIX_PATH}/bin;$ENV{PATH}"
    )
    
    # Application icon (if resource file exists)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/resources/app.rc")
        target_sources(WeaRStudio PRIVATE resources/app.rc)
    endif()
endif()

# ==============================================================================
# Output Name
# ==============================================================================
set_target_properties(WeaRStudio PROPERTIES
    OUTPUT_NAME "WeaR-Studio"
    DEBUG_POSTFIX "_d"
)

# ==============================================================================
# Deploy Qt Runtime DLLs (Windows)
# ==============================================================================
if(WIN32)
    get_target_property(_qmake_executable Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(_qt_bin_dir "${_qmake_executable}" DIRECTORY)
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${_qt_bin_dir}")
    
    if(WINDEPLOYQT_EXECUTABLE)
        add_custom_command(TARGET WeaRStudio POST_BUILD
            COMMAND "${WINDEPLOYQT_EXECUTABLE}"
                    --no-translations
                    --no-system-d3d-compiler
                    --no-opengl-sw
                    "$<TARGET_FILE:WeaRStudio>"
            COMMENT "Running windeployqt..."
        )
    endif()
endif()

