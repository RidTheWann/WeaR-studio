// ==============================================================================
// WeaR-studio SceneManager Implementation
// Manages scenes and runs the render loop for video composition
// ==============================================================================

#include "SceneManager.h"
#include "EncoderManager.h"

#include <QDebug>
#include <QDateTime>
#include <QPainter>

namespace WeaR {

// ==============================================================================
// SceneManager Singleton
// ==============================================================================
SceneManager& SceneManager::instance() {
    static SceneManager instance;
    return instance;
}

SceneManager::SceneManager(QObject* parent)
    : QObject(parent)
{
    // Create render timer
    m_renderTimer = new QTimer(this);
    m_renderTimer->setTimerType(Qt::PreciseTimer);
    connect(m_renderTimer, &QTimer::timeout, this, &SceneManager::onRenderTick);
    
    // Initialize frame timer
    m_frameTimer.start();
    
    // Create default scene
    createScene(QStringLiteral("Scene 1"));
    if (!m_scenes.isEmpty()) {
        setActiveScene(m_scenes.first());
    }
    
    qDebug() << "SceneManager initialized";
}

SceneManager::~SceneManager() {
    stopRenderLoop();
    
    // Delete all scenes
    qDeleteAll(m_scenes);
    m_scenes.clear();
}

// ==============================================================================
// Output Configuration
// ==============================================================================
void SceneManager::setOutputResolution(const QSize& size) {
    if (m_outputResolution != size) {
        m_outputResolution = size;
        
        // Update all scenes
        QMutexLocker lock(&m_sceneMutex);
        for (Scene* scene : m_scenes) {
            scene->setResolution(size);
        }
        
        qDebug() << "Output resolution set to:" << size;
    }
}

void SceneManager::setTargetFps(double fps) {
    if (fps > 0 && fps <= 240) {
        m_targetFps = fps;
        m_stats.targetFps = fps;
        
        // Update timer interval if running
        if (m_renderLoopRunning) {
            int intervalMs = static_cast<int>(1000.0 / fps);
            m_renderTimer->setInterval(intervalMs);
        }
        
        qDebug() << "Target FPS set to:" << fps;
    }
}

void SceneManager::setPreviewCallback(PreviewFrameCallback callback) {
    QMutexLocker lock(&m_frameMutex);
    m_previewCallback = std::move(callback);
}

void SceneManager::setEncoderOutputEnabled(bool enabled) {
    m_encoderOutputEnabled = enabled;
}

// ==============================================================================
// Scene Management
// ==============================================================================
Scene* SceneManager::createScene(const QString& name) {
    QString sceneName = name;
    if (sceneName.isEmpty()) {
        sceneName = QString("Scene %1").arg(m_scenes.size() + 1);
    }
    
    Scene* scene = new Scene(sceneName, this);
    scene->setResolution(m_outputResolution);
    
    {
        QMutexLocker lock(&m_sceneMutex);
        m_scenes.append(scene);
    }
    
    emit sceneAdded(scene);
    
    qDebug() << "Scene created:" << sceneName;
    
    return scene;
}

bool SceneManager::removeScene(Scene* scene) {
    if (!scene) return false;
    
    QMutexLocker lock(&m_sceneMutex);
    
    int index = m_scenes.indexOf(scene);
    if (index < 0) return false;
    
    // Don't remove the last scene
    if (m_scenes.size() <= 1) {
        qWarning() << "Cannot remove the last scene";
        return false;
    }
    
    QUuid id = scene->id();
    m_scenes.removeAt(index);
    
    // If active scene was removed, switch to another
    if (m_activeScene == scene) {
        m_activeScene = m_scenes.isEmpty() ? nullptr : m_scenes.first();
        emit activeSceneChanged(m_activeScene);
    }
    
    lock.unlock();
    
    emit sceneRemoved(id);
    scene->deleteLater();
    
    qDebug() << "Scene removed";
    
    return true;
}

QList<Scene*> SceneManager::scenes() const {
    QMutexLocker lock(&m_sceneMutex);
    return m_scenes;
}

int SceneManager::sceneCount() const {
    QMutexLocker lock(&m_sceneMutex);
    return m_scenes.size();
}

void SceneManager::setActiveScene(Scene* scene) {
    if (m_activeScene != scene) {
        QMutexLocker lock(&m_sceneMutex);
        
        if (scene && !m_scenes.contains(scene)) {
            qWarning() << "Scene not in manager";
            return;
        }
        
        m_activeScene = scene;
        
        lock.unlock();
        
        emit activeSceneChanged(scene);
        
        qDebug() << "Active scene changed to:" 
                 << (scene ? scene->name() : "none");
    }
}

Scene* SceneManager::sceneByName(const QString& name) const {
    QMutexLocker lock(&m_sceneMutex);
    for (Scene* scene : m_scenes) {
        if (scene->name() == name) {
            return scene;
        }
    }
    return nullptr;
}

Scene* SceneManager::sceneById(const QUuid& id) const {
    QMutexLocker lock(&m_sceneMutex);
    for (Scene* scene : m_scenes) {
        if (scene->id() == id) {
            return scene;
        }
    }
    return nullptr;
}

// ==============================================================================
// Render Loop Control
// ==============================================================================
bool SceneManager::startRenderLoop() {
    if (m_renderLoopRunning) return true;
    
    int intervalMs = static_cast<int>(1000.0 / m_targetFps);
    m_renderTimer->setInterval(intervalMs);
    m_renderTimer->start();
    
    m_renderLoopRunning = true;
    m_frameTimer.restart();
    m_lastFrameTime = 0;
    
    // Reset statistics
    {
        QMutexLocker lock(&m_statsMutex);
        m_stats = RenderStatistics();
        m_stats.targetFps = m_targetFps;
        m_renderTimes.clear();
    }
    
    emit renderLoopStarted();
    
    qDebug() << "Render loop started at" << m_targetFps << "FPS";
    
    return true;
}

void SceneManager::stopRenderLoop() {
    if (!m_renderLoopRunning) return;
    
    m_renderTimer->stop();
    m_renderLoopRunning = false;
    
    emit renderLoopStopped();
    
    qDebug() << "Render loop stopped";
}

void SceneManager::onRenderTick() {
    doRender();
}

QImage SceneManager::renderFrame() {
    if (!m_activeScene) {
        // Return black frame
        QImage frame(m_outputResolution, QImage::Format_ARGB32_Premultiplied);
        frame.fill(Qt::black);
        return frame;
    }
    
    return m_activeScene->render();
}

QImage SceneManager::lastFrame() const {
    QMutexLocker lock(&m_frameMutex);
    return m_lastFrame;
}

RenderStatistics SceneManager::statistics() const {
    QMutexLocker lock(&m_statsMutex);
    return m_stats;
}

// ==============================================================================
// Render Implementation
// ==============================================================================
void SceneManager::doRender() {
    QElapsedTimer renderTimer;
    renderTimer.start();
    
    // Calculate time since last frame
    int64_t currentTime = m_frameTimer.elapsed();
    int64_t deltaTime = currentTime - m_lastFrameTime;
    m_lastFrameTime = currentTime;
    
    // Render the active scene
    QImage frame = renderFrame();
    
    // Store the frame
    {
        QMutexLocker lock(&m_frameMutex);
        m_lastFrame = frame;
    }
    
    // Output to preview
    outputToPreview(frame);
    
    // Output to encoder
    if (m_encoderOutputEnabled) {
        outputToEncoder(frame);
    }
    
    // Update statistics
    double renderTime = renderTimer.elapsed();
    {
        QMutexLocker lock(&m_statsMutex);
        m_stats.framesRendered++;
        
        // Calculate rolling average render time
        m_renderTimes.append(renderTime);
        if (m_renderTimes.size() > 60) {
            m_renderTimes.removeFirst();
        }
        
        double sum = 0;
        for (double t : m_renderTimes) sum += t;
        m_stats.averageRenderTimeMs = sum / m_renderTimes.size();
        
        // Calculate current FPS
        if (deltaTime > 0) {
            m_stats.currentFps = 1000.0 / deltaTime;
        }
    }
    
    emit frameRendered(m_stats.framesRendered);
}

void SceneManager::outputToEncoder(const QImage& frame) {
    if (frame.isNull()) return;
    
    // Get timestamp
    int64_t pts = m_frameTimer.elapsed() * 1000;  // Convert to microseconds
    
    // Push to encoder (thread-safe call)
    EncoderManager::instance().pushFrame(frame, pts);
}

void SceneManager::outputToPreview(const QImage& frame) {
    PreviewFrameCallback callback;
    {
        QMutexLocker lock(&m_frameMutex);
        callback = m_previewCallback;
    }
    
    if (callback && !frame.isNull()) {
        callback(frame);
    }
}

} // namespace WeaR
