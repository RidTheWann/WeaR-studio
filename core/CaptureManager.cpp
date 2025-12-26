// ==============================================================================
// WeaR-studio CaptureManager Implementation
// High-performance screen/window capture using Windows Graphics Capture API
// ==============================================================================

#include "CaptureManager.h"

// C++/WinRT headers
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <winrt/Windows.System.h>

// Windows.Graphics.Capture interop
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>

// Additional Windows headers
#include <dwmapi.h>
#include <ShellScalingApi.h>
#include <DispatcherQueue.h>

#include <QDebug>
#include <QDateTime>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "shcore.lib")

namespace WeaR {

using namespace winrt;
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::Graphics;
using namespace winrt::Windows::Graphics::Capture;
using namespace winrt::Windows::Graphics::DirectX;
using namespace winrt::Windows::Graphics::DirectX::Direct3D11;

// ==============================================================================
// Helper: Create WinRT Direct3D device from D3D11 device
// ==============================================================================
static IDirect3DDevice CreateDirect3DDevice(ID3D11Device* d3dDevice) {
    ComPtr<IDXGIDevice> dxgiDevice;
    HRESULT hr = d3dDevice->QueryInterface(IID_PPV_ARGS(&dxgiDevice));
    if (FAILED(hr)) {
        throw std::runtime_error("Failed to get DXGI device");
    }
    
    winrt::com_ptr<::IInspectable> inspectable;
    hr = CreateDirect3D11DeviceFromDXGIDevice(dxgiDevice.Get(), inspectable.put());
    if (FAILED(hr)) {
        throw std::runtime_error("Failed to create WinRT Direct3D device");
    }
    
    return inspectable.as<IDirect3DDevice>();
}

// ==============================================================================
// Helper: Get D3D11 texture from WinRT Direct3DSurface
// ==============================================================================
static ComPtr<ID3D11Texture2D> GetTextureFromSurface(
    const IDirect3DSurface& surface,
    ComPtr<ID3D11Device>& device
) {
    auto access = surface.as<Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
    ComPtr<ID3D11Texture2D> texture;
    HRESULT hr = access->GetInterface(IID_PPV_ARGS(&texture));
    if (FAILED(hr)) {
        return nullptr;
    }
    return texture;
}

// ==============================================================================
// Implementation class (PIMPL) - Isolates WinRT types from header
// ==============================================================================
class CaptureManager::Impl {
public:
    Impl(CaptureManager* parent) : m_parent(parent) {}
    ~Impl() { cleanup(); }
    
    bool initialize(ID3D11Device* device) {
        if (!device) return false;
        
        try {
            m_winrtDevice = CreateDirect3DDevice(device);
            m_initialized = true;
            return true;
        } catch (const std::exception& e) {
            qWarning() << "Failed to initialize WinRT device:" << e.what();
            return false;
        }
    }
    
    void cleanup() {
        stopCapture();
        m_winrtDevice = nullptr;
        m_initialized = false;
    }
    
    bool startCapture(const CaptureTarget& target, bool showCursor, bool showBorder) {
        if (!m_initialized) return false;
        
        try {
            // Create GraphicsCaptureItem based on target type
            GraphicsCaptureItem item{nullptr};
            
            auto interop = winrt::get_activation_factory<GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
            
            if (target.type == CaptureTargetType::Window && target.hwnd) {
                HRESULT hr = interop->CreateForWindow(
                    target.hwnd,
                    winrt::guid_of<IGraphicsCaptureItem>(),
                    winrt::put_abi(item)
                );
                if (FAILED(hr) || !item) {
                    qWarning() << "Failed to create capture item for window";
                    return false;
                }
            } else if (target.type == CaptureTargetType::Monitor && target.hmonitor) {
                HRESULT hr = interop->CreateForMonitor(
                    target.hmonitor,
                    winrt::guid_of<IGraphicsCaptureItem>(),
                    winrt::put_abi(item)
                );
                if (FAILED(hr) || !item) {
                    qWarning() << "Failed to create capture item for monitor";
                    return false;
                }
            } else {
                qWarning() << "Invalid capture target";
                return false;
            }
            
            m_captureItem = item;
            
            // Get item size
            auto size = item.Size();
            m_frameWidth = size.Width;
            m_frameHeight = size.Height;
            
            // Create frame pool
            m_framePool = Direct3D11CaptureFramePool::CreateFreeThreaded(
                m_winrtDevice,
                DirectXPixelFormat::B8G8R8A8UIntNormalized,
                2,  // Buffer count
                size
            );
            
            // Set up frame arrived handler
            m_framePool.FrameArrived([this](auto&& pool, auto&&) {
                onFrameArrived(pool);
            });
            
            // Create capture session
            m_session = m_framePool.CreateCaptureSession(item);
            
            // Configure session (Windows 10 2004+)
            try {
                m_session.IsCursorCaptureEnabled(showCursor);
            } catch (...) {
                // Cursor capture control not available on older Windows
            }
            
            try {
                m_session.IsBorderRequired(showBorder);
            } catch (...) {
                // Border control not available on older Windows
            }
            
            // Handle item closed event
            m_captureItem.Closed([this](auto&&, auto&&) {
                QMetaObject::invokeMethod(m_parent, [this]() {
                    emit m_parent->captureClosed();
                });
            });
            
            // Start capturing
            m_session.StartCapture();
            m_capturing = true;
            
            qDebug() << "Capture started:" << target.name 
                     << "(" << m_frameWidth << "x" << m_frameHeight << ")";
            
            return true;
            
        } catch (const winrt::hresult_error& e) {
            qWarning() << "WinRT error starting capture:" 
                       << QString::fromStdWString(e.message().c_str());
            return false;
        } catch (const std::exception& e) {
            qWarning() << "Error starting capture:" << e.what();
            return false;
        }
    }
    
    void stopCapture() {
        if (!m_capturing) return;
        
        m_capturing = false;
        
        try {
            if (m_session) {
                m_session.Close();
                m_session = nullptr;
            }
            if (m_framePool) {
                m_framePool.Close();
                m_framePool = nullptr;
            }
            m_captureItem = nullptr;
        } catch (...) {
            // Ignore errors during cleanup
        }
        
        qDebug() << "Capture stopped";
    }
    
    ComPtr<ID3D11Texture2D> getLatestFrame(int64_t& timestamp) {
        QMutexLocker lock(&m_frameMutex);
        timestamp = m_latestTimestamp;
        return m_latestTexture;
    }
    
    bool isCapturing() const { return m_capturing; }
    int frameWidth() const { return m_frameWidth; }
    int frameHeight() const { return m_frameHeight; }
    
private:
    void onFrameArrived(const Direct3D11CaptureFramePool& pool) {
        auto frame = pool.TryGetNextFrame();
        if (!frame) return;
        
        try {
            auto surface = frame.Surface();
            auto size = frame.ContentSize();
            
            // Check if size changed
            if (size.Width != m_frameWidth || size.Height != m_frameHeight) {
                m_frameWidth = size.Width;
                m_frameHeight = size.Height;
                
                // Recreate frame pool with new size
                m_framePool.Recreate(
                    m_winrtDevice,
                    DirectXPixelFormat::B8G8R8A8UIntNormalized,
                    2,
                    size
                );
            }
            
            // Get D3D11 texture from surface
            ComPtr<ID3D11Device> device;
            m_parent->m_d3dDevice.As(&device);
            
            auto texture = GetTextureFromSurface(surface, device);
            if (texture) {
                QMutexLocker lock(&m_frameMutex);
                m_latestTexture = texture;
                m_latestTimestamp = QDateTime::currentMSecsSinceEpoch() * 1000;  // Convert to microseconds
                m_frameCount++;
            }
            
            // Notify parent
            QMetaObject::invokeMethod(m_parent, [this]() {
                emit m_parent->frameCaptured(m_latestTimestamp);
            });
            
        } catch (const std::exception& e) {
            qWarning() << "Error processing captured frame:" << e.what();
        }
    }
    
    CaptureManager* m_parent;
    bool m_initialized = false;
    bool m_capturing = false;
    
    // WinRT objects
    IDirect3DDevice m_winrtDevice{nullptr};
    GraphicsCaptureItem m_captureItem{nullptr};
    Direct3D11CaptureFramePool m_framePool{nullptr};
    GraphicsCaptureSession m_session{nullptr};
    
    // Frame data
    QMutex m_frameMutex;
    ComPtr<ID3D11Texture2D> m_latestTexture;
    int64_t m_latestTimestamp = 0;
    int64_t m_frameCount = 0;
    int m_frameWidth = 0;
    int m_frameHeight = 0;
};

// ==============================================================================
// CaptureManager Singleton
// ==============================================================================
CaptureManager& CaptureManager::instance() {
    static CaptureManager instance;
    return instance;
}

CaptureManager::CaptureManager(QObject* parent)
    : QObject(parent)
    , m_impl(std::make_unique<Impl>(this))
{
    // Default configuration
    m_config.resolution = QSize(1920, 1080);
    m_config.fps = 60.0;
    m_config.useHardwareAcceleration = true;
}

CaptureManager::~CaptureManager() {
    shutdown();
}

// ==============================================================================
// IPlugin Interface Implementation
// ==============================================================================
PluginInfo CaptureManager::info() const {
    return PluginInfo{
        .id = QStringLiteral("wear.source.screen-capture"),
        .name = QStringLiteral("Screen Capture"),
        .description = QStringLiteral("High-performance screen and window capture using Windows Graphics Capture API"),
        .version = QStringLiteral("0.1"),
        .author = QStringLiteral("WeaR-studio"),
        .website = QStringLiteral("https://github.com/wear-studio"),
        .type = PluginType::Source,
        .capabilities = capabilities()
    };
}

PluginCapability CaptureManager::capabilities() const {
    return PluginCapability::HasVideo 
         | PluginCapability::HasSettings
         | PluginCapability::HasPreview
         | PluginCapability::RequiresGPU
         | PluginCapability::ThreadSafe;
}

bool CaptureManager::initialize() {
    QMutexLocker lock(&m_mutex);
    
    if (m_initialized) return true;
    
    // Check platform support
    if (!isSupported()) {
        qWarning() << "Windows Graphics Capture is not supported on this system";
        return false;
    }
    
    // Initialize D3D11 device if we don't have one
    if (!m_d3dDevice) {
        if (!initializeD3D11()) {
            qWarning() << "Failed to initialize D3D11 device";
            return false;
        }
    }
    
    // Initialize WinRT wrapper
    if (!m_impl->initialize(m_d3dDevice.Get())) {
        qWarning() << "Failed to initialize WinRT capture";
        return false;
    }
    
    m_initialized = true;
    qDebug() << "CaptureManager initialized successfully";
    return true;
}

void CaptureManager::shutdown() {
    QMutexLocker lock(&m_mutex);
    
    if (!m_initialized) return;
    
    stop();
    m_impl->cleanup();
    cleanupD3D11();
    
    m_initialized = false;
    qDebug() << "CaptureManager shut down";
}

bool CaptureManager::isActive() const {
    return m_initialized;
}

// ==============================================================================
// ISource Interface Implementation
// ==============================================================================
bool CaptureManager::configure(const SourceConfig& config) {
    QMutexLocker lock(&m_mutex);
    m_config = config;
    return true;
}

SourceConfig CaptureManager::config() const {
    QMutexLocker lock(&m_mutex);
    return m_config;
}

bool CaptureManager::start() {
    QMutexLocker lock(&m_mutex);
    
    if (!m_initialized) {
        lock.unlock();
        if (!initialize()) return false;
        lock.relock();
    }
    
    if (m_running) return true;
    
    if (!m_currentTarget.isValid()) {
        qWarning() << "No capture target set";
        return false;
    }
    
    if (!m_impl->startCapture(m_currentTarget, m_showCursor, m_showBorder)) {
        return false;
    }
    
    m_running = true;
    return true;
}

void CaptureManager::stop() {
    QMutexLocker lock(&m_mutex);
    
    if (!m_running) return;
    
    m_impl->stopCapture();
    m_running = false;
    m_currentFrame.Reset();
}

bool CaptureManager::isRunning() const {
    return m_running;
}

VideoFrame CaptureManager::captureVideoFrame() {
    VideoFrame frame;
    
    if (!m_running) return frame;
    
    int64_t timestamp;
    auto texture = m_impl->getLatestFrame(timestamp);
    
    if (!texture) return frame;
    
    frame.hardwareFrame = texture.Get();
    frame.isHardwareFrame = true;
    frame.timestamp = timestamp;
    frame.frameNumber = m_frameNumber++;
    
    // Store reference to keep texture alive
    m_currentFrame = texture;
    m_frameTimestamp = timestamp;
    
    // If software frame is needed (e.g., for preview), convert to QImage
    // This is expensive - only do when necessary
    if (!m_config.useHardwareAcceleration) {
        frame.softwareFrame = textureToQImage(texture.Get());
        frame.isHardwareFrame = false;
    }
    
    return frame;
}

QSize CaptureManager::nativeResolution() const {
    if (m_currentTarget.isValid()) {
        return m_currentTarget.size;
    }
    return QSize(m_impl->frameWidth(), m_impl->frameHeight());
}

double CaptureManager::nativeFps() const {
    // Windows Graphics Capture runs at display refresh rate
    return 60.0;
}

QSize CaptureManager::outputResolution() const {
    return m_config.resolution;
}

double CaptureManager::outputFps() const {
    return m_config.fps;
}

void CaptureManager::setD3D11Device(ID3D11Device* device) {
    QMutexLocker lock(&m_mutex);
    
    if (m_running) {
        qWarning() << "Cannot change D3D11 device while capturing";
        return;
    }
    
    cleanupD3D11();
    
    if (device) {
        m_d3dDevice = device;
        device->GetImmediateContext(&m_d3dContext);
        m_ownsDevice = false;
    }
}

QStringList CaptureManager::availableDevices() const {
    QStringList devices;
    auto targets = enumerateTargets();
    for (const auto& target : targets) {
        devices.append(target.id);
    }
    return devices;
}

// ==============================================================================
// CaptureManager Specific API
// ==============================================================================
bool CaptureManager::isSupported() {
    // Check if GraphicsCaptureSession is supported (Windows 10 1903+)
    return winrt::Windows::Graphics::Capture::GraphicsCaptureSession::IsSupported();
}

QList<CaptureTarget> CaptureManager::enumerateTargets(
    bool includeWindows, 
    bool includeMonitors
) const {
    QList<CaptureTarget> targets;
    
    if (includeMonitors) {
        targets.append(enumerateMonitors());
    }
    
    if (includeWindows) {
        targets.append(enumerateWindows());
    }
    
    return targets;
}

QList<CaptureTarget> CaptureManager::enumerateMonitors() const {
    QList<CaptureTarget> monitors;
    
    int monitorIndex = 0;
    EnumDisplayMonitors(nullptr, nullptr, 
        [](HMONITOR hMonitor, HDC, LPRECT, LPARAM lParam) -> BOOL {
            auto* monitors = reinterpret_cast<QList<CaptureTarget>*>(lParam);
            
            MONITORINFOEXW monitorInfo;
            monitorInfo.cbSize = sizeof(MONITORINFOEXW);
            
            if (GetMonitorInfoW(hMonitor, &monitorInfo)) {
                CaptureTarget target;
                target.type = CaptureTargetType::Monitor;
                target.hmonitor = hMonitor;
                target.id = QString("monitor_%1").arg(monitors->size());
                target.name = QString::fromWCharArray(monitorInfo.szDevice);
                target.size = QSize(
                    monitorInfo.rcMonitor.right - monitorInfo.rcMonitor.left,
                    monitorInfo.rcMonitor.bottom - monitorInfo.rcMonitor.top
                );
                
                // Check if primary
                if (monitorInfo.dwFlags & MONITORINFOF_PRIMARY) {
                    target.name = QString("Primary Monitor (%1)").arg(target.name);
                }
                
                monitors->append(target);
            }
            
            return TRUE;
        }, 
        reinterpret_cast<LPARAM>(&monitors)
    );
    
    return monitors;
}

QList<CaptureTarget> CaptureManager::enumerateWindows(bool visibleOnly) const {
    QList<CaptureTarget> windows;
    
    struct EnumData {
        QList<CaptureTarget>* windows;
        bool visibleOnly;
    };
    
    EnumData data{&windows, visibleOnly};
    
    EnumWindows(
        [](HWND hwnd, LPARAM lParam) -> BOOL {
            auto* data = reinterpret_cast<EnumData*>(lParam);
            
            // Skip invisible windows if requested
            if (data->visibleOnly && !IsWindowVisible(hwnd)) {
                return TRUE;
            }
            
            // Skip minimized windows
            if (IsIconic(hwnd)) {
                return TRUE;
            }
            
            // Skip windows with no title
            int titleLength = GetWindowTextLengthW(hwnd);
            if (titleLength == 0) {
                return TRUE;
            }
            
            // Skip tool windows and other non-capturable windows
            LONG exStyle = GetWindowLong(hwnd, GWL_EXSTYLE);
            if (exStyle & WS_EX_TOOLWINDOW) {
                return TRUE;
            }
            
            // Get window title
            std::wstring title(titleLength + 1, L'\0');
            GetWindowTextW(hwnd, title.data(), titleLength + 1);
            
            // Get window size
            RECT rect;
            DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &rect, sizeof(rect));
            
            // Skip tiny windows
            int width = rect.right - rect.left;
            int height = rect.bottom - rect.top;
            if (width < 100 || height < 100) {
                return TRUE;
            }
            
            CaptureTarget target;
            target.type = CaptureTargetType::Window;
            target.hwnd = hwnd;
            target.id = QString("window_%1").arg(reinterpret_cast<quintptr>(hwnd), 0, 16);
            target.name = QString::fromStdWString(title);
            target.size = QSize(width, height);
            
            data->windows->append(target);
            
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&data)
    );
    
    return windows;
}

bool CaptureManager::setTarget(const CaptureTarget& target) {
    QMutexLocker lock(&m_mutex);
    
    if (m_running) {
        qWarning() << "Cannot change target while capturing. Stop first.";
        return false;
    }
    
    if (!target.isValid()) {
        qWarning() << "Invalid capture target";
        return false;
    }
    
    m_currentTarget = target;
    qDebug() << "Capture target set:" << target.name;
    return true;
}

CaptureTarget CaptureManager::currentTarget() const {
    QMutexLocker lock(&m_mutex);
    return m_currentTarget;
}

ID3D11Device* CaptureManager::d3d11Device() const {
    return m_d3dDevice.Get();
}

ID3D11DeviceContext* CaptureManager::d3d11Context() const {
    return m_d3dContext.Get();
}

ID3D11Texture2D* CaptureManager::currentFrameTexture() const {
    return m_currentFrame.Get();
}

void CaptureManager::setShowCursor(bool show) {
    m_showCursor = show;
}

bool CaptureManager::showCursor() const {
    return m_showCursor;
}

void CaptureManager::setShowBorder(bool show) {
    m_showBorder = show;
}

bool CaptureManager::showBorder() const {
    return m_showBorder;
}

// ==============================================================================
// Internal Methods
// ==============================================================================
bool CaptureManager::initializeD3D11() {
    UINT createDeviceFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    
#ifdef _DEBUG
    createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
    
    D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
    };
    
    D3D_FEATURE_LEVEL featureLevel;
    
    HRESULT hr = D3D11CreateDevice(
        nullptr,                    // Default adapter
        D3D_DRIVER_TYPE_HARDWARE,   // Hardware acceleration
        nullptr,                    // No software rasterizer
        createDeviceFlags,
        featureLevels,
        ARRAYSIZE(featureLevels),
        D3D11_SDK_VERSION,
        &m_d3dDevice,
        &featureLevel,
        &m_d3dContext
    );
    
    if (FAILED(hr)) {
        qWarning() << "Failed to create D3D11 device:" << Qt::hex << hr;
        return false;
    }
    
    m_ownsDevice = true;
    qDebug() << "D3D11 device created, feature level:" << Qt::hex << featureLevel;
    return true;
}

void CaptureManager::cleanupD3D11() {
    m_currentFrame.Reset();
    m_stagingTexture.Reset();
    
    if (m_ownsDevice) {
        m_d3dContext.Reset();
        m_d3dDevice.Reset();
    }
    
    m_ownsDevice = false;
}

QImage CaptureManager::textureToQImage(ID3D11Texture2D* texture) {
    if (!texture || !m_d3dContext) return QImage();
    
    // Get texture description
    D3D11_TEXTURE2D_DESC desc;
    texture->GetDesc(&desc);
    
    // Create staging texture if needed
    if (!m_stagingTexture) {
        D3D11_TEXTURE2D_DESC stagingDesc = desc;
        stagingDesc.Usage = D3D11_USAGE_STAGING;
        stagingDesc.BindFlags = 0;
        stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        stagingDesc.MiscFlags = 0;
        
        HRESULT hr = m_d3dDevice->CreateTexture2D(&stagingDesc, nullptr, &m_stagingTexture);
        if (FAILED(hr)) {
            qWarning() << "Failed to create staging texture";
            return QImage();
        }
    }
    
    // Copy to staging texture
    m_d3dContext->CopyResource(m_stagingTexture.Get(), texture);
    
    // Map staging texture
    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT hr = m_d3dContext->Map(m_stagingTexture.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) {
        qWarning() << "Failed to map staging texture";
        return QImage();
    }
    
    // Create QImage from mapped data
    QImage image(
        static_cast<const uchar*>(mapped.pData),
        desc.Width,
        desc.Height,
        mapped.RowPitch,
        QImage::Format_ARGB32
    );
    
    // Make a copy since we need to unmap
    QImage result = image.copy();
    
    m_d3dContext->Unmap(m_stagingTexture.Get(), 0);
    
    return result;
}

} // namespace WeaR
