// ==============================================================================
// WeaR-studio StreamManager Implementation
// RTMP streaming output using FFmpeg libavformat
// ==============================================================================

#include "StreamManager.h"

#include <QDebug>
#include <QDateTime>
#include <QElapsedTimer>

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

namespace WeaR {

// ==============================================================================
// Packet wrapper for queue
// ==============================================================================
struct QueuedPacket {
    AVPacket* packet = nullptr;
    bool isKeyframe = false;
    
    QueuedPacket() = default;
    
    QueuedPacket(AVPacket* pkt, bool keyframe) 
        : packet(pkt), isKeyframe(keyframe) {}
    
    // Move semantics
    QueuedPacket(QueuedPacket&& other) noexcept 
        : packet(other.packet), isKeyframe(other.isKeyframe) {
        other.packet = nullptr;
    }
    
    QueuedPacket& operator=(QueuedPacket&& other) noexcept {
        if (this != &other) {
            if (packet) av_packet_free(&packet);
            packet = other.packet;
            isKeyframe = other.isKeyframe;
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
    }
    
    bool configure(const StreamSettings& settings) {
        QMutexLocker lock(&m_mutex);
        
        if (m_state == StreamState::Streaming || 
            m_state == StreamState::Connecting) {
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
        
        qDebug() << "Codec parameters set:"
                 << "codec_id=" << m_codecpar->codec_id
                 << "extradata_size=" << m_codecpar->extradata_size;
        
        return true;
    }
    
    bool start() {
        QMutexLocker lock(&m_mutex);
        
        if (m_state == StreamState::Streaming) return true;
        
        if (m_settings.url.isEmpty()) {
            qWarning() << "No stream URL configured";
            return false;
        }
        
        // Transition to connecting
        setState(StreamState::Connecting);
        
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
        
        // Wake up output thread
        m_queueCondition.wakeAll();
        
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
                     int64_t pts, int64_t dts, bool isKeyframe) {
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
        
        return queuePacket(packet, isKeyframe);
    }
    
    bool writePacket(const AVPacket* srcPacket) {
        if (!m_running || m_state == StreamState::Stopped) return false;
        if (!srcPacket) return false;
        
        // Clone packet
        AVPacket* packet = av_packet_clone(srcPacket);
        if (!packet) return false;
        
        bool isKeyframe = (srcPacket->flags & AV_PKT_FLAG_KEY) != 0;
        return queuePacket(packet, isKeyframe);
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
        
        // Set up RTMP connection options
        AVDictionary* options = nullptr;
        
        // Connection timeout
        QString timeout = QString::number(m_settings.connectTimeout * 1000000);
        av_dict_set(&options, "timeout", timeout.toUtf8().constData(), 0);
        
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
        m_headerWritten = false;
        
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
        
        m_videoStream = nullptr;
        
        // Clear packet queue
        {
            QMutexLocker lock(&m_queueMutex);
            m_packetQueue.clear();
        }
    }
    
    bool queuePacket(AVPacket* packet, bool isKeyframe) {
        QMutexLocker lock(&m_queueMutex);
        
        // Check queue size limit
        const int MAX_QUEUE_SIZE = 300;  // ~5 seconds at 60fps
        if (m_packetQueue.size() >= MAX_QUEUE_SIZE) {
            av_packet_free(&packet);
            m_stats.droppedPackets++;
            qWarning() << "Stream queue full, dropping packet";
            return false;
        }
        
        m_packetQueue.emplace_back(packet, isKeyframe);
        m_queueCondition.wakeOne();
        
        return true;
    }
    
    void outputLoop() {
        qDebug() << "Stream output thread started";
        
        int reconnectAttempts = 0;
        
        while (m_running) {
            // Try to connect if not connected
            if (m_state == StreamState::Connecting || 
                m_state == StreamState::Reconnecting) {
                
                if (initializeOutput()) {
                    setState(StreamState::Streaming);
                    emit m_parent->connected();
                    reconnectAttempts = 0;
                } else {
                    // Connection failed
                    reconnectAttempts++;
                    
                    if (m_settings.maxReconnectAttempts > 0 &&
                        reconnectAttempts >= m_settings.maxReconnectAttempts) {
                        qCritical() << "Max reconnection attempts reached";
                        setState(StreamState::Error);
                        emit m_parent->streamError("Max reconnection attempts reached");
                        break;
                    }
                    
                    setState(StreamState::Reconnecting);
                    emit m_parent->reconnecting(reconnectAttempts);
                    
                    // Wait before retrying
                    QThread::sleep(m_settings.reconnectDelay);
                    continue;
                }
            }
            
            // Process packets
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
            
            if (!queuedPacket.packet) continue;
            
            // Send packet
            if (!sendPacket(queuedPacket.packet, queuedPacket.isKeyframe)) {
                // Send failed - attempt reconnection
                qWarning() << "Send failed, attempting reconnection...";
                cleanup();
                setState(StreamState::Reconnecting);
                
                QMutexLocker lock(&m_statsMutex);
                m_stats.reconnectCount++;
            }
        }
        
        qDebug() << "Stream output thread stopped";
    }
    
    bool sendPacket(AVPacket* packet, bool isKeyframe) {
        if (!m_formatContext || !m_videoStream || !m_headerWritten) {
            return false;
        }
        
        // CRITICAL: Rescale timestamps from encoder timebase to stream timebase
        // Encoder typically uses {1, fps} or {1, 1000000} timebase
        // FLV/RTMP uses {1, 1000} (milliseconds)
        
        // Assume encoder timebase is {1, 1000000} (microseconds) if not set
        AVRational encoderTimebase = {1, 1000000};
        
        av_packet_rescale_ts(packet, encoderTimebase, m_videoStream->time_base);
        
        // Set stream index
        packet->stream_index = m_videoStream->index;
        
        // Set duration if not set
        if (packet->duration <= 0) {
            // Calculate from FPS
            packet->duration = av_rescale_q(
                1, 
                AVRational{m_settings.videoFpsDen, m_settings.videoFpsNum},
                m_videoStream->time_base
            );
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
            if (isKeyframe) {
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
    QWaitCondition m_queueCondition;
    
    // State
    std::atomic<StreamState> m_state{StreamState::Stopped};
    std::atomic<bool> m_running{false};
    std::thread m_outputThread;
    
    // Settings
    StreamSettings m_settings;
    
    // FFmpeg objects
    AVFormatContext* m_formatContext = nullptr;
    AVStream* m_videoStream = nullptr;
    AVCodecParameters* m_codecpar = nullptr;
    
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
                                 int64_t pts, int64_t dts, bool isKeyframe) {
    return m_impl->writePacket(data, size, pts, dts, isKeyframe);
}

bool StreamManager::writePacket(const AVPacket* packet) {
    return m_impl->writePacket(packet);
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
