# ==============================================================================
# WeaR-studio Plugins
# plugins/CMakeLists.txt
# ==============================================================================

# ==============================================================================
# Example Plugin - Color Source
# ==============================================================================
add_library(example_plugin MODULE
    ExamplePlugin.cpp
    ExamplePlugin.h
    ExamplePlugin.json
)

# Alias for cleaner usage
add_library(WeaRStudio::ExamplePlugin ALIAS example_plugin)

# ==============================================================================
# Qt6 Plugin Configuration
# ==============================================================================
target_link_libraries(example_plugin
    PRIVATE
        Qt6::Core
        Qt6::Widgets
        Qt6::Gui
        
        # Link interface-only (headers) from core
        core
)

# ==============================================================================
# Include Directories
# ==============================================================================
target_include_directories(example_plugin
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/core
)

# ==============================================================================
# Compile Features
# ==============================================================================
target_compile_features(example_plugin PRIVATE cxx_std_20)

# ==============================================================================
# Plugin Output Configuration
# ==============================================================================
set_target_properties(example_plugin PROPERTIES
    # Plugin naming
    PREFIX ""
    OUTPUT_NAME "ExamplePlugin"
    
    # Place plugins in dedicated folder
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/plugins"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/plugins"
    
    # Position independent code for shared library
    POSITION_INDEPENDENT_CODE ON
    
    # C++ visibility
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
)

# ==============================================================================
# Plugin Definitions
# ==============================================================================
target_compile_definitions(example_plugin
    PRIVATE
        WEAR_PLUGIN_EXPORTS
        QT_PLUGIN
)

# ==============================================================================
# Installation
# ==============================================================================
install(TARGETS example_plugin
    LIBRARY DESTINATION bin/plugins
    RUNTIME DESTINATION bin/plugins
)

# ==============================================================================
# Future Plugins Template
# ==============================================================================
# To add a new plugin:
# 1. Create a new directory for your plugin
# 2. Implement IPlugin/ISource/IFilter interface
# 3. Add the following:
#
# add_library(my_plugin MODULE
#     MyPlugin.cpp
#     MyPlugin.h
# )
# target_link_libraries(my_plugin PRIVATE Qt6::Core Qt6::Widgets core)
# set_target_properties(my_plugin PROPERTIES
#     PREFIX ""
#     LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/plugins"
# )
