// =============================================================================
// WeaR-studio Virtual Camera Manager implementation
// =============================================================================

#include "VirtualCameraManager.h"
#include "VirtualCameraProtocol.h"

#ifdef Q_OS_WIN

#include <mfapi.h>
#include <mfvirtualcamera.h>

#include <QDebug>
#include <QDateTime>

#include <sddl.h>
#include <windows.h>

#include <atomic>
#include <thread>
#include <vector>

#pragma comment(lib, "mfsensorgroup.lib")
#pragma comment(lib, "advapi32.lib")

namespace WeaR {
namespace {

inline constexpr char kVirtualCameraClsid[] =
    "{A5E4C9E0-0F54-4A2E-9C10-74E1F6E4DCD1}";

bool writeAll(HANDLE pipe, const void* data, DWORD size) {
    const auto* bytes = static_cast<const BYTE*>(data);
    while (size > 0) {
        DWORD written = 0;
        if (!WriteFile(pipe, bytes, size, &written, nullptr) || written == 0) {
            return false;
        }
        bytes += written;
        size -= written;
    }
    return true;
}

SECURITY_ATTRIBUTES makePipeSecurity(PSECURITY_DESCRIPTOR* descriptorOut) {
    *descriptorOut = nullptr;

    // The Media Foundation Frame Server may activate the media source as
    // LocalService/System. Authenticated users are also allowed because the
    // producer is the current WeaR Studio user process.
    constexpr LPCWSTR sddl =
        L"D:P(A;;GA;;;SY)(A;;GA;;;LS)(A;;GA;;;AU)";

    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            sddl,
            SDDL_REVISION_1,
            &descriptor,
            nullptr)) {
        return {};
    }

    SECURITY_ATTRIBUTES attributes{};
    attributes.nLength = sizeof(attributes);
    attributes.lpSecurityDescriptor = descriptor;
    attributes.bInheritHandle = FALSE;
    *descriptorOut = descriptor;
    return attributes;
}

} // namespace

class VirtualCameraManager::Impl {
public:
    explicit Impl(VirtualCameraManager* owner)
        : m_owner(owner) {}

    ~Impl() {
        stopPipeServer();
    }

    bool startPipeServer() {
        if (m_pipeThread.joinable()) {
            return true;
        }

        m_stopPipe.store(false);
        m_pipeThread = std::thread([this] {
            pipeLoop();
        });
        return true;
    }

    void stopPipeServer() {
        m_stopPipe.store(true);

        // Wake a blocked ConnectNamedPipe call.
        HANDLE wake = CreateFileW(
            VirtualCameraProtocol::kPipeName,
            GENERIC_READ | GENERIC_WRITE,
            0,
            nullptr,
            OPEN_EXISTING,
            0,
            nullptr);
        if (wake != INVALID_HANDLE_VALUE) {
            CloseHandle(wake);
        }

        if (m_pipeThread.joinable()) {
            m_pipeThread.join();
        }
    }

    void updateFrame(const QImage& frame) {
        if (frame.isNull()) {
            return;
        }
        QMutexLocker lock(&m_owner->m_mutex);
        m_owner->m_latestFrame = frame;
    }

private:
    void pipeLoop() {
        while (!m_stopPipe.load()) {
            PSECURITY_DESCRIPTOR descriptor = nullptr;
            SECURITY_ATTRIBUTES security = makePipeSecurity(&descriptor);
            if (!security.lpSecurityDescriptor) {
                if (m_owner) {
                    m_owner->setError(QStringLiteral(
                        "Failed to create virtual camera pipe security descriptor."));
                }
                return;
            }

            HANDLE pipe = CreateNamedPipeW(
                VirtualCameraProtocol::kPipeName,
                PIPE_ACCESS_DUPLEX,
                PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
                1,
                8 * 1024 * 1024,
                8 * 1024 * 1024,
                0,
                &security);

            LocalFree(descriptor);

            if (pipe == INVALID_HANDLE_VALUE) {
                if (m_owner) {
                    m_owner->setError(QStringLiteral(
                        "CreateNamedPipe failed: %1")
                        .arg(static_cast<quint32>(GetLastError()), 8, 16,
                             QLatin1Char('0')));
                }
                return;
            }

            const BOOL connected =
                ConnectNamedPipe(pipe, nullptr)
                    ? TRUE
                    : (GetLastError() == ERROR_PIPE_CONNECTED);

            if (!connected) {
                CloseHandle(pipe);
                continue;
            }

            while (!m_stopPipe.load()) {
                char request = 0;
                DWORD read = 0;
                if (!ReadFile(pipe, &request, 1, &read, nullptr) ||
                    read != 1) {
                    break;
                }
                if (request != 1) {
                    break;
                }

                QImage frame;
                {
                    QMutexLocker lock(&m_owner->m_mutex);
                    frame = m_owner->m_latestFrame;
                }

                if (frame.isNull()) {
                    frame = QImage(
                        static_cast<int>(VirtualCameraProtocol::kWidth),
                        static_cast<int>(VirtualCameraProtocol::kHeight),
                        QImage::Format_ARGB32_Premultiplied);
                    frame.fill(Qt::black);
                } else if (frame.size() != QSize(
                               static_cast<int>(VirtualCameraProtocol::kWidth),
                               static_cast<int>(VirtualCameraProtocol::kHeight))) {
                    frame = frame.scaled(
                        static_cast<int>(VirtualCameraProtocol::kWidth),
                        static_cast<int>(VirtualCameraProtocol::kHeight),
                        Qt::IgnoreAspectRatio,
                        Qt::FastTransformation);
                }

                frame = frame.convertToFormat(QImage::Format_ARGB32);

                VirtualCameraProtocol::FrameHeader header{};
                std::copy(
                    std::begin(VirtualCameraProtocol::kMagic),
                    std::end(VirtualCameraProtocol::kMagic),
                    std::begin(header.magic));
                header.version = VirtualCameraProtocol::kVersion;
                header.width = VirtualCameraProtocol::kWidth;
                header.height = VirtualCameraProtocol::kHeight;
                header.stride = VirtualCameraProtocol::kStride;
                header.dataBytes = VirtualCameraProtocol::kBgraBytes;
                header.timestamp100ns =
                    static_cast<std::int64_t>(
                        QDateTime::currentMSecsSinceEpoch()) * 10000LL;

                if (!writeAll(pipe, &header, sizeof(header))) {
                    break;
                }

                bool writeOk = true;
                for (int y = 0;
                     y < static_cast<int>(VirtualCameraProtocol::kHeight);
                     ++y) {
                    if (!writeAll(
                            pipe,
                            frame.constScanLine(y),
                            VirtualCameraProtocol::kStride)) {
                        writeOk = false;
                        break;
                    }
                }
                if (!writeOk) {
                    break;
                }
            }

            DisconnectNamedPipe(pipe);
            CloseHandle(pipe);
        }
    }

    VirtualCameraManager* m_owner = nullptr;
    std::atomic<bool> m_stopPipe{true};
    std::thread m_pipeThread;
};

#endif

namespace WeaR {

VirtualCameraManager& VirtualCameraManager::instance() {
    static VirtualCameraManager manager;
    return manager;
}

VirtualCameraManager::VirtualCameraManager(QObject* parent)
    : QObject(parent)
#ifdef Q_OS_WIN
    , m_impl(std::make_unique<Impl>(this))
#else
    , m_impl(nullptr)
#endif
{
}

VirtualCameraManager::~VirtualCameraManager() {
    stop();
}

bool VirtualCameraManager::start() {
#ifdef Q_OS_WIN
    if (m_running.load()) {
        return true;
    }

    HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool uninitializeCom =
        SUCCEEDED(comResult) || comResult == S_FALSE;
    if (FAILED(comResult) && comResult != RPC_E_CHANGED_MODE) {
        setError(QStringLiteral(
            "COM initialization failed: 0x%1")
            .arg(static_cast<quint32>(comResult), 8, 16, QLatin1Char('0')));
        return false;
    }

    const HRESULT mfResult = MFStartup(MF_VERSION);
    if (FAILED(mfResult)) {
        if (uninitializeCom) {
            CoUninitialize();
        }
        setError(QStringLiteral(
            "Media Foundation startup failed: 0x%1")
            .arg(static_cast<quint32>(mfResult), 8, 16, QLatin1Char('0')));
        return false;
    }

    if (!m_impl->startPipeServer()) {
        MFShutdown();
        if (uninitializeCom) {
            CoUninitialize();
        }
        return false;
    }

    IMFVirtualCamera* rawCamera = nullptr;
    const HRESULT createResult = MFCreateVirtualCamera(
        MFVirtualCameraType_SoftwareCameraSource,
        MFVirtualCameraLifetime_Session,
        MFVirtualCameraAccess_CurrentUser,
        L"WeaR Studio",
        kVirtualCameraClsid,
        nullptr,
        0,
        &rawCamera);

    if (FAILED(createResult) || !rawCamera) {
        m_impl->stopPipeServer();
        MFShutdown();
        if (uninitializeCom) {
            CoUninitialize();
        }
        setError(QStringLiteral(
            "MFCreateVirtualCamera failed: 0x%1. "
            "Make sure the WeaR virtual-camera media-source DLL is registered.")
            .arg(static_cast<quint32>(createResult), 8, 16, QLatin1Char('0')));
        return false;
    }

    m_vcam.reset(rawCamera);

    const HRESULT startResult = m_vcam->Start(nullptr);
    if (FAILED(startResult)) {
        m_vcam.reset();
        m_impl->stopPipeServer();
        MFShutdown();
        if (uninitializeCom) {
            CoUninitialize();
        }
        setError(QStringLiteral(
            "Virtual camera start failed: 0x%1")
            .arg(static_cast<quint32>(startResult), 8, 16, QLatin1Char('0')));
        return false;
    }

    m_running.store(true);
    m_comInitialized = uninitializeCom;
    emit stateChanged(true);

    qDebug() << "Virtual camera started:" << deviceName();
    return true;
#else
    setError(QStringLiteral("Virtual Camera requires Windows 11."));
    return false;
#endif
}

void VirtualCameraManager::stop() {
#ifdef Q_OS_WIN
    if (!m_running.exchange(false)) {
        return;
    }

    if (m_vcam) {
        // Session lifetime makes Remove the correct cleanup operation.
        m_vcam->Remove();
        m_vcam.reset();
    }

    if (m_impl) {
        m_impl->stopPipeServer();
    }

    MFShutdown();
    if (m_comInitialized) {
        CoUninitialize();
        m_comInitialized = false;
    }

    emit stateChanged(false);
#endif
}

QString VirtualCameraManager::lastError() const {
    QMutexLocker lock(&m_mutex);
    return m_lastError;
}

void VirtualCameraManager::pushFrame(const QImage& frame) {
#ifdef Q_OS_WIN
    if (m_running.load() && m_impl) {
        m_impl->updateFrame(frame);
    }
#else
    Q_UNUSED(frame);
#endif
}

void VirtualCameraManager::setError(const QString& error) {
    {
        QMutexLocker lock(&m_mutex);
        m_lastError = error;
    }

    qWarning() << "Virtual camera:" << error;
    emit errorOccurred(error);
}

} // namespace WeaR
