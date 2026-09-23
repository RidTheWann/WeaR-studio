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
