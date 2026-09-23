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
    AudioMixerDock.cpp
    AudioMixerDock.h
)

# Create the main executable
add_executable(WeaRStudio WIN32
    ${UI_SOURCES}
)

# ==============================================================================
# Qt6 Dependencies
# ==============================================================================
# ==============================================================================
# Precompile cross-backend RHI shaders into the application executable
# ==============================================================================
qt_add_shaders(WeaRStudio "wear_rhi_shaders"
    PREFIX "/wear/rhi"
    PRECOMPILE
    OPTIMIZED
    FILES
        ${CMAKE_SOURCE_DIR}/shaders/rhi_composite.vert
        ${CMAKE_SOURCE_DIR}/shaders/rhi_composite.frag
)

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
if(WEAR_ENABLE_CEF)
    target_link_libraries(WeaRStudio PRIVATE libcef_dll_wrapper)
    target_include_directories(WeaRStudio PRIVATE "${CEF_ROOT}")
    add_dependencies(WeaRStudio libcef_dll_wrapper)

    # Deploy the CEF binary payload beside the application executable.
    set(WEAR_CEF_RUNTIME_DIR "${CEF_ROOT}/$<CONFIG>")
    foreach(_cef_binary IN LISTS CEF_BINARY_FILES)
        add_custom_command(TARGET WeaRStudio POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                    "${WEAR_CEF_RUNTIME_DIR}/${_cef_binary}"
                    "$<TARGET_FILE_DIR:WeaRStudio>/${_cef_binary}"
        )
    endforeach()

    add_custom_command(TARGET WeaRStudio POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_directory
                "${CEF_RESOURCE_DIR}"
                "$<TARGET_FILE_DIR:WeaRStudio>"
        COMMENT "Deploying CEF runtime/resources..."
    )
endif()

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

