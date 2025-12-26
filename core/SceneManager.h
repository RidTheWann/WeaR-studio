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
     * @brief Set active scene
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
    void outputToEncoder(const QImage& frame);
    void outputToPreview(const QImage& frame);
    
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
    
    // Frame buffer
    QImage m_lastFrame;
    mutable QMutex m_frameMutex;
    
    // Statistics
    RenderStatistics m_stats;
    mutable QMutex m_statsMutex;
    QList<double> m_renderTimes;
};

} // namespace WeaR
