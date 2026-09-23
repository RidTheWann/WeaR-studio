#pragma once
// ==============================================================================
// WeaR-studio RecordingManager
// Independent local recording output using FFmpeg
// ==============================================================================

#include "EncoderManager.h"

#include <QObject>
#include <QString>
#include <QImage>

#include <memory>
#include <atomic>
#include <cstdint>

namespace WeaR {

/**
 * @brief Local recording container format.
 */
enum class RecordingFormat {
    MP4,
    MKV,
    FLV
};

/**
 * @brief Recording lifecycle state.
 */
enum class RecordingState {
    Stopped,
    Recording,
    Paused,
    Error
};

/**
 * @brief Recording-specific video/audio quality settings.
 *
 * These settings are intentionally independent from StreamSettings and
 * EncoderManager::EncoderSettings so local recording quality can differ from
 * the live stream.
 */
struct RecordingSettings {
    QString outputPath;

    RecordingFormat format = RecordingFormat::MKV;

    int width = 1920;
    int height = 1080;
    int fpsNum = 60;
    int fpsDen = 1;

    int videoBitrate = 12000;  ///< kbps
    int maxVideoBitrate = 16000;
    int bufferSize = 24000;
    int crf = 18;
    int qp = 18;

    EncoderType encoderType = EncoderType::Auto;
    EncoderPreset preset = EncoderPreset::Fast;
    RateControlMode rateControl = RateControlMode::CRF;

    int keyframeInterval = 2;
    int bFrames = 2;
    int threads = 0;

    bool audioEnabled = true;
    int audioSampleRate = 48000;
    int audioChannels = 2;
    int audioBitrate = 192;  ///< kbps
};

/**
 * @brief Independent recording pipeline.
 *
 * RecordingManager consumes the same composed video/audio render ticks as the
 * streaming pipeline, but encodes and muxes them in its own worker thread and
 * its own FFmpeg contexts. Starting/stopping/pausing recording therefore does
 * not start/stop/reconfigure StreamManager or the streaming encoder.
 */
class RecordingManager : public QObject {
    Q_OBJECT

public:
    static RecordingManager& instance();

    RecordingManager(const RecordingManager&) = delete;
    RecordingManager& operator=(const RecordingManager&) = delete;

    ~RecordingManager() override;

    bool configure(const RecordingSettings& settings);
    [[nodiscard]] RecordingSettings settings() const;

    bool startRecording(const QString& outputPath = QString());
    bool stopRecording();
    bool pauseRecording();
    bool resumeRecording();

    [[nodiscard]] RecordingState state() const;
    [[nodiscard]] bool isRecording() const;
    [[nodiscard]] bool isPaused() const;

    /**
     * @brief Active recording duration in milliseconds. Paused time is excluded.
     */
    [[nodiscard]] qint64 durationMs() const;

    [[nodiscard]] QString outputPath() const;

    /**
     * @brief Submit one composed video frame.
     *
     * This is non-blocking in the normal path. The frame is queued for the
     * dedicated recording worker.
     */
    void pushFrame(const QImage& frame);

    /**
     * @brief Submit one mixed audio render tick.
     */
    void pushAudioFrame(const AudioFrame& frame);

signals:
    void stateChanged(WeaR::RecordingState state);
    void recordingStarted(const QString& path);
    void recordingStopped(const QString& path);
    void recordingError(const QString& error);

private:
    explicit RecordingManager(QObject* parent = nullptr);

    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace WeaR
