#pragma once
// ==============================================================================
// WeaR-studio Source Interface
// ISource.h - Interface for video/audio source plugins
// ==============================================================================

#include "IPlugin.h"
#include <QImage>
#include <QSize>
#include <QRect>
#include <memory>

// Forward declarations for hardware acceleration
struct ID3D11Texture2D;
struct ID3D11Device;

namespace WeaR {

/**
 * @brief Video frame data container
 * Holds either software (QImage) or hardware (D3D11 texture) frame data
 */
struct VideoFrame {
    QImage softwareFrame;           ///< CPU-accessible frame (RGBA)
    ID3D11Texture2D* hardwareFrame = nullptr;  ///< GPU texture (optional)
    int64_t timestamp = 0;          ///< Presentation timestamp (microseconds)
    int64_t frameNumber = 0;        ///< Sequential frame number
    bool isHardwareFrame = false;   ///< True if hardwareFrame is valid

    [[nodiscard]] bool isValid() const {
        return isHardwareFrame ? (hardwareFrame != nullptr) : !softwareFrame.isNull();
    }

    [[nodiscard]] QSize size() const {
        return softwareFrame.isNull() ? QSize() : softwareFrame.size();
    }
};

/**
 * @brief Audio sample data container
 */
struct AudioFrame {
    std::vector<float> samples;     ///< Interleaved audio samples
    int sampleRate = 48000;         ///< Sample rate in Hz
    int channels = 2;               ///< Number of audio channels
    int64_t timestamp = 0;          ///< Presentation timestamp (microseconds)

    [[nodiscard]] bool isValid() const {
        return !samples.empty() && sampleRate > 0 && channels > 0;
    }
};

/**
 * @brief Source configuration
 */
struct SourceConfig {
    QSize resolution = {1920, 1080};   ///< Desired output resolution
    double fps = 30.0;                  ///< Target frame rate
    bool useHardwareAcceleration = true; ///< Prefer GPU frames
    QRect captureRegion;                ///< Region of interest (empty = full)
    QString deviceId;                   ///< Device identifier (for capture devices)
};

/**
 * @brief Source interface for video/audio input plugins
 * 
 * Implementations include:
 * - Screen/window capture
 * - Webcam capture
 * - Media file playback
 * - Browser sources
 * - Image sources
 * - Text sources
 * 
 * @note Sources should emit frames at the configured FPS rate
 */
class ISource : public IPlugin {
public:
    ~ISource() override = default;

    /**
     * @brief Get plugin type (always Source)
     */
    [[nodiscard]] PluginType type() const override { return PluginType::Source; }

    /**
     * @brief Configure the source
     * @param config Source configuration parameters
     * @return true if configuration succeeded
     */
    virtual bool configure(const SourceConfig& config) = 0;

    /**
     * @brief Get current configuration
     * @return Current source configuration
     */
    [[nodiscard]] virtual SourceConfig config() const = 0;

    /**
     * @brief Start capturing/generating frames
     * @return true if started successfully
     */
    virtual bool start() = 0;

    /**
     * @brief Stop capturing/generating frames
     */
    virtual void stop() = 0;

    /**
     * @brief Check if the source is currently running
     * @return true if actively producing frames
     */
    [[nodiscard]] virtual bool isRunning() const = 0;

    /**
     * @brief Capture/get the current video frame
     * 
     * For real-time sources (capture), this returns the latest frame.
     * For media sources, this returns the current playback frame.
     * 
     * @return VideoFrame containing the current frame data
     */
    [[nodiscard]] virtual VideoFrame captureVideoFrame() = 0;

    /**
     * @brief Capture/get current audio samples
     * 
     * Only valid if HasAudio capability is set.
     * 
     * @return AudioFrame containing audio sample data
     */
    [[nodiscard]] virtual AudioFrame captureAudioFrame() {
        return AudioFrame(); // Default: no audio
    }

    /**
     * @brief Get the native resolution of this source
     * @return Native resolution (before any scaling)
     */
    [[nodiscard]] virtual QSize nativeResolution() const = 0;

    /**
     * @brief Get the native frame rate
     * @return Native FPS of the source
     */
    [[nodiscard]] virtual double nativeFps() const = 0;

    /**
     * @brief Get output resolution (after scaling)
     * @return Current output resolution
     */
    [[nodiscard]] virtual QSize outputResolution() const = 0;

    /**
     * @brief Get current FPS
     * @return Configured output FPS
     */
    [[nodiscard]] virtual double outputFps() const = 0;

    /**
     * @brief Set the D3D11 device for hardware acceleration
     * 
     * Optional: Only needed if source supports hardware frames.
     * Must be called before start() if hardware acceleration is desired.
     * 
     * @param device Pointer to ID3D11Device
     */
    virtual void setD3D11Device(ID3D11Device* device) {
        (void)device; // Suppress unused parameter warning
    }

    /**
     * @brief List available devices/sources
     * 
     * For capture sources, returns list of available devices.
     * 
     * @return List of device IDs that can be used in SourceConfig
     */
    [[nodiscard]] virtual QStringList availableDevices() const {
        return QStringList();
    }
};

} // namespace WeaR

// Qt Plugin interface declaration for sources
#define WEAR_SOURCE_IID "com.wear-studio.source/1.0"
Q_DECLARE_INTERFACE(WeaR::ISource, WEAR_SOURCE_IID)
