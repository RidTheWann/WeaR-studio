#pragma once
// ==============================================================================
// WeaR-studio StreamManager
// RTMP streaming output using FFmpeg libavformat
// ==============================================================================

#include <QObject>
#include <QMutex>
#include <QQueue>
#include <QWaitCondition>
#include <QTimer>

#include <memory>
#include <atomic>
#include <thread>
#include <functional>

// Forward declarations for FFmpeg types
struct AVFormatContext;
struct AVStream;
struct AVCodecParameters;
struct AVPacket;
struct AVIOContext;

namespace WeaR {

/**
 * @brief Stream connection state
 */
enum class StreamState {
    Stopped,        ///< Not streaming
    Connecting,     ///< Establishing connection
    Streaming,      ///< Actively streaming
    Reconnecting,   ///< Connection lost, attempting to reconnect
    Error           ///< Unrecoverable error
};

/**
 * @brief Streaming service presets
 */
enum class StreamService {
    Custom,         ///< Custom RTMP URL
    Twitch,         ///< Twitch.tv
    YouTube,        ///< YouTube Live
    Facebook,       ///< Facebook Live
    Kick,           ///< Kick.com
    TikTok          ///< TikTok Live
};

/**
 * @brief Stream configuration settings
 */
struct StreamSettings {
    // Connection
    QString url;                ///< Full RTMP URL (or base URL for services)
    QString streamKey;          ///< Stream key/token
    StreamService service = StreamService::Custom;
    
    // Timeouts (in seconds)
    int connectTimeout = 10;    ///< Connection timeout
    int reconnectDelay = 5;     ///< Delay between reconnection attempts
    int maxReconnectAttempts = 5; ///< Max reconnection attempts (0 = infinite)
    
    // Buffer settings
    int sendBufferSize = 1024 * 1024;  ///< TCP send buffer (1MB)
    
    // Stream parameters (from encoder)
    int videoWidth = 1920;
    int videoHeight = 1080;
    int videoFpsNum = 60;
    int videoFpsDen = 1;
    int videoBitrate = 6000;    ///< kbps
    
    // Audio (placeholder for future)
    int audioSampleRate = 48000;
    int audioChannels = 2;
    int audioBitrate = 160;     ///< kbps
    
    /**
     * @brief Get the full RTMP URL including stream key
     */
    [[nodiscard]] QString fullUrl() const {
        if (streamKey.isEmpty()) return url;
        
        QString separator = url.endsWith('/') ? "" : "/";
        return url + separator + streamKey;
    }
    
    /**
     * @brief Get service-specific ingest URL
     */
    [[nodiscard]] static QString getServiceUrl(StreamService service) {
        switch (service) {
            case StreamService::Twitch:
                return "rtmp://live.twitch.tv/app";
            case StreamService::YouTube:
                return "rtmp://a.rtmp.youtube.com/live2";
            case StreamService::Facebook:
                return "rtmps://live-api-s.facebook.com:443/rtmp";
            case StreamService::Kick:
                return "rtmp://fa723fc1b171.global-contribute.live-video.net/app";
            case StreamService::TikTok:
                return "rtmp://push.tiktok.com/live";
            default:
                return QString();
        }
    }
};

/**
 * @brief Streaming statistics
 */
struct StreamStatistics {
    int64_t bytesWritten = 0;       ///< Total bytes sent
    int64_t packetsWritten = 0;     ///< Total packets sent
    int64_t keyframesSent = 0;      ///< Keyframes sent
    int64_t droppedPackets = 0;     ///< Packets dropped due to queue overflow
    int64_t streamDurationMs = 0;   ///< Stream duration in milliseconds
    double currentBitrateKbps = 0;  ///< Current bitrate
    double averageLatencyMs = 0;    ///< Average send latency
    int reconnectCount = 0;         ///< Number of reconnections
    StreamState state = StreamState::Stopped;
};

/**
 * @brief RTMP streaming manager using FFmpeg
 * 
 * Handles RTMP output for live streaming to services like
 * Twitch, YouTube, Facebook, etc.
 * 
 * Thread-safe Singleton pattern for application-wide access.
 * 
 * Usage:
 * @code
 *   auto& stream = StreamManager::instance();
 *   
 *   StreamSettings settings;
 *   settings.url = "rtmp://live.twitch.tv/app";
 *   settings.streamKey = "your_stream_key";
 *   
 *   stream.configure(settings);
 *   stream.startStream();
 *   
 *   // Connect to encoder output
 *   encoder.setPacketCallback([&stream](const EncodedPacket& pkt) {
 *       stream.writePacket(pkt.data, pkt.size, pkt.pts, pkt.dts, pkt.isKeyframe);
 *   });
 *   
 *   // Later:
 *   stream.stopStream();
 * @endcode
 */
class StreamManager : public QObject {
    Q_OBJECT

public:
    /**
     * @brief Get singleton instance
     * @return Reference to the StreamManager instance
     */
    static StreamManager& instance();

    // Prevent copying
    StreamManager(const StreamManager&) = delete;
    StreamManager& operator=(const StreamManager&) = delete;

    ~StreamManager() override;

    // =========================================================================
    // Configuration
    // =========================================================================
    
    /**
     * @brief Configure stream settings
     * @param settings Stream configuration
     * @return true if configuration is valid
     */
    bool configure(const StreamSettings& settings);
    
    /**
     * @brief Get current stream settings
     * @return Current configuration
     */
    [[nodiscard]] StreamSettings settings() const;
    
    /**
     * @brief Set codec parameters from encoder
     * 
     * This should be called after encoder is initialized but before
     * starting the stream. The extradata (SPS/PPS) is required for
     * the stream header.
     * 
     * @param codecpar Codec parameters from encoder
     * @return true if parameters were set successfully
     */
    bool setCodecParameters(const AVCodecParameters* codecpar);

    // =========================================================================
    // Stream Control
    // =========================================================================
    
    /**
     * @brief Start streaming to configured URL
     * @return true if connection was initiated
     */
    bool startStream();
    
    /**
     * @brief Start streaming with URL and key
     * @param url RTMP URL
     * @param streamKey Stream key
     * @return true if connection was initiated
     */
    bool startStream(const QString& url, const QString& streamKey);
    
    /**
     * @brief Stop streaming gracefully
     * 
     * Flushes remaining packets and closes connection.
     */
    void stopStream();
    
    /**
     * @brief Get current stream state
     * @return Current state
     */
    [[nodiscard]] StreamState state() const;
    
    /**
     * @brief Check if currently streaming
     * @return true if in Streaming state
     */
    [[nodiscard]] bool isStreaming() const;
    
    /**
     * @brief Check if connected (streaming or connecting)
     * @return true if connection is active
     */
    [[nodiscard]] bool isConnected() const;

    // =========================================================================
    // Packet Input
    // =========================================================================
    
    /**
     * @brief Write an encoded packet to the stream
     * 
     * Thread-safe. Packets are queued and sent asynchronously.
     * 
     * @param data Packet data
     * @param size Data size in bytes
     * @param pts Presentation timestamp (encoder timebase)
     * @param dts Decoding timestamp (encoder timebase)
     * @param isKeyframe True if this is a keyframe
     * @return true if packet was queued
     */
    bool writePacket(const uint8_t* data, int size, 
                     int64_t pts, int64_t dts, bool isKeyframe);
    
    /**
     * @brief Write an AVPacket directly
     * @param packet FFmpeg packet (will be cloned)
     * @return true if packet was queued
     */
    bool writePacket(const AVPacket* packet);
    
    /**
     * @brief Get current packet queue size
     * @return Number of packets waiting to be sent
     */
    [[nodiscard]] int queueSize() const;

    // =========================================================================
    // Statistics
    // =========================================================================
    
    /**
     * @brief Get streaming statistics
     * @return Current statistics
     */
    [[nodiscard]] StreamStatistics statistics() const;
    
    /**
     * @brief Reset statistics
     */
    void resetStatistics();

signals:
    /**
     * @brief Emitted when stream state changes
     * @param state New state
     */
    void stateChanged(StreamState state);
    
    /**
     * @brief Emitted when connected to server
     */
    void connected();
    
    /**
     * @brief Emitted when disconnected
     * @param reason Disconnect reason
     */
    void disconnected(const QString& reason);
    
    /**
     * @brief Emitted when a packet is sent
     * @param pts Packet PTS
     * @param size Packet size
     */
    void packetSent(int64_t pts, int size);
    
    /**
     * @brief Emitted on streaming error
     * @param error Error description
     */
    void streamError(const QString& error);
    
    /**
     * @brief Emitted when attempting to reconnect
     * @param attempt Current attempt number
     */
    void reconnecting(int attempt);

private:
    // Private constructor for singleton
    explicit StreamManager(QObject* parent = nullptr);

    // Internal implementation
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace WeaR
