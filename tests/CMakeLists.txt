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


# GPU compositor integration/visual regression coverage.
add_executable(RhiCompositorTest
    RhiCompositorTest.cpp
)

target_link_libraries(RhiCompositorTest
    PRIVATE
        core
        Qt6::Gui
)

target_include_directories(RhiCompositorTest
    PRIVATE
        ${CMAKE_SOURCE_DIR}/core
)

target_compile_features(RhiCompositorTest PRIVATE cxx_std_20)

add_test(NAME RhiCompositorTest COMMAND RhiCompositorTest)

# QPainter vs RHI CPU-cost benchmark. The test skips cleanly when no RHI
# backend is available on the host.
add_executable(CompositingBenchmark
    CompositingBenchmark.cpp
)

target_link_libraries(CompositingBenchmark
    PRIVATE
        core
        Qt6::Gui
)

target_include_directories(CompositingBenchmark
    PRIVATE
        ${CMAKE_SOURCE_DIR}/core
)

target_compile_features(CompositingBenchmark PRIVATE cxx_std_20)

add_test(NAME CompositingBenchmark COMMAND CompositingBenchmark)


set_tests_properties(CompositingBenchmark PROPERTIES LABELS "benchmark")


# Exercises the automatic SceneManager QPainter safety path using a blend mode
# that is intentionally not migrated to the RHI shader yet.
add_executable(SceneManagerFallbackTest
    SceneManagerFallbackTest.cpp
)

target_link_libraries(SceneManagerFallbackTest
    PRIVATE
        core
        Qt6::Gui
)

target_include_directories(SceneManagerFallbackTest
    PRIVATE
        ${CMAKE_SOURCE_DIR}/core
)

target_compile_features(SceneManagerFallbackTest PRIVATE cxx_std_20)

add_test(NAME SceneManagerFallbackTest COMMAND SceneManagerFallbackTest)
