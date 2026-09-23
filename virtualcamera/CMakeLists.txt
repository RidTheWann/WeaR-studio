# ==============================================================================
# WeaR Studio Windows 11 Virtual Camera Media Source
# ==============================================================================
if(WIN32)
    add_library(wear_virtual_camera_source SHARED
        VirtualCameraMediaSource.cpp
        VirtualCameraMediaSource.h
        VirtualCameraMediaSource.def
    )

    target_include_directories(wear_virtual_camera_source
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_SOURCE_DIR}/core
    )

    target_compile_features(wear_virtual_camera_source PRIVATE cxx_std_20)

    target_link_libraries(wear_virtual_camera_source
        PRIVATE
            mfplat
            mfuuid
            mfsensorgroup
            shlwapi
            ole32
            oleaut32
            advapi32
    )

    set_target_properties(wear_virtual_camera_source PROPERTIES
        PREFIX ""
        OUTPUT_NAME "WeaRVirtualCamera"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/virtualcamera"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/virtualcamera"
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib/virtualcamera"
        WINDOWS_EXPORT_ALL_SYMBOLS OFF
    )

    target_compile_definitions(wear_virtual_camera_source
        PRIVATE
            UNICODE
            _UNICODE
            NOMINMAX
    )

    install(TARGETS wear_virtual_camera_source
        RUNTIME DESTINATION bin/virtualcamera
        LIBRARY DESTINATION bin/virtualcamera
        ARCHIVE DESTINATION lib/virtualcamera
    )
endif()
