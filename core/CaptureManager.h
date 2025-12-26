#pragma once
// ==============================================================================
// WeaR-studio CaptureManager
// High-performance screen/window capture using Windows Graphics Capture API
// ==============================================================================

#include "ISource.h"

#include <QObject>
#include <QMutex>
#include <QThread>
#include <QTimer>
#include <QImage>

#include <memory>
#include <atomic>
#include <functional>

// Windows headers
#include <Windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

// Forward declarations for WinRT types (implementation uses winrt headers)
namespace winrt::Windows::Graphics::Capture {
    struct GraphicsCaptureItem;
    struct Direct3D11CaptureFramePool;
    struct GraphicsCaptureSession;
}

namespace WeaR {

using Microsoft::WRL::ComPtr;

/**
 * @brief Capture target type
 */
enum class CaptureTargetType {
    Monitor,    ///< Entire monitor/display
    Window,     ///< Specific window
};

/**
 * @brief Information about a capturable target
 */
struct CaptureTarget {
    QString id;                 ///< Unique identifier
    QString name;               ///< Display name
    CaptureTargetType type;     ///< Monitor or Window
    QSize size;                 ///< Native resolution
    HWND hwnd = nullptr;        ///< Window handle (for windows)
    HMONITOR hmonitor = nullptr; ///< Monitor handle (for monitors)
    
    bool isValid() const { return !id.isEmpty(); }
};

/**
 * @brief High-performance screen/window capture manager
 * 
 * Uses Windows Graphics Capture API (Windows 10 1903+) for zero-copy
 * GPU-accelerated screen capture. Frames are kept on GPU as D3D11
 * textures for efficient encoding pipeline.
 * 
 * Thread-safe Singleton pattern for application-wide access.
 * 
 * Usage:
 * @code
 *   auto& capture = CaptureManager::instance();
 *   auto targets = capture.enumerateTargets();
 *   capture.setTarget(targets[0]);
 *   capture.start();
 *   // ... frames available via captureVideoFrame()
 *   capture.stop();
 * @endcode
 */
class CaptureManager : public QObject, public ISource {
    Q_OBJECT
    Q_INTERFACES(WeaR::ISource)

public:
    /**
     * @brief Get singleton instance
     * @return Reference to the CaptureManager instance
     */
    static CaptureManager& instance();

    // Prevent copying
    CaptureManager(const CaptureManager&) = delete;
    CaptureManager& operator=(const CaptureManager&) = delete;

    ~CaptureManager() override;

    // =========================================================================
    // IPlugin Interface
    // =========================================================================
    [[nodiscard]] PluginInfo info() const override;
    [[nodiscard]] QString name() const override { return QStringLiteral("Screen Capture"); }
    [[nodiscard]] QString version() const override { return QStringLiteral("1.0.0"); }
    [[nodiscard]] PluginType type() const override { return PluginType::Source; }
    [[nodiscard]] PluginCapability capabilities() const override;
    
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
    
    [[nodiscard]] VideoFrame captureVideoFrame() override;
    [[nodiscard]] QSize nativeResolution() const override;
    [[nodiscard]] double nativeFps() const override;
    [[nodiscard]] QSize outputResolution() const override;
    [[nodiscard]] double outputFps() const override;
    
    void setD3D11Device(ID3D11Device* device) override;
    [[nodiscard]] QStringList availableDevices() const override;

    // =========================================================================
    // CaptureManager Specific API
    // =========================================================================
    
    /**
     * @brief Check if Windows Graphics Capture API is supported
     * @return true if the system supports WGC
     */
    [[nodiscard]] static bool isSupported();
    
    /**
     * @brief Enumerate all capturable targets (monitors and windows)
     * @param includeWindows Include application windows
     * @param includeMonitors Include monitors/displays
     * @return List of available capture targets
     */
    [[nodiscard]] QList<CaptureTarget> enumerateTargets(
        bool includeWindows = true, 
        bool includeMonitors = true
    ) const;
    
    /**
     * @brief Enumerate only monitors
     * @return List of available monitors
     */
    [[nodiscard]] QList<CaptureTarget> enumerateMonitors() const;
    
    /**
     * @brief Enumerate only windows
     * @param visibleOnly Only include visible, non-minimized windows
     * @return List of available windows
     */
    [[nodiscard]] QList<CaptureTarget> enumerateWindows(bool visibleOnly = true) const;
    
    /**
     * @brief Set the current capture target
     * @param target Target to capture
     * @return true if target was set successfully
     */
    bool setTarget(const CaptureTarget& target);
    
    /**
     * @brief Get current capture target
     * @return Current target, invalid if none set
     */
    [[nodiscard]] CaptureTarget currentTarget() const;
    
    /**
     * @brief Get the internal D3D11 device
     * @return Pointer to the D3D11 device
     */
    [[nodiscard]] ID3D11Device* d3d11Device() const;
    
    /**
     * @brief Get the internal D3D11 device context
     * @return Pointer to the D3D11 immediate context
     */
    [[nodiscard]] ID3D11DeviceContext* d3d11Context() const;
    
    /**
     * @brief Get the current frame as a D3D11 texture (zero-copy)
     * 
     * This is the preferred method for the encoding pipeline as it
     * keeps the frame on the GPU without expensive CPU copies.
     * 
     * @return Pointer to the current frame texture, nullptr if no frame
     */
    [[nodiscard]] ID3D11Texture2D* currentFrameTexture() const;
    
    /**
     * @brief Set whether to show cursor in capture
     * @param show true to include cursor
     */
    void setShowCursor(bool show);
    
    /**
     * @brief Get cursor visibility setting
     * @return true if cursor is included in capture
     */
    [[nodiscard]] bool showCursor() const;
    
    /**
     * @brief Set whether to show yellow border around captured window
     * @param show true to show border (Windows default)
     */
    void setShowBorder(bool show);
    
    /**
     * @brief Get border visibility setting
     * @return true if yellow border is shown
     */
    [[nodiscard]] bool showBorder() const;

signals:
    /**
     * @brief Emitted when a new frame is captured
     * @param timestamp Frame timestamp in microseconds
     */
    void frameCaptured(int64_t timestamp);
    
    /**
     * @brief Emitted when capture session is closed (e.g., window closed)
     */
    void captureClosed();
    
    /**
     * @brief Emitted when an error occurs
     * @param error Error description
     */
    void captureError(const QString& error);

private:
    // Private constructor for singleton
    explicit CaptureManager(QObject* parent = nullptr);
    
    // Internal implementation class (PIMPL for WinRT isolation)
    class Impl;
    std::unique_ptr<Impl> m_impl;
    
    // Thread safety
    mutable QMutex m_mutex;
    
    // State
    std::atomic<bool> m_initialized{false};
    std::atomic<bool> m_running{false};
    
    // Configuration
    SourceConfig m_config;
    CaptureTarget m_currentTarget;
    bool m_showCursor = true;
    bool m_showBorder = false;
    
    // D3D11 resources (may be external or internal)
    ComPtr<ID3D11Device> m_d3dDevice;
    ComPtr<ID3D11DeviceContext> m_d3dContext;
    bool m_ownsDevice = false;
    
    // Current frame
    ComPtr<ID3D11Texture2D> m_currentFrame;
    ComPtr<ID3D11Texture2D> m_stagingTexture;  // For CPU readback if needed
    int64_t m_frameTimestamp = 0;
    int64_t m_frameNumber = 0;
    
    // Internal methods
    bool initializeD3D11();
    void cleanupD3D11();
    bool createCaptureSession();
    void destroyCaptureSession();
    void onFrameArrived();
    QImage textureToQImage(ID3D11Texture2D* texture);
};

} // namespace WeaR
