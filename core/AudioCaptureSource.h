#pragma once
// ==============================================================================
// WeaR-studio AudioCaptureSource
// WASAPI-based Desktop Audio (Loopback) and Microphone Capture
// ==============================================================================

#include "ISource.h"

#include <QObject>
#include <QMutex>
#include <QString>
#include <vector>
#include <deque>
#include <memory>
#include <atomic>
#include <thread>

namespace WeaR {

enum class AudioDeviceType {
    DesktopLoopback,  ///< System playback audio (what you hear)
    Microphone        ///< Audio input device (microphone)
};

/**
 * @brief WASAPI-based audio source implementation
 */
class AudioCaptureSource : public QObject, public ISource {
    Q_OBJECT
    Q_INTERFACES(WeaR::ISource)

public:
    explicit AudioCaptureSource(AudioDeviceType deviceType, QObject* parent = nullptr);
    ~AudioCaptureSource() override;

    // Prevent copying
    AudioCaptureSource(const AudioCaptureSource&) = delete;
    AudioCaptureSource& operator=(const AudioCaptureSource&) = delete;

    // =========================================================================
    // IPlugin Interface
    // =========================================================================
    [[nodiscard]] PluginInfo info() const override;
    [[nodiscard]] QString name() const override;
    [[nodiscard]] QString version() const override { return QStringLiteral("1.0.0"); }
    [[nodiscard]] PluginType type() const override { return PluginType::Source; }
    [[nodiscard]] PluginCapability capabilities() const override {
        return PluginCapability::HasAudio | PluginCapability::ThreadSafe;
    }

    bool initialize() override;
    void shutdown() override;
    [[nodiscard]] bool isActive() const override;

    // =========================================================================
    // ISource Interface
    // =========================================================================
    bool configure(const SourceConfig& config) override;
    [[nodiscard]] SourceConfig config() const override;

    bool start() override;
    void stop() override;
    [[nodiscard]] bool isRunning() const override;

    [[nodiscard]] VideoFrame captureVideoFrame() override { return VideoFrame(); }
    [[nodiscard]] AudioFrame captureAudioFrame() override;

    [[nodiscard]] QSize nativeResolution() const override { return QSize(); }
    [[nodiscard]] double nativeFps() const override { return 0.0; }
    [[nodiscard]] QSize outputResolution() const override { return QSize(); }
    [[nodiscard]] double outputFps() const override { return 0.0; }

    [[nodiscard]] AudioDeviceType deviceType() const { return m_deviceType; }

    /**
     * @brief Set how many samples captureAudioFrame() should pull (per channel)
     */
    void setSamplesPerFrame(int samples) { m_samplesPerFrame = samples; }
    [[nodiscard]] int samplesPerFrame() const { return m_samplesPerFrame; }

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;

    AudioDeviceType m_deviceType;
    SourceConfig m_config;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_initialized{false};
    int m_samplesPerFrame = 800; // 800 samples @ 48kHz = 16.66ms (60 FPS tick)
    mutable QMutex m_mutex;
};

/**
 * @brief Desktop audio loopback source singleton
 */
class DesktopAudioSource : public AudioCaptureSource {
    Q_OBJECT

public:
    static DesktopAudioSource& instance();
    explicit DesktopAudioSource(QObject* parent = nullptr);
};

/**
 * @brief Microphone audio capture source singleton
 */
class MicrophoneAudioSource : public AudioCaptureSource {
    Q_OBJECT

public:
    static MicrophoneAudioSource& instance();
    explicit MicrophoneAudioSource(QObject* parent = nullptr);
};

} // namespace WeaR
