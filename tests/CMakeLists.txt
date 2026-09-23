# ==============================================================================
# WeaR-studio Tests
# tests/CMakeLists.txt
# ==============================================================================

add_executable(AudioMixerTest
    AudioMixerTest.cpp
)

target_link_libraries(AudioMixerTest
    PRIVATE
        core
        Qt6::Core
)

target_include_directories(AudioMixerTest
    PRIVATE
        ${CMAKE_SOURCE_DIR}/core
)

target_compile_features(AudioMixerTest PRIVATE cxx_std_20)

add_test(NAME AudioMixerTest COMMAND AudioMixerTest)


add_executable(RecordingManagerTest
    RecordingManagerTest.cpp
)

target_link_libraries(RecordingManagerTest
    PRIVATE
        core
        Qt6::Core
)

target_include_directories(RecordingManagerTest
    PRIVATE
        ${CMAKE_SOURCE_DIR}/core
)

target_compile_features(RecordingManagerTest PRIVATE cxx_std_20)

add_test(NAME RecordingManagerTest COMMAND RecordingManagerTest)
