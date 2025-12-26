#pragma once
// ==============================================================================
// WeaR-studio EncoderManager
// Hardware-accelerated video encoding using FFmpeg (NVENC/AMF/libx264)
// ==============================================================================

#include <QObject>
#include <QMutex>
#include <QImage>
#include <QQueue>
#include <QWaitCondition>

#include <memory>
#include <atomic>
#include <thread>
#include <functional>

// Forward declarations for FFmpeg types (avoid including headers in .h)
struct AVCodec;
struct AVCodecContext;
struct AVFrame;
struct AVPacket;
struct SwsContext;

namespace WeaR {

/**
 * @brief Encoder type enumeration
 */
enum class EncoderType {
    NVENC_H264,     ///< NVIDIA NVENC H.264
    NVENC_HEVC,     ///< NVIDIA NVENC H.265/HEVC
    AMF_H264,       ///< AMD AMF H.264
    AMF_HEVC,       ///< AMD AMF H.265/HEVC
    QSV_H264,       ///< Intel QuickSync H.264
    QSV_HEVC,       ///< Intel QuickSync H.265/HEVC
    X264,           ///< Software libx264
    X265,           ///< Software libx265
    Auto            ///< Auto-detect best available
};

/**
 * @brief Encoder preset (speed vs quality tradeoff)
 */
enum class EncoderPreset {
    UltraFast,      ///< Fastest, lowest quality
    SuperFast,
    VeryFast,
    Faster,
    Fast,
    Medium,         ///< Balanced (default)
    Slow,
    Slower,
    VerySlow,       ///< Slowest, highest quality
    Placebo         ///< Maximum quality (not recommended)
};

/**
 * @brief Rate control mode
 */
enum class RateControlMode {
    CBR,            ///< Constant Bitrate
    VBR,            ///< Variable Bitrate
    CRF,            ///< Constant Rate Factor (quality-based)
    CQP             ///< Constant Quantization Parameter
};

/**
 * @brief Encoder configuration settings
 */
struct EncoderSettings {
    // Video dimensions
    int width = 1920;
    int height = 1080;
    
    // Frame rate
    int fpsNum = 60;        ///< FPS numerator
    int fpsDen = 1;         ///< FPS denominator
    
    // Bitrate settings (in kbps)
    int bitrate = 6000;     ///< Target bitrate
    int maxBitrate = 8000;  ///< Maximum bitrate (for VBR)
    int bufferSize = 12000; ///< VBV buffer size
    
    // Quality settings
    int crf = 23;           ///< CRF value (0-51, lower = better)
    int qp = 20;            ///< QP value for CQP mode
    
    // Encoder selection
    EncoderType encoderType = EncoderType::Auto;
    EncoderPreset preset = EncoderPreset::Fast;
    RateControlMode rateControl = RateControlMode::CBR;
    
    // Keyframe interval
    int keyframeInterval = 2;  ///< Seconds between keyframes
    
    // B-frames (0 for low latency streaming)
    int bFrames = 0;
    
    // Profile and level
    QString profile = "high";   ///< H.264 profile
    QString level = "4.1";      ///< H.264 level
    
    // NVENC specific
    bool nvencLowLatency = true;    ///< NVENC low-latency mode
    bool nvencZeroLatency = false;  ///< NVENC zero-latency (tune)
    
    // Thread count (for software encoders)
    int threads = 0;  ///< 0 = auto
};

/**
 * @brief Encoded packet data passed to callbacks
 */
struct EncodedPacket {
    const uint8_t* data = nullptr;  ///< Packet data
    int size = 0;                    ///< Data size in bytes
    int64_t pts = 0;                 ///< Presentation timestamp
    int64_t dts = 0;                 ///< Decoding timestamp
    bool isKeyframe = false;         ///< True if this is an I-frame
    int64_t duration = 0;            ///< Packet duration
};

/**
 * @brief Callback type for encoded packets
 */
using EncodedPacketCallback = std::function<void(const EncodedPacket& packet)>;

/**
 * @brief Hardware-accelerated video encoder using FFmpeg
 * 
 * Uses NVIDIA NVENC for hardware encoding with automatic fallback
 * to CPU-based libx264 if hardware encoding is unavailable.
 * 
 * Thread-safe Singleton pattern for application-wide access.
 * 
 * Usage:
 * @code
 *   auto& encoder = EncoderManager::instance();
 *   
 *   EncoderSettings settings;
 *   settings.width = 1920;
 *   settings.height = 1080;
 *   settings.bitrate = 6000;
 *   
 *   encoder.setPacketCallback([](const EncodedPacket& pkt) {
 *       // Send to StreamManager
 *   });
 *   
 *   encoder.configure(settings);
 *   encoder.start();
 *   
 *   // From capture loop:
 *   encoder.pushFrame(capturedImage);
 *   
 *   encoder.stop();
 * @endcode
 */
class EncoderManager : public QObject {
    Q_OBJECT

public:
    /**
     * @brief Get singleton instance
     * @return Reference to the EncoderManager instance
     */
    static EncoderManager& instance();

    // Prevent copying
    EncoderManager(const EncoderManager&) = delete;
    EncoderManager& operator=(const EncoderManager&) = delete;

    ~EncoderManager() override;

    // =========================================================================
    // Configuration
    // =========================================================================
    
    /**
     * @brief Configure encoder settings
     * @param settings Encoder configuration
     * @return true if configuration is valid
     */
    bool configure(const EncoderSettings& settings);
    
    /**
     * @brief Get current encoder settings
     * @return Current configuration
     */
    [[nodiscard]] EncoderSettings settings() const;
    
    /**
     * @brief Set callback for encoded packets
     * @param callback Function to receive encoded packets
     */
    void setPacketCallback(EncodedPacketCallback callback);

    // =========================================================================
    // Encoder Control
    // =========================================================================
    
    /**
     * @brief Initialize and start the encoder
     * @return true if encoder started successfully
     */
    bool start();
    
    /**
     * @brief Stop the encoder and flush remaining frames
     */
    void stop();
    
    /**
     * @brief Check if encoder is running
     * @return true if encoding is active
     */
    [[nodiscard]] bool isRunning() const;
    
    /**
     * @brief Check if encoder is initialized
     * @return true if encoder is ready
     */
    [[nodiscard]] bool isInitialized() const;

    // =========================================================================
    // Frame Input
    // =========================================================================
    
    /**
     * @brief Push a frame to the encoding queue
     * 
     * This method is thread-safe and can be called from any thread.
     * The frame will be queued and encoded asynchronously.
     * 
     * @param image Frame to encode (will be converted to YUV internally)
     * @param pts Presentation timestamp (microseconds), -1 for auto
     */
    void pushFrame(const QImage& image, int64_t pts = -1);
    
    /**
     * @brief Get the number of frames waiting in queue
     * @return Queue size
     */
    [[nodiscard]] int queueSize() const;
    
    /**
     * @brief Get maximum queue size before dropping frames
     * @return Max queue size
     */
    [[nodiscard]] int maxQueueSize() const;
    
    /**
     * @brief Set maximum queue size
     * @param size Maximum frames to buffer
     */
    void setMaxQueueSize(int size);

    // =========================================================================
    // Encoder Information
    // =========================================================================
    
    /**
     * @brief Get the active encoder name
     * @return Encoder name (e.g., "h264_nvenc", "libx264")
     */
    [[nodiscard]] QString activeEncoderName() const;
    
    /**
     * @brief Get active encoder type
     * @return Current encoder type
     */
    [[nodiscard]] EncoderType activeEncoderType() const;
    
    /**
     * @brief Check if hardware encoding is available
     * @return true if NVENC/AMF/QSV is available
     */
    [[nodiscard]] static bool isHardwareEncodingAvailable();
    
    /**
     * @brief Get list of available encoders
     * @return List of available encoder names
     */
    [[nodiscard]] static QStringList availableEncoders();
    
    /**
     * @brief Get encoding statistics
     * @return Frames encoded, dropped, etc.
     */
    struct Statistics {
        int64_t framesEncoded = 0;
        int64_t framesDropped = 0;
        int64_t bytesEncoded = 0;
        double averageEncodeTimeMs = 0.0;
        double currentFps = 0.0;
        double averageBitrateKbps = 0.0;
    };
    [[nodiscard]] Statistics statistics() const;

signals:
    /**
     * @brief Emitted when a packet is encoded
     * @param pts Packet PTS
     * @param size Packet size in bytes
     * @param isKeyframe True if keyframe
     */
    void packetEncoded(int64_t pts, int size, bool isKeyframe);
    
    /**
     * @brief Emitted when encoder encounters an error
     * @param error Error description
     */
    void encoderError(const QString& error);
    
    /**
     * @brief Emitted when encoder is ready
     */
    void encoderReady();
    
    /**
     * @brief Emitted when encoder is stopped
     */
    void encoderStopped();

private:
    // Private constructor for singleton
    explicit EncoderManager(QObject* parent = nullptr);

    // Internal implementation
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace WeaR
