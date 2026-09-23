#include "WebcamSourcePlugin.h"

#ifdef Q_OS_WIN
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>

#include <QComboBox>
#include <QDateTime>
#include <QDebug>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <wrl/client.h>
#include <windows.h>
#include <combaseapi.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <mutex>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mf.lib")

namespace {
using Microsoft::WRL::ComPtr;

struct DeviceInfo {
    QString id;
    QString name;
};

QString hresultString(HRESULT hr) {
    return QStringLiteral("0x%1")
        .arg(static_cast<quint32>(hr), 8, 16, QLatin1Char('0'))
        .toUpper();
}

QString readStringAttribute(IMFAttributes* attributes, REFGUID key) {
    if (!attributes) return {};
    WCHAR* value = nullptr;
    UINT32 length = 0;
    if (FAILED(attributes->GetAllocatedString(key, &value, &length)) || !value) {
        return {};
    }
    const QString result = QString::fromWCharArray(value, static_cast<int>(length));
    CoTaskMemFree(value);
    return result;
}

QList<DeviceInfo> enumerateWebcams() {
    QList<DeviceInfo> devices;
    ComPtr<IMFAttributes> attributes;
    if (FAILED(MFCreateAttributes(&attributes, 1))) return devices;
    if (FAILED(attributes->SetGUID(
            MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
            MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID))) return devices;

    IMFActivate** activatedDevices = nullptr;
    UINT32 count = 0;
    if (FAILED(MFEnumDeviceSources(attributes.Get(), &activatedDevices, &count))) {
        return devices;
    }

    for (UINT32 index = 0; index < count; ++index) {
        if (!activatedDevices[index]) continue;
        DeviceInfo device;
        device.name = readStringAttribute(
            activatedDevices[index], MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME);
        device.id = readStringAttribute(
            activatedDevices[index],
            MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK);
        if (device.name.isEmpty()) device.name = QStringLiteral("Webcam %1").arg(index + 1);
        if (device.id.isEmpty()) device.id = device.name;
        devices.append(device);
        activatedDevices[index]->Release();
    }

    CoTaskMemFree(activatedDevices);
    return devices;
}

ComPtr<IMFActivate> findDevice(const QString& wantedId) {
    ComPtr<IMFAttributes> attributes;
    if (FAILED(MFCreateAttributes(&attributes, 1))) return nullptr;
    if (FAILED(attributes->SetGUID(
            MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
            MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID))) return nullptr;

    IMFActivate** activatedDevices = nullptr;
    UINT32 count = 0;
    if (FAILED(MFEnumDeviceSources(attributes.Get(), &activatedDevices, &count))) {
        return nullptr;
    }

    ComPtr<IMFActivate> selected;
    const QList<DeviceInfo> devices = enumerateWebcams();

    UINT32 selectedIndex = 0;
    if (!wantedId.isEmpty()) {
        selectedIndex = count;
        for (UINT32 i = 0;
             i < count && i < static_cast<UINT32>(devices.size());
             ++i) {
            if (devices.at(static_cast<int>(i)).id == wantedId) {
                selectedIndex = i;
                break;
            }
        }
    }

    if (selectedIndex < count && activatedDevices[selectedIndex]) {
        selected = activatedDevices[selectedIndex];
        selected->AddRef();
    }

    for (UINT32 index = 0; index < count; ++index) {
        if (activatedDevices[index]) activatedDevices[index]->Release();
    }
    CoTaskMemFree(activatedDevices);
    return selected;
}

bool setRequestedOutputType(
    IMFSourceReader* reader,
    const WeaR::SourceConfig& config) {
    if (!reader) return false;

    auto makeRgb32Type = [&](bool includeRequestedMode) -> ComPtr<IMFMediaType> {
        ComPtr<IMFMediaType> outputType;
        if (FAILED(MFCreateMediaType(&outputType))) {
            return nullptr;
        }

        if (FAILED(outputType->SetGUID(
                MF_MT_MAJOR_TYPE, MFMediaType_Video))) {
            return nullptr;
        }
        if (FAILED(outputType->SetGUID(
                MF_MT_SUBTYPE, MFVideoFormat_RGB32))) {
            return nullptr;
        }

        if (includeRequestedMode) {
            const QSize resolution =
                config.resolution.isValid()
                    ? config.resolution
                    : QSize(1280, 720);

            if (FAILED(MFSetAttributeSize(
                    outputType.Get(),
                    MF_MT_FRAME_SIZE,
                    static_cast<UINT32>(resolution.width()),
                    static_cast<UINT32>(resolution.height())))) {
                return nullptr;
            }

            if (config.fps > 0.0) {
                const UINT32 numerator =
                    static_cast<UINT32>(std::lround(config.fps * 1000.0));
                if (FAILED(MFSetAttributeRatio(
                        outputType.Get(),
                        MF_MT_FRAME_RATE,
                        numerator,
                        1000))) {
                    return nullptr;
                }
            }
        }

        return outputType;
    };

    // First try the exact requested mode.
    ComPtr<IMFMediaType> requestedType = makeRgb32Type(true);
    if (requestedType &&
        SUCCEEDED(reader->SetCurrentMediaType(
            MF_SOURCE_READER_FIRST_VIDEO_STREAM,
            nullptr,
            requestedType.Get()))) {
        return true;
    }

    // A camera may reject a precise size/FPS combination while still
    // supporting Source Reader's RGB32 conversion. Retry with only RGB32
    // specified so compressed/native camera formats are never copied as if
    // they were raw 4-byte pixels.
    ComPtr<IMFMediaType> rgb32Type = makeRgb32Type(false);
    return rgb32Type &&
           SUCCEEDED(reader->SetCurrentMediaType(
               MF_SOURCE_READER_FIRST_VIDEO_STREAM,
               nullptr,
               rgb32Type.Get()));
}

QString makeCaptureError(const QString& operation, HRESULT hr) {
    return QStringLiteral("%1 failed (%2)").arg(operation, hresultString(hr));
}
} // namespace

namespace WeaR {

class WebcamSourcePlugin::Impl {
public:
    explicit Impl(WebcamSourcePlugin* parent) : m_parent(parent) {}
    ~Impl() { stop(); }

    bool start(const QString& deviceId, const SourceConfig& config) {
        stop();
        m_stopRequested.store(false);
        m_thread = std::thread([this, deviceId, config]() {
            run(deviceId, config);
        });
        return true;
    }

    void stop() {
        m_stopRequested.store(true);
        if (m_thread.joinable()) m_thread.join();
        std::lock_guard<std::mutex> lock(m_readerMutex);
        m_reader.Reset();
        m_mediaSource.Reset();
    }

    bool running() const { return !m_stopRequested.load(); }

    QString lastError() const {
        QMutexLocker lock(&m_errorMutex);
        return m_lastError;
    }

private:
    void setError(const QString& error) {
        {
            QMutexLocker lock(&m_errorMutex);
            m_lastError = error;
        }
        if (m_parent) {
            QMetaObject::invokeMethod(
                m_parent,
                [parent = m_parent, error]() { parent->captureError(error); },
                Qt::QueuedConnection);
        }
    }

    void run(const QString& deviceId, const SourceConfig& config) {
        HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        const bool comInitialized = SUCCEEDED(hr);

        if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
            setError(makeCaptureError(QStringLiteral("COM initialization"), hr));
            m_stopRequested.store(true);
            return;
        }

        ComPtr<IMFActivate> activate = findDevice(deviceId);
        if (!activate) {
            setError(QStringLiteral(
                "No webcam device is available or the selected device is disconnected."));
            if (comInitialized) CoUninitialize();
            m_stopRequested.store(true);
            return;
        }

        ComPtr<IMFMediaSource> mediaSource;
        hr = activate->ActivateObject(IID_PPV_ARGS(&mediaSource));
        if (FAILED(hr)) {
            setError(makeCaptureError(QStringLiteral("Activating webcam"), hr));
            if (comInitialized) CoUninitialize();
            m_stopRequested.store(true);
            return;
        }

        ComPtr<IMFAttributes> readerAttributes;
        hr = MFCreateAttributes(&readerAttributes, 2);
        if (FAILED(hr)) {
            setError(makeCaptureError(
                QStringLiteral("Creating source reader attributes"), hr));
            if (comInitialized) CoUninitialize();
            m_stopRequested.store(true);
            return;
        }

        readerAttributes->SetUINT32(
            MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);

        ComPtr<IMFSourceReader> reader;
        hr = MFCreateSourceReaderFromMediaSource(
            mediaSource.Get(), readerAttributes.Get(), &reader);
        if (FAILED(hr)) {
            setError(makeCaptureError(
                QStringLiteral("Creating webcam source reader"), hr));
            if (comInitialized) CoUninitialize();
            m_stopRequested.store(true);
            return;
        }

        if (!setRequestedOutputType(reader.Get(), config)) {
            qWarning() << "Webcam requested mode rejected; using native/default mode";
        }

        ComPtr<IMFMediaType> currentType;
        reader->GetCurrentMediaType(
            MF_SOURCE_READER_FIRST_VIDEO_STREAM, &currentType);

        UINT32 width = static_cast<UINT32>(
            config.resolution.isValid() ? config.resolution.width() : 1280);
        UINT32 height = static_cast<UINT32>(
            config.resolution.isValid() ? config.resolution.height() : 720);
        UINT32 fpsNumerator = 30;
        UINT32 fpsDenominator = 1;
        LONG defaultStride = static_cast<LONG>(width * 4);

        if (currentType) {
            MFGetAttributeSize(
                currentType.Get(), MF_MT_FRAME_SIZE, &width, &height);
            MFGetAttributeRatio(
                currentType.Get(), MF_MT_FRAME_RATE,
                &fpsNumerator, &fpsDenominator);

            UINT32 strideValue = 0;
            if (SUCCEEDED(currentType->GetUINT32(
                    MF_MT_DEFAULT_STRIDE, &strideValue)) &&
                strideValue != 0) {
                defaultStride = static_cast<LONG>(strideValue);
            }
        }

        if (defaultStride == 0) defaultStride = static_cast<LONG>(width * 4);

        const int stride = std::abs(static_cast<int>(defaultStride));
        const double fps =
            fpsDenominator > 0
                ? static_cast<double>(fpsNumerator) /
                    static_cast<double>(fpsDenominator)
                : 30.0;

        {
            QMutexLocker lock(&m_parent->m_mutex);
            m_parent->m_nativeResolution = QSize(
                static_cast<int>(width), static_cast<int>(height));
            m_parent->m_nativeFps = fps;
        }

        {
            std::lock_guard<std::mutex> lock(m_readerMutex);
            m_reader = reader;
            m_mediaSource = mediaSource;
        }

        reader->SetStreamSelection(
            MF_SOURCE_READER_FIRST_VIDEO_STREAM, TRUE);

        while (!m_stopRequested.load()) {
            DWORD actualStreamIndex = 0;
            DWORD streamFlags = 0;
            LONGLONG timestamp = 0;
            ComPtr<IMFSample> sample;

            hr = reader->ReadSample(
                MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0,
                &actualStreamIndex, &streamFlags, &timestamp, &sample);
            if (FAILED(hr)) {
                setError(makeCaptureError(
                    QStringLiteral("Reading webcam frame"), hr));
                break;
            }

            if (streamFlags & MF_SOURCE_READERF_ENDOFSTREAM) break;
            if (!sample) continue;

            ComPtr<IMFMediaBuffer> buffer;
            hr = sample->ConvertToContiguousBuffer(&buffer);
            if (FAILED(hr)) continue;

            BYTE* data = nullptr;
            DWORD maxLength = 0;
            DWORD currentLength = 0;
            hr = buffer->Lock(&data, &maxLength, &currentLength);
            if (FAILED(hr) || !data) continue;

            const int heightInt = static_cast<int>(height);
            const int widthInt = static_cast<int>(width);
            const size_t requiredBytes =
                static_cast<size_t>(stride) *
                static_cast<size_t>(heightInt);

            if (currentLength >= requiredBytes) {
                QImage frame(
                    widthInt, heightInt, QImage::Format_ARGB32);

                for (int y = 0; y < heightInt; ++y) {
                    const int sourceRow =
                        defaultStride >= 0 ? y : heightInt - 1 - y;
                    const auto* sourceLine =
                        data + static_cast<size_t>(sourceRow) *
                            static_cast<size_t>(stride);
                    std::memcpy(
                        frame.scanLine(y),
                        sourceLine,
                        static_cast<size_t>(widthInt) * 4U);
                }

                m_parent->publishFrame(
                    frame,
                    timestamp > 0
                        ? static_cast<int64_t>(timestamp / 10)
                        : QDateTime::currentMSecsSinceEpoch() * 1000);
            }

            buffer->Unlock();
        }

        {
            std::lock_guard<std::mutex> lock(m_readerMutex);
            m_reader.Reset();
            m_mediaSource.Reset();
        }

        m_stopRequested.store(true);
        if (comInitialized) CoUninitialize();
    }

    WebcamSourcePlugin* m_parent = nullptr;
    std::atomic<bool> m_stopRequested{true};
    std::thread m_thread;
    std::mutex m_readerMutex;
    ComPtr<IMFSourceReader> m_reader;
    ComPtr<IMFMediaSource> m_mediaSource;
    mutable QMutex m_errorMutex;
    QString m_lastError;
};

} // namespace WeaR

#else

namespace WeaR {
class WebcamSourcePlugin::Impl {
public:
    explicit Impl(WebcamSourcePlugin*) {}
    bool start(const QString&, const SourceConfig&) { return false; }
    void stop() {}
    bool running() const { return false; }
    QString lastError() const {
        return QStringLiteral("Webcam source is only available on Windows.");
    }
};
} // namespace WeaR

#endif

namespace WeaR {

WebcamSourcePlugin::WebcamSourcePlugin(QObject* parent)
    : QObject(parent),
      m_impl(std::make_unique<Impl>(this)) {
    m_config.resolution = QSize(1280, 720);
    m_config.fps = 30.0;
    m_config.useHardwareAcceleration = false;
}

WebcamSourcePlugin::~WebcamSourcePlugin() {
    shutdown();
}

PluginInfo WebcamSourcePlugin::info() const {
    return PluginInfo{
        .id = QStringLiteral("wear.source.webcam"),
        .name = QStringLiteral("Webcam"),
        .description = QStringLiteral("Windows Media Foundation webcam video source"),
        .version = QStringLiteral("0.1"),
        .author = QStringLiteral("WeaR-studio"),
        .website = QStringLiteral("https://github.com/RidTheWann/WeaR-studio"),
        .type = PluginType::Source,
        .capabilities = capabilities()
    };
}

PluginCapability WebcamSourcePlugin::capabilities() const {
#ifdef Q_OS_WIN
    return PluginCapability::HasVideo |
           PluginCapability::HasSettings |
           PluginCapability::HasPreview |
           PluginCapability::SupportsAsync |
           PluginCapability::ThreadSafe;
#else
    return PluginCapability::HasSettings;
#endif
}

bool WebcamSourcePlugin::initialize() {
    if (m_initialized.exchange(true)) return true;
#ifdef Q_OS_WIN
    const HRESULT hr = MFStartup(MF_VERSION);
    if (FAILED(hr)) {
        m_initialized.store(false);
        setError(makeCaptureError(
            QStringLiteral("Media Foundation startup"), hr));
        return false;
    }
    return true;
#else
    setError(QStringLiteral("Webcam source requires Windows."));
    m_initialized.store(false);
    return false;
#endif
}

void WebcamSourcePlugin::shutdown() {
    stop();
    if (!m_initialized.exchange(false)) return;
#ifdef Q_OS_WIN
    MFShutdown();
#endif
}

bool WebcamSourcePlugin::isActive() const {
    return m_initialized.load();
}

bool WebcamSourcePlugin::configure(const SourceConfig& config) {
    if (m_running.load()) {
        setError(QStringLiteral(
            "Stop the webcam before changing its configuration."));
        return false;
    }

    QMutexLocker lock(&m_mutex);
    m_config = config;
    if (!m_config.resolution.isValid()) m_config.resolution = QSize(1280, 720);
    if (m_config.fps <= 0.0) m_config.fps = 30.0;
    m_selectedDeviceId = m_config.deviceId;
    return true;
}

SourceConfig WebcamSourcePlugin::config() const {
    QMutexLocker lock(&m_mutex);
    return m_config;
}

bool WebcamSourcePlugin::start() {
    if (!m_initialized.load() && !initialize()) return false;
    if (m_running.exchange(true)) return true;

    SourceConfig configCopy;
    QString deviceId;
    {
        QMutexLocker lock(&m_mutex);
        configCopy = m_config;
        deviceId = m_selectedDeviceId;
        m_lastError.clear();
    }

#ifdef Q_OS_WIN
    if (deviceId.isEmpty()) {
        const QStringList devices = availableDevices();
        if (!devices.isEmpty()) {
            deviceId = devices.first();
            QMutexLocker lock(&m_mutex);
            m_selectedDeviceId = deviceId;
            m_config.deviceId = deviceId;
        }
    }

    if (deviceId.isEmpty()) {
        m_running.store(false);
        setError(QStringLiteral(
            "No webcam was detected. Check Windows Camera privacy permissions and device connectivity."));
        return false;
    }

    if (!m_impl->start(deviceId, configCopy)) {
        m_running.store(false);
        const QString error = m_impl->lastError();
        if (!error.isEmpty()) setError(error);
        return false;
    }

    qDebug() << "Webcam source started:" << deviceId;
    return true;
#else
    m_running.store(false);
    setError(QStringLiteral("Webcam source requires Windows."));
    return false;
#endif
}

void WebcamSourcePlugin::stop() {
    if (!m_running.exchange(false)) return;
    m_impl->stop();
}

bool WebcamSourcePlugin::isRunning() const {
    return m_running.load() && m_impl->running();
}

VideoFrame WebcamSourcePlugin::captureVideoFrame() {
    VideoFrame frame;
    QMutexLocker lock(&m_mutex);
    if (!m_currentFrame.isNull()) {
        frame.softwareFrame = m_currentFrame;
        frame.timestamp = m_currentTimestamp;
        frame.frameNumber = m_frameNumber.load();
        frame.isHardwareFrame = false;
    }
    return frame;
}

QSize WebcamSourcePlugin::nativeResolution() const {
    QMutexLocker lock(&m_mutex);
    return m_nativeResolution.isValid()
        ? m_nativeResolution : m_config.resolution;
}

double WebcamSourcePlugin::nativeFps() const {
    QMutexLocker lock(&m_mutex);
    return m_nativeFps;
}

QSize WebcamSourcePlugin::outputResolution() const {
    QMutexLocker lock(&m_mutex);
    return m_config.resolution;
}

double WebcamSourcePlugin::outputFps() const {
    QMutexLocker lock(&m_mutex);
    return m_config.fps;
}

QStringList WebcamSourcePlugin::availableDevices() const {
#ifdef Q_OS_WIN
    const QList<DeviceInfo> devices = enumerateWebcams();
    QStringList ids;
    ids.reserve(devices.size());
    for (const DeviceInfo& device : devices) ids.append(device.id);
    return ids;
#else
    return {};
#endif
}

QWidget* WebcamSourcePlugin::settingsWidget() {
    auto* widget = new QWidget();
    auto* layout = new QVBoxLayout(widget);

    auto* title = new QLabel(QStringLiteral("Webcam Settings"), widget);
    title->setStyleSheet(QStringLiteral("font-weight: 600;"));
    layout->addWidget(title);

#ifdef Q_OS_WIN
    const QList<DeviceInfo> devices = enumerateWebcams();
#else
    const QList<DeviceInfo> devices;
#endif

    auto* deviceCombo = new QComboBox(widget);
    for (const DeviceInfo& device : devices) {
        deviceCombo->addItem(device.name, device.id);
    }

    {
        QMutexLocker lock(&m_mutex);
        const int currentIndex = deviceCombo->findData(m_selectedDeviceId);
        if (currentIndex >= 0) deviceCombo->setCurrentIndex(currentIndex);
    }

    layout->addWidget(new QLabel(QStringLiteral("Camera:"), widget));
    layout->addWidget(deviceCombo);

    auto* widthSpin = new QSpinBox(widget);
    widthSpin->setRange(160, 7680);
    widthSpin->setSingleStep(16);
    widthSpin->setValue(outputResolution().width());

    auto* heightSpin = new QSpinBox(widget);
    heightSpin->setRange(120, 4320);
    heightSpin->setSingleStep(8);
    heightSpin->setValue(outputResolution().height());

    auto* fpsSpin = new QSpinBox(widget);
    fpsSpin->setRange(1, 240);
    fpsSpin->setValue(static_cast<int>(outputFps()));

    auto* form = new QFormLayout();
    form->addRow(QStringLiteral("Width:"), widthSpin);
    form->addRow(QStringLiteral("Height:"), heightSpin);
    form->addRow(QStringLiteral("FPS:"), fpsSpin);
    layout->addLayout(form);

    const auto apply = [this, deviceCombo, widthSpin, heightSpin, fpsSpin]() {
        SourceConfig next = config();
        next.deviceId = deviceCombo->currentData().toString();
        next.resolution = QSize(widthSpin->value(), heightSpin->value());
        next.fps = fpsSpin->value();
        configure(next);
    };

    auto* applyButton = new QPushButton(QStringLiteral("Apply"), widget);
    connect(applyButton, &QPushButton::clicked, widget, apply);
    layout->addWidget(applyButton);
    layout->addStretch();

    return widget;
}

QString WebcamSourcePlugin::lastError() const {
    QMutexLocker lock(&m_mutex);
    if (!m_lastError.isEmpty()) return m_lastError;
#ifdef Q_OS_WIN
    return m_impl->lastError();
#else
    return QStringLiteral("Webcam source requires Windows.");
#endif
}

void WebcamSourcePlugin::setError(const QString& error) {
    {
        QMutexLocker lock(&m_mutex);
        m_lastError = error;
    }
    qWarning() << "Webcam source:" << error;
}

void WebcamSourcePlugin::publishFrame(
    const QImage& frame,
    int64_t timestamp) {
    if (frame.isNull()) return;
    {
        QMutexLocker lock(&m_mutex);
        m_currentFrame = frame;
        m_currentTimestamp = timestamp;
        m_frameNumber.fetch_add(1);
    }
    emit frameCaptured(timestamp);
}

} // namespace WeaR
