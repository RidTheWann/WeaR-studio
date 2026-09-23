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


set(WEAR_RHI_SHADER_FILES
    "${CMAKE_SOURCE_DIR}/shaders/rhi_composite.vert"
    "${CMAKE_SOURCE_DIR}/shaders/rhi_composite.frag"
)

function(add_wear_rhi_shaders target)
    qt_add_shaders(${target} "wear_rhi_shaders"
        PREFIX "/wear/rhi"
        BASE "${CMAKE_SOURCE_DIR}"
        PRECOMPILE
        FILES
            ${WEAR_RHI_SHADER_FILES}
    )
endfunction()

# GPU compositor integration/visual regression coverage.
add_executable(RhiCompositorTest
    RhiCompositorTest.cpp
)

add_wear_rhi_shaders(RhiCompositorTest)

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

add_wear_rhi_shaders(CompositingBenchmark)

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

add_wear_rhi_shaders(SceneManagerFallbackTest)

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


# Scene transition rendering and SceneManager integration coverage.
add_executable(SceneTransitionTest
    SceneTransitionTest.cpp
)

target_link_libraries(SceneTransitionTest
    PRIVATE
        core
        Qt6::Gui
)

target_include_directories(SceneTransitionTest
    PRIVATE
        ${CMAKE_SOURCE_DIR}/core
)

target_compile_features(SceneTransitionTest PRIVATE cxx_std_20)

add_test(NAME SceneTransitionTest COMMAND SceneTransitionTest)


add_executable(StreamReconnectTest
    StreamReconnectTest.cpp
)

target_link_libraries(StreamReconnectTest
    PRIVATE
        core
        Qt6::Core
)

target_include_directories(StreamReconnectTest
    PRIVATE
        ${CMAKE_SOURCE_DIR}/core
)

target_compile_features(StreamReconnectTest PRIVATE cxx_std_20)

add_test(NAME StreamReconnectTest COMMAND StreamReconnectTest)


add_executable(AppDiagnosticsTest
    AppDiagnosticsTest.cpp
)

target_link_libraries(AppDiagnosticsTest
    PRIVATE
        core
        Qt6::Core
)

target_include_directories(AppDiagnosticsTest
    PRIVATE
        ${CMAKE_SOURCE_DIR}/core
)

target_compile_features(AppDiagnosticsTest PRIVATE cxx_std_20)

add_test(NAME AppDiagnosticsTest COMMAND AppDiagnosticsTest)
