add_executable(WeaRStudio
    main.cpp
    WeaRApp.cpp
    WeaRApp.h
    MainWindow.cpp
    MainWindow.h
)

target_link_libraries(WeaRStudio PRIVATE core Qt6::Widgets)
