// ==============================================================================
// WeaR-studio SceneManager Implementation
// Manages scenes and runs the render loop for video composition
// ==============================================================================

#include "SceneManager.h"
#include "EncoderManager.h"
#include "RecordingManager.h"
#include "AudioMixer.h"
#include "RhiCompositor.h"
#include "VirtualCameraManager.h"
#include "SceneTransition.h"

#include <QDebug>
#include <QDateTime>
#include <QPainter>
#include <algorithm>

#ifdef Q_OS_WIN
#  include <windows.h>
#elif defined(Q_OS_UNIX)
#  include <time.h>
#endif


namespace {

qint64 currentThreadCpuTimeNsecs() {
#ifdef Q_OS_WIN
    FILETIME creationTime{};
    FILETIME exitTime{};
    FILETIME kernelTime{};
    FILETIME userTime{};
    if (!GetThreadTimes(
            GetCurrentThread(),
            &creationTime,
            &exitTime,
            &kernelTime,
            &userTime)) {
        return -1;
    }

    ULARGE_INTEGER kernel{};
    kernel.LowPart = kernelTime.dwLowDateTime;
    kernel.HighPart = kernelTime.dwHighDateTime;

    ULARGE_INTEGER user{};
    user.LowPart = userTime.dwLowDateTime;
    user.HighPart = userTime.dwHighDateTime;

    // FILETIME uses 100 ns units.
    return static_cast<qint64>((kernel.QuadPart + user.QuadPart) * 100ULL);
#elif defined(Q_OS_UNIX)
    timespec ts{};
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts) != 0) {
        return -1;
    }

    return static_cast<qint64>(ts.tv_sec) * 1000000000LL +
           static_cast<qint64>(ts.tv_nsec);
#else
    return -1;
#endif
}

QString compositorModeFromEnvironment() {
    return qEnvironmentVariable(
        "WEAR_COMPOSITOR",
        QStringLiteral("auto")).trimmed().toLower();
}

} // namespace

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

    // RHI is initialized lazily on the render thread. This avoids making
    // application startup dependent on a particular graphics backend.
    m_rhiCompositor = std::make_unique<RhiCompositor>();
    
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

void SceneManager::setRecordingOutputEnabled(bool enabled) {
    m_recordingOutputEnabled = enabled;
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
        cancelSceneTransition();
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

void SceneManager::setTransitionType(SceneTransitionType type) {
    m_transitionType = type;
}

void SceneManager::setTransitionDuration(SceneTransitionType type, int durationMs) {
    const int index = static_cast<int>(type);
    if (index < 0 || index >= static_cast<int>(m_transitionDurationsMs.size())) {
        return;
    }

    m_transitionDurationsMs[static_cast<size_t>(index)] =
        type == SceneTransitionType::Cut
            ? 0
            : std::clamp(durationMs, 0, 10000);
}

int SceneManager::transitionDuration(SceneTransitionType type) const {
    const int index = static_cast<int>(type);
    if (index < 0 || index >= static_cast<int>(m_transitionDurationsMs.size())) {
        return 0;
    }
    return m_transitionDurationsMs[static_cast<size_t>(index)];
}

double SceneManager::transitionProgress() const {
    if (!m_transition.active || m_transition.durationMs <= 0) {
        return m_transition.active ? 1.0 : 0.0;
    }

    return std::clamp(
        static_cast<double>(m_transition.clock.elapsed()) /
            static_cast<double>(m_transition.durationMs),
        0.0,
        1.0);
}

void SceneManager::cancelSceneTransition() {
    m_transition.active = false;
    m_transition.durationMs = 0;
    m_transition.fromFrame = QImage();
    m_transition.targetScene = nullptr;
}

void SceneManager::setActiveScene(Scene* scene) {
    if (m_activeScene == scene) {
        return;
    }

    {
        QMutexLocker lock(&m_sceneMutex);
        if (scene && !m_scenes.contains(scene)) {
            qWarning() << "Scene not in manager";
            return;
        }
    }

    Scene* previousScene = m_activeScene;
    const SceneTransitionType requestedType = m_transitionType;
    const int requestedDuration = transitionDuration(requestedType);

    if (previousScene &&
        scene &&
        SceneTransition::isAnimated(requestedType) &&
        requestedDuration > 0) {
        QImage outgoingFrame;
        {
            QMutexLocker lock(&m_frameMutex);
            outgoingFrame = m_lastFrame.copy();
        }

        if (outgoingFrame.isNull()) {
            outgoingFrame = previousScene->render();
        }

        if (!outgoingFrame.isNull()) {
            m_transition.active = true;
            m_transition.type = requestedType;
            m_transition.durationMs = requestedDuration;
            m_transition.fromFrame = std::move(outgoingFrame);
            m_transition.targetScene = scene;
            m_transition.clock.restart();
        } else {
            cancelSceneTransition();
        }
    } else {
        cancelSceneTransition();
    }

    m_activeScene = scene;
    emit activeSceneChanged(scene);

    qDebug() << "Active scene changed to:"
             << (scene ? scene->name() : "none")
             << "transition=" << SceneTransition::typeName(requestedType)
             << "durationMs=" << requestedDuration;
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
        m_compositingWallTimes.clear();
        m_compositingCpuTimes.clear();
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
    QElapsedTimer compositingTimer;
    compositingTimer.start();
    const qint64 cpuStart = currentThreadCpuTimeNsecs();

    QImage frame;
    QString backendName = QStringLiteral("QPainter");

    if (!m_activeScene) {
        frame = QImage(
            m_outputResolution,
            QImage::Format_ARGB32_Premultiplied);
        frame.fill(Qt::black);
    } else {
        const QString mode = compositorModeFromEnvironment();
        const bool forceQPainter = mode == QStringLiteral("qpainter");
        bool usedRhi = false;

        if (!forceQPainter && m_rhiCompositor) {
            QImage rhiFrame;
            if (m_rhiCompositor->compose(
                    *m_activeScene,
                    m_outputResolution,
                    rhiFrame)) {
                frame = std::move(rhiFrame);
                backendName = QStringLiteral("RHI/%1")
                    .arg(m_rhiCompositor->backendName());
                usedRhi = true;
                m_loggedRhiFallback = false;
            } else if (!m_loggedRhiFallback) {
                qWarning() << "RHI compositor unavailable; falling back to QPainter:"
                           << m_rhiCompositor->lastError();
                m_loggedRhiFallback = true;
            }
        }

        if (!usedRhi) {
            frame = renderFrameQPainter();
        }

        frame = applySceneTransition(frame);
    }

    const double wallTimeMs = compositingTimer.nsecsElapsed() / 1000000.0;
    const qint64 cpuEnd = currentThreadCpuTimeNsecs();
    const double cpuTimeMs =
        cpuStart >= 0 && cpuEnd >= cpuStart
            ? static_cast<double>(cpuEnd - cpuStart) / 1000000.0
            : wallTimeMs;

    updateCompositingStats(wallTimeMs, cpuTimeMs, backendName);
    return frame;
}

QImage SceneManager::applySceneTransition(const QImage& incomingFrame) {
    if (!m_transition.active || incomingFrame.isNull()) {
        return incomingFrame;
    }

    const double progress = transitionProgress();
    if (progress >= 1.0) {
        cancelSceneTransition();
        return incomingFrame;
    }

    return SceneTransition::compose(
        m_transition.fromFrame,
        incomingFrame,
        m_transition.type,
        progress);
}

QImage SceneManager::renderFrameQPainter() {
    if (!m_activeScene) {
        QImage frame(
            m_outputResolution,
            QImage::Format_ARGB32_Premultiplied);
        frame.fill(Qt::black);
        return frame;
    }

    return m_activeScene->render();
}

void SceneManager::updateCompositingStats(
    double wallTimeMs,
    double cpuTimeMs,
    const QString& backendName) {
    QMutexLocker lock(&m_statsMutex);

    m_compositingWallTimes.append(wallTimeMs);
    m_compositingCpuTimes.append(cpuTimeMs);

    constexpr int kWindow = 60;
    while (m_compositingWallTimes.size() > kWindow) {
        m_compositingWallTimes.removeFirst();
    }
    while (m_compositingCpuTimes.size() > kWindow) {
        m_compositingCpuTimes.removeFirst();
    }

    double wallSum = 0.0;
    for (double value : m_compositingWallTimes) {
        wallSum += value;
    }

    double cpuSum = 0.0;
    for (double value : m_compositingCpuTimes) {
        cpuSum += value;
    }

    m_stats.compositingWallTimeMs =
        m_compositingWallTimes.isEmpty()
            ? 0.0
            : wallSum / m_compositingWallTimes.size();

    m_stats.compositingCpuTimeMs =
        m_compositingCpuTimes.isEmpty()
            ? 0.0
            : cpuSum / m_compositingCpuTimes.size();

    m_stats.compositingCpuUsagePercent =
        wallSum > 0.0
            ? (cpuSum / wallSum) * 100.0
            : 0.0;

    m_stats.compositingBackend = backendName;
    if (backendName == QStringLiteral("QPainter")) {
        ++m_stats.qPainterFrames;
    } else {
        ++m_stats.rhiFrames;
        m_stats.rhiBackend = backendName;
    }
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
    
    const bool streamEnabled = m_encoderOutputEnabled.load();
    const bool recordingEnabled = m_recordingOutputEnabled.load();

    // Mix audio once per render tick and fan it out to the independent
    // streaming and recording pipelines.
    if (streamEnabled || recordingEnabled) {
        const int sampleRate = 48000;
        const int samplesPerFrame = static_cast<int>(
            sampleRate / (m_targetFps > 0.0 ? m_targetFps : 60.0));
        AudioFrame mixedAudio = AudioMixer::instance().mixTracks(samplesPerFrame);

        if (streamEnabled) {
            outputToEncoder(frame);
            if (!mixedAudio.samples.empty()) {
                EncoderManager::instance().pushAudioFrame(mixedAudio);
            }
        }

        if (recordingEnabled) {
            outputToRecorder(frame);
            if (!mixedAudio.samples.empty()) {
                RecordingManager::instance().pushAudioFrame(mixedAudio);
            }
        }
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

void SceneManager::outputToRecorder(const QImage& frame) {
    if (frame.isNull()) return;
    RecordingManager::instance().pushFrame(frame);
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

// Recording output is intentionally separate from the streaming encoder.