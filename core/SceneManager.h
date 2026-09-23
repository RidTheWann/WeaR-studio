#pragma once
// ==============================================================================
// WeaR-studio SceneManager
// Manages scenes and runs the render loop for video composition
// ==============================================================================

#include "Scene.h"
#include "SceneItem.h"

#include <QObject>
#include <QMutex>
#include <QTimer>
#include <QImage>
#include <QElapsedTimer>
#include <QString>

#include <memory>
#include <atomic>
#include <functional>

namespace WeaR {

/**
 * @brief Render output target
 */
enum class RenderTarget {
    Preview,    ///< Output for UI preview
    Stream,     ///< Output for encoding/streaming
    Both        ///< Both preview and stream
};

/**
 * @brief Render loop statistics
 */
struct RenderStatistics {
    int64_t framesRendered = 0;     ///< Total frames rendered
    double currentFps = 0.0;        ///< Current render FPS
    double averageRenderTimeMs = 0.0; ///< Average render time
    double targetFps = 60.0;        ///< Target FPS
    int64_t droppedFrames = 0;      ///< Frames dropped due to timing

    // Composition-specific telemetry.
    double compositingWallTimeMs = 0.0;
    double compositingCpuTimeMs = 0.0;
    double compositingCpuUsagePercent = 0.0;
    QString compositingBackend = QStringLiteral("QPainter");
    QString rhiBackend;
    int64_t rhiFrames = 0;
    int64_t qPainterFrames = 0;
};

/**
 * @brief Callback for preview frame updates
 */
using PreviewFrameCallback = std::function<void(const QImage& frame)>;

/**
 * @brief Scene and render loop manager
 * 
 * SceneManager is responsible for:
 * - Managing multiple scenes
 * - Switching between active scene
 * - Running the render loop
 * - Outputting frames to preview and encoder
 * 
 * Thread-safe Singleton pattern for application-wide access.
 * 
 * Usage:
 * @code
 *   auto& scene = SceneManager::instance();
 *   
 *   // Create a scene
 *   Scene* myScene = scene.createScene("Main Scene");
 *   scene.setActiveScene(myScene);
 *   
 *   // Add a source
 *   myScene->addItem("Screen Capture", &CaptureManager::instance());
 *   
 *   // Set preview callback
 *   scene.setPreviewCallback([](const QImage& frame) {
 *       previewWidget->setFrame(frame);
 *   });
 *   
 *   // Start render loop
 *   scene.startRenderLoop();
 * @endcode
 */
class RhiCompositor;

class SceneManager : public QObject {
    Q_OBJECT

public:
    /**
     * @brief Get singleton instance
     * @return Reference to the SceneManager instance
     */
    static SceneManager& instance();

    // Prevent copying
    SceneManager(const SceneManager&) = delete;
    SceneManager& operator=(const SceneManager&) = delete;

    ~SceneManager() override;

    // =========================================================================
    // Output Configuration
    // =========================================================================
    
    /**
     * @brief Set output resolution
     */
    void setOutputResolution(const QSize& size);
    void setOutputResolution(int width, int height) { setOutputResolution(QSize(width, height)); }
    
    /**
     * @brief Get output resolution
     */
    [[nodiscard]] QSize outputResolution() const { return m_outputResolution; }
    
    /**
     * @brief Set target frame rate
     */
    void setTargetFps(double fps);
    
    /**
     * @brief Get target frame rate
     */
    [[nodiscard]] double targetFps() const { return m_targetFps; }
    
    /**
     * @brief Set preview callback
     */
    void setPreviewCallback(PreviewFrameCallback callback);
    
    /**
     * @brief Enable/disable encoder output
     */
    void setEncoderOutputEnabled(bool enabled);
    
    /**
     * @brief Check if encoder output is enabled
     */
    [[nodiscard]] bool isEncoderOutputEnabled() const { return m_encoderOutputEnabled; }

    /**
     * @brief Enable/disable independent local recording output.
     */
    void setRecordingOutputEnabled(bool enabled);

    /**
     * @brief Check if local recording output is enabled.
     */
    [[nodiscard]] bool isRecordingOutputEnabled() const { return m_recordingOutputEnabled; }

    // =========================================================================
    // Scene Management
    // =========================================================================
    
    /**
     * @brief Create a new scene
     * @param name Scene name
     * @return Pointer to created scene
     */
    Scene* createScene(const QString& name = QString());
    
    /**
     * @brief Remove a scene
     * @param scene Scene to remove
     * @return true if removed
     */
    bool removeScene(Scene* scene);
    
    /**
     * @brief Get all scenes
     */
    [[nodiscard]] QList<Scene*> scenes() const;
    
    /**
     * @brief Get scene count
     */
    [[nodiscard]] int sceneCount() const;
    
    /**
     * @brief Get active scene
     */
    [[nodiscard]] Scene* activeScene() const { return m_activeScene; }
    
    /**
     * @brief Set the transition type used for subsequent scene changes.
     */
    void setTransitionType(SceneTransitionType type);

    /**
     * @brief Get the transition type used for subsequent scene changes.
     */
    [[nodiscard]] SceneTransitionType transitionType() const { return m_transitionType; }

    /**
     * @brief Configure the duration for one transition type.
     * @param type Transition type
     * @param durationMs Duration in milliseconds. Cut is always immediate.
     */
    void setTransitionDuration(SceneTransitionType type, int durationMs);

    /**
     * @brief Get the configured duration for one transition type.
     */
    [[nodiscard]] int transitionDuration(SceneTransitionType type) const;

    /**
     * @brief Whether an animated scene transition is currently running.
     */
    [[nodiscard]] bool isTransitionActive() const { return m_transition.active; }

    /**
     * @brief Current transition progress in the range [0, 1].
     */
    [[nodiscard]] double transitionProgress() const;

    /**
     * @brief Set active scene using the configured transition.
     */
    void setActiveScene(Scene* scene);
    
    /**
     * @brief Get scene by name
     */
    [[nodiscard]] Scene* sceneByName(const QString& name) const;
    
    /**
     * @brief Get scene by ID
     */
    [[nodiscard]] Scene* sceneById(const QUuid& id) const;

    // =========================================================================
    // Render Loop Control
    // =========================================================================
    
    /**
     * @brief Start the render loop
     * @return true if started
     */
    bool startRenderLoop();
    
    /**
     * @brief Stop the render loop
     */
    void stopRenderLoop();
    
    /**
     * @brief Check if render loop is running
     */
    [[nodiscard]] bool isRenderLoopRunning() const { return m_renderLoopRunning; }
    
    /**
     * @brief Force render a single frame
     * @return Rendered frame
     */
    QImage renderFrame();
    
    /**
     * @brief Get last rendered frame
     */
    [[nodiscard]] QImage lastFrame() const;
    
    /**
     * @brief Get render statistics
     */
    [[nodiscard]] RenderStatistics statistics() const;

signals:
    /**
     * @brief Emitted when active scene changes
     */
    void activeSceneChanged(Scene* scene);
    
    /**
     * @brief Emitted when a scene is added
     */
    void sceneAdded(Scene* scene);
    
    /**
     * @brief Emitted when a scene is removed
     */
    void sceneRemoved(const QUuid& id);
    
    /**
     * @brief Emitted when a frame is rendered
     */
    void frameRendered(int64_t frameNumber);
    
    /**
     * @brief Emitted when render loop starts
     */
    void renderLoopStarted();
    
    /**
     * @brief Emitted when render loop stops
     */
    void renderLoopStopped();

private slots:
    void onRenderTick();

private:
    // Private constructor for singleton
    explicit SceneManager(QObject* parent = nullptr);
    
    // Render implementation
    void doRender();
    QImage renderFrameQPainter();
    void updateCompositingStats(
        double wallTimeMs,
        double cpuTimeMs,
        const QString& backendName);
    void outputToEncoder(const QImage& frame);
    void outputToRecorder(const QImage& frame);
    void outputToPreview(const QImage& frame);
    QImage applySceneTransition(const QImage& incomingFrame);
    void cancelSceneTransition();
    
    // Scenes
    QList<Scene*> m_scenes;
    Scene* m_activeScene = nullptr;
    mutable QMutex m_sceneMutex;
    
    // Output settings
    QSize m_outputResolution{1920, 1080};
    double m_targetFps = 60.0;
    
    // Render loop
    QTimer* m_renderTimer = nullptr;
    std::atomic<bool> m_renderLoopRunning{false};
    QElapsedTimer m_frameTimer;
    int64_t m_lastFrameTime = 0;
    
    // Output
    PreviewFrameCallback m_previewCallback;
    std::atomic<bool> m_encoderOutputEnabled{true};
    std::atomic<bool> m_recordingOutputEnabled{false};
    
    // Frame buffer
    QImage m_lastFrame;
    mutable QMutex m_frameMutex;
    
    // Statistics
    RenderStatistics m_stats;
    mutable QMutex m_statsMutex;
    QList<double> m_renderTimes;
    QList<double> m_compositingWallTimes;
    QList<double> m_compositingCpuTimes;

    // Scene transition settings/state. The transition clock is monotonic and
    // advances only when renderFrame() is called, so it cannot block encoder
    // or stream delivery with sleeps or auxiliary timers.
    SceneTransitionType m_transitionType = SceneTransitionType::Fade;
    std::array<int, 3> m_transitionDurationsMs{0, 300, 300};
    struct ActiveTransition {
        bool active = false;
        SceneTransitionType type = SceneTransitionType::Cut;
        int durationMs = 0;
        QElapsedTimer clock;
        QImage fromFrame;
        Scene* targetScene = nullptr;
    } m_transition;

    // GPU compositor. A failed/unsupported RHI path never prevents the
    // established QPainter renderer from producing a frame.
    std::unique_ptr<RhiCompositor> m_rhiCompositor;
    bool m_loggedRhiFallback = false;
};

} // namespace WeaR
