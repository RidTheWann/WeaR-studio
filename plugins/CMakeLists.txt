add_library(example_plugin SHARED
    ExamplePlugin.cpp
    ExamplePlugin.h
)

target_link_libraries(example_plugin PRIVATE core Qt6::Widgets)
