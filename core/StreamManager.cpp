// ==============================================================================
// WeaR-studio StreamManager Implementation
// RTMP streaming output using FFmpeg libavformat
// ==============================================================================

#include "StreamManager.h"

#include <QDebug>
#include <QDateTime>
#include <QElapsedTimer>
#include <QThread>
#include <algorithm>
#include <atomic>
#include <limits>

// FFmpeg headers (C linkage)
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavutil/time.h>
#include <libavutil/mathematics.h>
}

#include <chrono>
#include <deque>

namespace {

struct InterruptState {
    const std::atomic<bool>* running = nullptr;
    std::chrono::steady_clock::time_point deadline =
        std::chrono::steady_clock::time_point::max();
};

int ffmpegInterruptCallback(void* opaque) {
    const auto* state = static_cast<const InterruptState*>(opaque);

    if (!state || !state->running) {
        return 1;
    }

    if (!state->running->load(std::memory_order_relaxed)) {
        return 1;
    }

    return std::chrono::steady_clock::now() >= state->deadline ? 1 : 0;
}

} // namespace

namespace WeaR {

// ==============================================================================
// Packet wrapper for queue
// ==============================================================================
struct QueuedPacket {
    AVPacket* packet = nullptr;
    bool isKeyframe = false;
    bool isAudio = false;
    
    QueuedPacket() = default;
    
    QueuedPacket(AVPacket* pkt, bool keyframe, bool audio = false) 
        : packet(pkt), isKeyframe(keyframe), isAudio(audio) {}
    
    // Move semantics
    QueuedPacket(QueuedPacket&& other) noexcept 
        : packet(other.packet), isKeyframe(other.isKeyframe), isAudio(other.isAudio) {
        other.packet = nullptr;
    }
    
    QueuedPacket& operator=(QueuedPacket&& other) noexcept {
        if (this != &other) {
            if (packet) av_packet_free(&packet);
            packet = other.packet;
            isKeyframe = other.isKeyframe;
            isAudio = other.isAudio;
            other.packet = nullptr;
        }
        return *this;
    }
    
    ~QueuedPacket() {
        if (packet) av_packet_free(&packet);
    }
    
    // No copy
    QueuedPacket(const QueuedPacket&) = delete;
    QueuedPacket& operator=(const QueuedPacket&) = delete;
};

// ==============================================================================
// Implementation class (PIMPL)
// ==============================================================================
class StreamManager::Impl {
public:
    Impl(StreamManager* parent) : m_parent(parent) {}
    
    ~Impl() {
        stop();
        cleanup();
        if (m_codecpar) {
            avcodec_parameters_free(&m_codecpar);
            m_codecpar = nullptr;
        }
        if (m_audioCodecpar) {
            avcodec_parameters_free(&m_audioCodecpar);
            m_audioCodecpar = nullptr;
        }
    }
    
    bool configure(const StreamSettings& settings) {
        QMutexLocker lock(&m_mutex);
        
        if (m_state == StreamState::Streaming ||
            m_state == StreamState::Connecting ||
            m_state == StreamState::Reconnecting) {
            qWarning() << "Cannot configure while streaming";
            return false;
        }
        
        m_settings = settings;
        
        // Apply service URL if using a preset
        if (settings.service != StreamService::Custom) {
            m_settings.url = StreamSettings::getServiceUrl(settings.service);
        }
        
        return true;
    }
    
    bool setCodecParameters(const AVCodecParameters* codecpar) {
        return setVideoCodecParameters(codecpar);
    }

    bool setVideoCodecParameters(const AVCodecParameters* codecpar) {
        QMutexLocker lock(&m_mutex);
        
        if (!codecpar) return false;
        
        // Copy codec parameters
        if (m_codecpar) {
            avcodec_parameters_free(&m_codecpar);
        }
        
        m_codecpar = avcodec_parameters_alloc();
        if (!m_codecpar) return false;
        
        int ret = avcodec_parameters_copy(m_codecpar, codecpar);
        if (ret < 0) {
            avcodec_parameters_free(&m_codecpar);
            return false;
        }
        
        qDebug() << "Video codec parameters set:"
                 << "codec_id=" << m_codecpar->codec_id
                 << "extradata_size=" << m_codecpar->extradata_size;
        
        return true;
    }

    bool setAudioCodecParameters(const AVCodecParameters* codecpar) {
        QMutexLocker lock(&m_mutex);
        
        if (!codecpar) return false;
        
        if (m_audioCodecpar) {
            avcodec_parameters_free(&m_audioCodecpar);
        }
        
        m_audioCodecpar = avcodec_parameters_alloc();
        if (!m_audioCodecpar) return false;
        
        int ret = avcodec_parameters_copy(m_audioCodecpar, codecpar);
        if (ret < 0) {
            avcodec_parameters_free(&m_audioCodecpar);
            return false;
        }
        
        qDebug() << "Audio codec parameters set:"
                 << "codec_id=" << m_audioCodecpar->codec_id
                 << "sample_rate=" << m_audioCodecpar->sample_rate
                 << "channels=" << m_audioCodecpar->ch_layout.nb_channels;
        
        return true;
    }
    
    bool start() {
        QMutexLocker lock(&m_mutex);
        
        if (m_state == StreamState::Streaming) return true;
        
        if (m_settings.url.isEmpty()) {
            qWarning() << "No stream URL configured";
            return false;
        }
        
        // Transition to connecting.
        setState(StreamState::Connecting);

        {
            QMutexLocker statsLock(&m_statsMutex);
            m_stats.reconnectAttempt = 0;
            m_stats.reconnectDelayMs = 0;
        }

        // Start output thread
        m_running = true;
        m_outputThread = std::thread(&Impl::outputLoop, this);
        
        return true;
    }
    
    bool start(const QString& url, const QString& streamKey) {
        m_settings.url = url;
        m_settings.streamKey = streamKey;
        return start();
    }
    
    void stop() {
        {
            QMutexLocker lock(&m_mutex);
            if (m_state == StreamState::Stopped) return;
            m_running = false;
        }
        
        // Wake both packet waiters and reconnect backoff waiters.
        m_queueCondition.wakeAll();
        m_reconnectCondition.wakeAll();
        
        // Wait for thread to finish
        if (m_outputThread.joinable()) {
            m_outputThread.join();
        }
        
        // Cleanup connection
        cleanup();
        
        setState(StreamState::Stopped);
        emit m_parent->disconnected("Stream stopped");
    }
    
    StreamState state() const {
        QMutexLocker lock(&m_mutex);
        return m_state;
    }
    
    bool isStreaming() const {
        return m_state == StreamState::Streaming;
    }
    
    bool isConnected() const {
        auto s = m_state.load();
        return s == StreamState::Streaming || 
               s == StreamState::Connecting ||
               s == StreamState::Reconnecting;
    }
    
    bool writePacket(const uint8_t* data, int size, 
                     int64_t pts, int64_t dts, bool isKeyframe, bool isAudio = false) {
        if (!m_running || m_state == StreamState::Stopped) return false;
        
        // Create AVPacket
        AVPacket* packet = av_packet_alloc();
        if (!packet) return false;
        
        int ret = av_new_packet(packet, size);
        if (ret < 0) {
            av_packet_free(&packet);
            return false;
        }
        
        memcpy(packet->data, data, size);
        packet->pts = pts;
        packet->dts = dts;
        packet->flags = isKeyframe ? AV_PKT_FLAG_KEY : 0;
        
        return queuePacket(packet, isKeyframe, isAudio);
    }
    
    bool writeAudioPacket(const uint8_t* data, int size, 
                          int64_t pts, int64_t dts) {
        return writePacket(data, size, pts, dts, true, true);
    }

    bool writePacket(const AVPacket* srcPacket, bool isAudio = false) {
        if (!m_running || m_state == StreamState::Stopped) return false;
        if (!srcPacket) return false;
        
        // Clone packet
        AVPacket* packet = av_packet_clone(srcPacket);
        if (!packet) return false;
        
        bool isKeyframe = (srcPacket->flags & AV_PKT_FLAG_KEY) != 0;
        return queuePacket(packet, isKeyframe, isAudio);
    }
    
    int queueSize() const {
        QMutexLocker lock(&m_queueMutex);
        return static_cast<int>(m_packetQueue.size());
    }
    
    StreamSettings settings() const {
        QMutexLocker lock(&m_mutex);
        return m_settings;
    }
    
    StreamStatistics statistics() const {
        QMutexLocker lock(&m_statsMutex);
        StreamStatistics stats = m_stats;
        stats.state = m_state;
        
        // Calculate stream duration
        if (m_streamStartTime > 0 && m_state == StreamState::Streaming) {
            stats.streamDurationMs = QDateTime::currentMSecsSinceEpoch() - m_streamStartTime;
        }
        
        // Calculate current bitrate
        if (stats.streamDurationMs > 0) {
            stats.currentBitrateKbps = 
                (stats.bytesWritten * 8.0) / stats.streamDurationMs;
        }
        
        return stats;
    }
    
    void resetStatistics() {
        QMutexLocker lock(&m_statsMutex);
        m_stats = StreamStatistics();
    }

private:
    bool initializeOutput() {
        QString url = m_settings.fullUrl();
        qDebug() << "Connecting to:" << url;
        
        // Allocate output context
        int ret = avformat_alloc_output_context2(
            &m_formatContext, nullptr, "flv", url.toUtf8().constData()
        );
        
        if (ret < 0 || !m_formatContext) {
            logAvError("Failed to allocate output context", ret);
            return false;
        }

        // Make all blocking FFmpeg I/O interruptible when StreamManager stops.
        m_interruptState.running = &m_running;
        m_interruptState.deadline =
            std::chrono::steady_clock::now() +
            std::chrono::seconds(
                std::max(1, m_settings.connectTimeout) + 1);

        m_formatContext->interrupt_callback.callback =
            &ffmpegInterruptCallback;
        m_formatContext->interrupt_callback.opaque =
            &m_interruptState;

        // Create video stream
        m_videoStream = avformat_new_stream(m_formatContext, nullptr);
        if (!m_videoStream) {
            qCritical() << "Failed to create video stream";
            return false;
        }
        
        m_videoStream->id = 0;
        m_videoStream->time_base = AVRational{1, 1000};  // FLV uses milliseconds
        
        // Copy codec parameters to stream
        if (m_codecpar) {
            ret = avcodec_parameters_copy(m_videoStream->codecpar, m_codecpar);
            if (ret < 0) {
                logAvError("Failed to copy codec parameters", ret);
                return false;
            }
        } else {
            // Set default parameters if none provided
            m_videoStream->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
            m_videoStream->codecpar->codec_id = AV_CODEC_ID_H264;
            m_videoStream->codecpar->width = m_settings.videoWidth;
            m_videoStream->codecpar->height = m_settings.videoHeight;
            m_videoStream->codecpar->bit_rate = m_settings.videoBitrate * 1000;
        }

        // Create audio stream
        if (m_settings.audioEnabled) {
            m_audioStream = avformat_new_stream(m_formatContext, nullptr);
            if (!m_audioStream) {
                qWarning() << "Failed to create audio stream";
            } else {
                m_audioStream->id = 1;
                m_audioStream->time_base = AVRational{1, 1000};  // FLV uses milliseconds
                
                if (m_audioCodecpar) {
                    ret = avcodec_parameters_copy(m_audioStream->codecpar, m_audioCodecpar);
                    if (ret < 0) {
                        logAvError("Failed to copy audio codec parameters", ret);
                    }
                } else {
                    m_audioStream->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
                    m_audioStream->codecpar->codec_id = AV_CODEC_ID_AAC;
                    m_audioStream->codecpar->sample_rate = m_settings.audioSampleRate;
                    av_channel_layout_default(&m_audioStream->codecpar->ch_layout, m_settings.audioChannels);
                    m_audioStream->codecpar->bit_rate = m_settings.audioBitrate * 1000;
                }
            }
        }
        
        // Set up RTMP connection options
        AVDictionary* options = nullptr;
        
        // Connection timeout
        const auto timeoutUs =
            QString::number(
                std::max(1, m_settings.connectTimeout) * 1000000);
        av_dict_set(&options, "timeout", timeoutUs.toUtf8().constData(), 0);

        // Bound generic FFmpeg read/write waits as well as the connect timeout.
        av_dict_set(&options, "rw_timeout",
                    timeoutUs.toUtf8().constData(), 0);

        // TCP buffer size
        QString bufSize = QString::number(m_settings.sendBufferSize);
        av_dict_set(&options, "buffer_size", bufSize.toUtf8().constData(), 0);
        
        // RTMP-specific options
        av_dict_set(&options, "rtmp_live", "live", 0);
        av_dict_set(&options, "rtmp_buffer", "1000", 0);  // 1 second buffer
        
        // Open output
        if (!(m_formatContext->oformat->flags & AVFMT_NOFILE)) {
            ret = avio_open2(
                &m_formatContext->pb, 
                url.toUtf8().constData(),
                AVIO_FLAG_WRITE,
                nullptr,
                &options
            );
            
            av_dict_free(&options);
            
            if (ret < 0) {
                logAvError("Failed to open output URL", ret);
                return false;
            }

            // The connection deadline only applies during initial network setup.
            // Keep the interrupt callback active for cancellation through stop(),
            // but do not let the connection deadline expire during normal streaming.
            m_interruptState.deadline =
                std::chrono::steady_clock::time_point::max();
        }
        
        // Write stream header
        ret = avformat_write_header(m_formatContext, nullptr);
        if (ret < 0) {
            logAvError("Failed to write header", ret);
            return false;
        }
        
        m_headerWritten = true;
        m_streamStartTime = QDateTime::currentMSecsSinceEpoch();
        
        qDebug() << "Connected to RTMP server successfully";
        return true;
    }
    
    void cleanup() {
        if (m_formatContext) {
            // Write trailer if header was written
            if (m_headerWritten) {
                av_write_trailer(m_formatContext);
            }
            
            // Close output
            if (m_formatContext->pb) {
                avio_closep(&m_formatContext->pb);
            }
            
            avformat_free_context(m_formatContext);
            m_formatContext = nullptr;
        }
        m_headerWritten = false;
        
        m_videoStream = nullptr;
        m_audioStream = nullptr;
        
        // Clear packet queue
        {
            QMutexLocker lock(&m_queueMutex);
            m_packetQueue.clear();
        }
    }
    
    bool queuePacket(AVPacket* packet, bool isKeyframe, bool isAudio = false) {
        QMutexLocker lock(&m_queueMutex);
        
        // Check queue size limit
        const int MAX_QUEUE_SIZE = 500;  // Support both video and audio
        if (m_packetQueue.size() >= MAX_QUEUE_SIZE) {
            av_packet_free(&packet);
            m_stats.droppedPackets++;
            qWarning() << "Stream queue full, dropping packet";
            return false;
        }
        
        m_packetQueue.emplace_back(packet, isKeyframe, isAudio);
        m_queueCondition.wakeOne();
        
        return true;
    }
    
    int calculateReconnectDelayMs(int attempt) const {
        const int baseSeconds = std::max(1, m_settings.reconnectDelay);
        const int maxSeconds = std::max(baseSeconds, m_settings.reconnectMaxDelay);

        qint64 delaySeconds = baseSeconds;
        for (int i = 1; i < attempt && delaySeconds < maxSeconds; ++i) {
            delaySeconds = std::min<qint64>(
                maxSeconds,
                delaySeconds * 2);
        }

        return static_cast<int>(std::min<qint64>(
            delaySeconds * 1000LL,
            std::numeric_limits<int>::max()));
    }

    bool waitForReconnectBackoff(int attempt) {
        const int delayMs = calculateReconnectDelayMs(attempt);

        {
            QMutexLocker lock(&m_statsMutex);
            m_stats.reconnectAttempt = attempt;
            m_stats.reconnectDelayMs = delayMs;
        }

        qInfo() << "RTMP reconnect scheduled:"
                << "attempt=" << attempt
                << "delay_ms=" << delayMs;

        emit m_parent->reconnecting(attempt);

        QMutexLocker lock(&m_reconnectMutex);
        if (!m_running) {
            return false;
        }

        m_reconnectCondition.wait(&m_reconnectMutex, delayMs);
        return m_running;
    }

    void outputLoop() {
        qDebug() << "Stream output thread started";

        int reconnectAttempts = 0;

        while (m_running) {
            if (m_state == StreamState::Connecting ||
                m_state == StreamState::Reconnecting) {

                if (initializeOutput()) {
                    setState(StreamState::Streaming);
                    emit m_parent->connected();
                    reconnectAttempts = 0;

                    QMutexLocker statsLock(&m_statsMutex);
                    m_stats.reconnectAttempt = 0;
                    m_stats.reconnectDelayMs = 0;
                } else {
                    // initializeOutput can fail after partially allocating an
                    // AVFormatContext, so always clean it before the next try.
                    cleanup();

                    ++reconnectAttempts;

                    {
                        QMutexLocker statsLock(&m_statsMutex);
                        ++m_stats.reconnectCount;
                        m_stats.reconnectAttempt = reconnectAttempts;
                    }

                    if (m_settings.maxReconnectAttempts > 0 &&
                        reconnectAttempts >= m_settings.maxReconnectAttempts) {
                        qCritical()
                            << "Max RTMP reconnection attempts reached:"
                            << reconnectAttempts;
                        setState(StreamState::Error);
                        emit m_parent->streamError(
                            "Max reconnection attempts reached");
                        break;
                    }

                    setState(StreamState::Reconnecting);
                    if (!waitForReconnectBackoff(reconnectAttempts)) {
                        break;
                    }
                    continue;
                }
            }

            QueuedPacket queuedPacket;

            {
                QMutexLocker lock(&m_queueMutex);

                if (m_packetQueue.empty()) {
                    m_queueCondition.wait(&m_queueMutex, 100);
                    continue;
                }

                queuedPacket = std::move(m_packetQueue.front());
                m_packetQueue.pop_front();
            }

            if (!queuedPacket.packet) {
                continue;
            }

            if (!sendPacket(
                    queuedPacket.packet,
                    queuedPacket.isKeyframe,
                    queuedPacket.isAudio)) {
                qWarning() << "RTMP packet send failed; reconnecting.";
                cleanup();
                emit m_parent->disconnected(
                    "RTMP connection lost; reconnecting.");
                setState(StreamState::Reconnecting);

                {
                    QMutexLocker statsLock(&m_statsMutex);
                    ++m_stats.reconnectCount;
                }

                ++reconnectAttempts;
                if (m_settings.maxReconnectAttempts > 0 &&
                    reconnectAttempts >= m_settings.maxReconnectAttempts) {
                    qCritical()
                        << "Max RTMP reconnection attempts reached:"
                        << reconnectAttempts;
                    setState(StreamState::Error);
                    emit m_parent->streamError(
                        "Max reconnection attempts reached");
                    break;
                }

                if (!waitForReconnectBackoff(reconnectAttempts)) {
                    break;
                }
            }
        }

        qDebug() << "Stream output thread stopped";
    }
    
    bool sendPacket(AVPacket* packet, bool isKeyframe, bool isAudio) {
        if (!m_formatContext || !m_headerWritten) {
            return false;
        }
        
        if (isAudio) {
            if (!m_audioStream) return false;
            
            AVRational audioEncoderTimebase = {1, m_settings.audioSampleRate};
            av_packet_rescale_ts(packet, audioEncoderTimebase, m_audioStream->time_base);
            packet->stream_index = m_audioStream->index;
        } else {
            if (!m_videoStream) return false;
            
            AVRational encoderTimebase = {1, 1000000};
            av_packet_rescale_ts(packet, encoderTimebase, m_videoStream->time_base);
            packet->stream_index = m_videoStream->index;
            
            // Set duration if not set
            if (packet->duration <= 0) {
                packet->duration = av_rescale_q(
                    1, 
                    AVRational{m_settings.videoFpsDen, m_settings.videoFpsNum},
                    m_videoStream->time_base
                );
            }
        }
        
        // Write packet
        QElapsedTimer timer;
        timer.start();
        
        int ret = av_interleaved_write_frame(m_formatContext, packet);
        
        if (ret < 0) {
            logAvError("Failed to write frame", ret);
            return false;
        }
        
        // Update statistics
        {
            QMutexLocker lock(&m_statsMutex);
            m_stats.bytesWritten += packet->size;
            m_stats.packetsWritten++;
            if (!isAudio && isKeyframe) {
                m_stats.keyframesSent++;
            }
            
            // Update latency average
            double latency = timer.elapsed();
            m_latencyHistory.push_back(latency);
            if (m_latencyHistory.size() > 60) {
                m_latencyHistory.pop_front();
            }
            
            double sum = 0;
            for (double l : m_latencyHistory) sum += l;
            m_stats.averageLatencyMs = sum / m_latencyHistory.size();
        }
        
        emit m_parent->packetSent(packet->pts, packet->size);
        
        return true;
    }
    
    void setState(StreamState newState) {
        StreamState oldState = m_state.exchange(newState);
        if (oldState != newState) {
            emit m_parent->stateChanged(newState);
        }
    }
    
    void logAvError(const char* message, int errnum) {
        char errbuf[256];
        av_strerror(errnum, errbuf, sizeof(errbuf));
        qCritical() << message << ":" << errbuf;
        emit m_parent->streamError(QString("%1: %2").arg(message, errbuf));
    }
    
    // Parent reference
    StreamManager* m_parent;
    
    // Thread safety
    mutable QMutex m_mutex;
    mutable QMutex m_queueMutex;
    mutable QMutex m_statsMutex;
    mutable QMutex m_reconnectMutex;
    QWaitCondition m_queueCondition;
    QWaitCondition m_reconnectCondition;
    
    // State
    std::atomic<StreamState> m_state{StreamState::Stopped};
    std::atomic<bool> m_running{false};
    std::thread m_outputThread;

    InterruptState m_interruptState;
    
    // Settings
    StreamSettings m_settings;
    
    // FFmpeg objects
    AVFormatContext* m_formatContext = nullptr;
    AVStream* m_videoStream = nullptr;
    AVStream* m_audioStream = nullptr;
    AVCodecParameters* m_codecpar = nullptr;
    AVCodecParameters* m_audioCodecpar = nullptr;
    
    // Flags
    bool m_headerWritten = false;
    int64_t m_streamStartTime = 0;
    
    // Packet queue
    std::deque<QueuedPacket> m_packetQueue;
    
    // Statistics
    StreamStatistics m_stats;
    std::deque<double> m_latencyHistory;
};

// ==============================================================================
// StreamManager Singleton
// ==============================================================================
StreamManager& StreamManager::instance() {
    static StreamManager instance;
    return instance;
}

StreamManager::StreamManager(QObject* parent)
    : QObject(parent)
    , m_impl(std::make_unique<Impl>(this))
{
}

StreamManager::~StreamManager() = default;

bool StreamManager::configure(const StreamSettings& settings) {
    return m_impl->configure(settings);
}

StreamSettings StreamManager::settings() const {
    return m_impl->settings();
}

bool StreamManager::setCodecParameters(const AVCodecParameters* codecpar) {
    return m_impl->setCodecParameters(codecpar);
}

bool StreamManager::setVideoCodecParameters(const AVCodecParameters* codecpar) {
    return m_impl->setVideoCodecParameters(codecpar);
}

bool StreamManager::setAudioCodecParameters(const AVCodecParameters* codecpar) {
    return m_impl->setAudioCodecParameters(codecpar);
}

bool StreamManager::startStream() {
    return m_impl->start();
}

bool StreamManager::startStream(const QString& url, const QString& streamKey) {
    return m_impl->start(url, streamKey);
}

void StreamManager::stopStream() {
    m_impl->stop();
}

StreamState StreamManager::state() const {
    return m_impl->state();
}

bool StreamManager::isStreaming() const {
    return m_impl->isStreaming();
}

bool StreamManager::isConnected() const {
    return m_impl->isConnected();
}

bool StreamManager::writePacket(const uint8_t* data, int size, 
                                 int64_t pts, int64_t dts, bool isKeyframe, bool isAudio) {
    return m_impl->writePacket(data, size, pts, dts, isKeyframe, isAudio);
}

bool StreamManager::writeAudioPacket(const uint8_t* data, int size, 
                                      int64_t pts, int64_t dts) {
    return m_impl->writeAudioPacket(data, size, pts, dts);
}

bool StreamManager::writePacket(const AVPacket* packet, bool isAudio) {
    return m_impl->writePacket(packet, isAudio);
}

int StreamManager::queueSize() const {
    return m_impl->queueSize();
}

StreamStatistics StreamManager::statistics() const {
    return m_impl->statistics();
}

void StreamManager::resetStatistics() {
    m_impl->resetStatistics();
}

} // namespace WeaR
