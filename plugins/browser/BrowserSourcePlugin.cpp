// =============================================================================
// WeaR-studio Browser Source implementation
// =============================================================================

#include "BrowserSourcePlugin.h"
#include "BrowserSourceRuntime.h"

#ifdef WEAR_ENABLE_CEF

#include "include/cef_app.h"
#include "include/cef_browser.h"
#include "include/cef_client.h"
#include "include/cef_render_handler.h"
#include "include/cef_string.h"

#include <QComboBox>
#include <QDateTime>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <condition_variable>
#include <mutex>

namespace WeaR {
namespace {

class BrowserRenderHandler final : public CefRenderHandler {
public:
    explicit BrowserRenderHandler(BrowserSourcePlugin* owner)
        : m_owner(owner) {}

    bool GetViewRect(
        CefRefPtr<CefBrowser> /*browser*/,
        CefRect& rect) override {
        const QSize size = m_owner->outputResolution();
        if (!size.isValid()) {
            return false;
        }
        rect = CefRect(0, 0, size.width(), size.height());
        return true;
    }

    void OnPaint(
        CefRefPtr<CefBrowser> /*browser*/,
        PaintElementType type,
        const RectList& /*dirtyRects*/,
        const void* buffer,
        int width,
        int height) override {
        if (type != PET_VIEW || !buffer || width <= 0 || height <= 0) {
            return;
        }

        // On Windows the CEF OSR buffer is BGRA with an upper-left origin.
        // QImage::Format_ARGB32 has the same byte ordering on little-endian
        // Windows, so the framebuffer can be wrapped without channel shuffling.
        const QImage image(
            static_cast<const uchar*>(buffer),
            width,
            height,
            width * 4,
            QImage::Format_ARGB32);

        if (m_owner) {
            m_owner->publishFrame(
                image.copy(),
                QDateTime::currentMSecsSinceEpoch() * 1000);
        }
    }

private:
    BrowserSourcePlugin* m_owner = nullptr;

    IMPLEMENT_REFCOUNTING(BrowserRenderHandler);
};

class BrowserClient final
    : public CefClient
    , public CefLifeSpanHandler {
public:
    explicit BrowserClient(CefRefPtr<BrowserRenderHandler> renderHandler)
        : m_renderHandler(std::move(renderHandler)) {}

    CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override {
        return this;
    }

    CefRefPtr<CefRenderHandler> GetRenderHandler() override {
        return m_renderHandler;
    }

    void OnAfterCreated(CefRefPtr<CefBrowser> browser) override {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_browser = browser;
        }
        m_created.notify_all();
    }

    void OnBeforeClose(CefRefPtr<CefBrowser> /*browser*/) override {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_browser = nullptr;
        }
        m_closed.notify_all();
    }

    bool waitForBrowser(int timeoutMs) {
        std::unique_lock<std::mutex> lock(m_mutex);
        return m_created.wait_for(
            lock,
            std::chrono::milliseconds(timeoutMs),
            [this]() { return m_browser != nullptr; });
    }

    void closeAndWait(int timeoutMs) {
        CefRefPtr<CefBrowser> browser;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            browser = m_browser;
        }

        if (browser) {
            browser->GetHost()->CloseBrowser(true);
        }

        std::unique_lock<std::mutex> lock(m_mutex);
        m_closed.wait_for(
            lock,
            std::chrono::milliseconds(timeoutMs),
            [this]() { return m_browser == nullptr; });
    }

    bool hasBrowser() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_browser != nullptr;
    }

private:
    CefRefPtr<BrowserRenderHandler> m_renderHandler;

    mutable std::mutex m_mutex;
    std::condition_variable m_created;
    std::condition_variable m_closed;
    CefRefPtr<CefBrowser> m_browser;

    IMPLEMENT_REFCOUNTING(BrowserClient);
};

} // namespace

class BrowserSourcePlugin::Impl {
public:
    explicit Impl(BrowserSourcePlugin* owner)
        : m_owner(owner) {}

    ~Impl() {
        stop();
    }

    bool start(const QString& url, const QSize& size) {
        stop();

        if (!BrowserSourceRuntime::isInitialized()) {
            return false;
        }

        m_renderHandler = new BrowserRenderHandler(m_owner);
        m_client = new BrowserClient(m_renderHandler);

        CefWindowInfo windowInfo;
        windowInfo.SetAsWindowless(nullptr);

        CefBrowserSettings browserSettings;
        browserSettings.windowless_frame_rate = 60;

        if (!CefBrowserHost::CreateBrowser(
                windowInfo,
                m_client,
                CefString(url.toStdString()),
                browserSettings,
                nullptr,
                nullptr)) {
            m_client = nullptr;
            m_renderHandler = nullptr;
            return false;
        }

        m_size = size;
        return true;
    }

    void stop() {
        if (m_client) {
            m_client->closeAndWait(2000);
        }
        m_client = nullptr;
        m_renderHandler = nullptr;
    }

    bool running() const {
        return m_client && m_client->hasBrowser();
    }

private:
    BrowserSourcePlugin* m_owner = nullptr;
    CefRefPtr<BrowserRenderHandler> m_renderHandler;
    CefRefPtr<BrowserClient> m_client;
    QSize m_size;
};

BrowserSourcePlugin::BrowserSourcePlugin(QObject* parent)
    : QObject(parent),
      m_impl(std::make_unique<Impl>(this)) {
    m_config.resolution = QSize(1280, 720);
    m_config.fps = 60.0;
    m_config.useHardwareAcceleration = true;
    m_url = QStringLiteral("https://example.com");
}

BrowserSourcePlugin::~BrowserSourcePlugin() {
    shutdown();
}

PluginInfo BrowserSourcePlugin::info() const {
    return PluginInfo{
        .id = QStringLiteral("wear.source.browser"),
        .name = QStringLiteral("Browser Source"),
        .description = QStringLiteral(
            "CEF Chromium Windowless/Off-Screen web page source"),
        .version = QStringLiteral("0.1"),
        .author = QStringLiteral("WeaR-studio"),
        .website = QStringLiteral("https://github.com/RidTheWann/WeaR-studio"),
        .type = PluginType::Source,
        .capabilities = capabilities()
    };
}

PluginCapability BrowserSourcePlugin::capabilities() const {
    return PluginCapability::HasVideo |
           PluginCapability::HasSettings |
           PluginCapability::HasPreview |
           PluginCapability::SupportsAsync |
           PluginCapability::ThreadSafe;
}

bool BrowserSourcePlugin::initialize() {
    if (m_initialized.exchange(true)) {
        return true;
    }

    if (!BrowserSourceRuntime::isInitialized()) {
        m_initialized.store(false);
        setError(BrowserSourceRuntime::lastError());
        return false;
    }

    return true;
}

void BrowserSourcePlugin::shutdown() {
    stop();
    m_initialized.store(false);
}

bool BrowserSourcePlugin::isActive() const {
    return m_initialized.load();
}

bool BrowserSourcePlugin::configure(const SourceConfig& config) {
    if (m_running.load()) {
        setError(QStringLiteral(
            "Stop the Browser Source before changing its configuration."));
        return false;
    }

    QMutexLocker lock(&m_mutex);
    m_config = config;
    if (!m_config.resolution.isValid()) {
        m_config.resolution = QSize(1280, 720);
    }
    if (m_config.fps <= 0.0) {
        m_config.fps = 60.0;
    }
    return true;
}

SourceConfig BrowserSourcePlugin::config() const {
    QMutexLocker lock(&m_mutex);
    return m_config;
}

bool BrowserSourcePlugin::start() {
    if (!m_initialized.load() && !initialize()) {
        return false;
    }

    if (m_running.exchange(true)) {
        return true;
    }

    SourceConfig configCopy;
    QString url;
    {
        QMutexLocker lock(&m_mutex);
        configCopy = m_config;
        url = m_url.trimmed();
        m_lastError.clear();
    }

    if (url.isEmpty()) {
        m_running.store(false);
        setError(QStringLiteral("Browser URL must not be empty."));
        return false;
    }

    if (!m_impl->start(url, configCopy.resolution)) {
        m_running.store(false);
        setError(QStringLiteral(
            "Failed to create the CEF Browser Source."));
        return false;
    }

    return true;
}

void BrowserSourcePlugin::stop() {
    if (!m_running.exchange(false)) {
        return;
    }
    m_impl->stop();
}

bool BrowserSourcePlugin::isRunning() const {
    return m_running.load() && m_impl->running();
}

VideoFrame BrowserSourcePlugin::captureVideoFrame() {
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

QSize BrowserSourcePlugin::nativeResolution() const {
    QMutexLocker lock(&m_mutex);
    return m_config.resolution;
}

double BrowserSourcePlugin::nativeFps() const {
    QMutexLocker lock(&m_mutex);
    return m_config.fps;
}

QSize BrowserSourcePlugin::outputResolution() const {
    QMutexLocker lock(&m_mutex);
    return m_config.resolution;
}

double BrowserSourcePlugin::outputFps() const {
    QMutexLocker lock(&m_mutex);
    return m_config.fps;
}

QWidget* BrowserSourcePlugin::settingsWidget() {
    auto* widget = new QWidget();
    auto* layout = new QVBoxLayout(widget);

    auto* title = new QLabel(
        QStringLiteral("Browser Source Settings"), widget);
    title->setStyleSheet(QStringLiteral("font-weight: 600;"));
    layout->addWidget(title);

    auto* urlEdit = new QLineEdit(widget);
    {
        QMutexLocker lock(&m_mutex);
        urlEdit->setText(m_url);
    }
    urlEdit->setPlaceholderText(QStringLiteral("https://example.com"));
    layout->addWidget(new QLabel(QStringLiteral("URL:"), widget));
    layout->addWidget(urlEdit);

    auto* widthSpin = new QSpinBox(widget);
    widthSpin->setRange(160, 7680);
    widthSpin->setValue(outputResolution().width());

    auto* heightSpin = new QSpinBox(widget);
    heightSpin->setRange(120, 4320);
    heightSpin->setValue(outputResolution().height());

    auto* fpsSpin = new QSpinBox(widget);
    fpsSpin->setRange(1, 60);
    fpsSpin->setValue(static_cast<int>(outputFps()));

    auto* form = new QFormLayout();
    form->addRow(QStringLiteral("Width:"), widthSpin);
    form->addRow(QStringLiteral("Height:"), heightSpin);
    form->addRow(QStringLiteral("FPS:"), fpsSpin);
    layout->addLayout(form);

    const auto apply = [
        this, urlEdit, widthSpin, heightSpin, fpsSpin
    ] {
        SourceConfig next = config();
        next.resolution = QSize(widthSpin->value(), heightSpin->value());
        next.fps = fpsSpin->value();

        if (!configure(next)) {
            return;
        }

        QMutexLocker lock(&m_mutex);
        m_url = urlEdit->text().trimmed();
    };

    auto* applyButton = new QPushButton(
        QStringLiteral("Apply"), widget);
    connect(applyButton, &QPushButton::clicked, widget, apply);
    layout->addWidget(applyButton);
    layout->addStretch();

    return widget;
}

QString BrowserSourcePlugin::lastError() const {
    QMutexLocker lock(&m_mutex);
    return m_lastError;
}

void BrowserSourcePlugin::publishFrame(
    const QImage& frame,
    int64_t timestamp) {
    if (frame.isNull()) {
        return;
    }

    {
        QMutexLocker lock(&m_mutex);
        m_currentFrame = frame;
        m_currentTimestamp = timestamp;
        m_frameNumber.fetch_add(1);
    }
}

void BrowserSourcePlugin::setError(const QString& error) {
    QMutexLocker lock(&m_mutex);
    m_lastError = error;
}

} // namespace WeaR

#else

namespace WeaR {

class BrowserSourcePlugin::Impl {
public:
    explicit Impl(BrowserSourcePlugin*) {}
    bool start(const QString&, const QSize&) { return false; }
    void stop() {}
    bool running() const { return false; }
};

BrowserSourcePlugin::BrowserSourcePlugin(QObject* parent)
    : QObject(parent),
      m_impl(std::make_unique<Impl>(this)) {
    m_config.resolution = QSize(1280, 720);
    m_config.fps = 60.0;
    m_url = QStringLiteral("https://example.com");
}

BrowserSourcePlugin::~BrowserSourcePlugin() {
    shutdown();
}

PluginInfo BrowserSourcePlugin::info() const {
    return PluginInfo{
        .id = QStringLiteral("wear.source.browser"),
        .name = QStringLiteral("Browser Source"),
        .description = QStringLiteral("CEF support is disabled"),
        .version = QStringLiteral("0.1"),
        .author = QStringLiteral("WeaR-studio"),
        .website = QStringLiteral("https://github.com/RidTheWann/WeaR-studio"),
        .type = PluginType::Source,
        .capabilities = PluginCapability::HasSettings
    };
}

PluginCapability BrowserSourcePlugin::capabilities() const {
    return PluginCapability::HasSettings;
}

bool BrowserSourcePlugin::initialize() {
    setError(QStringLiteral(
        "Build with WEAR_ENABLE_CEF=ON and CEF_ROOT to enable Browser Source."));
    m_initialized.store(false);
    return false;
}

void BrowserSourcePlugin::shutdown() {
    stop();
    m_initialized.store(false);
}

bool BrowserSourcePlugin::isActive() const {
    return m_initialized.load();
}

bool BrowserSourcePlugin::configure(const SourceConfig& config) {
    QMutexLocker lock(&m_mutex);
    m_config = config;
    return true;
}

SourceConfig BrowserSourcePlugin::config() const {
    QMutexLocker lock(&m_mutex);
    return m_config;
}

bool BrowserSourcePlugin::start() {
    setError(QStringLiteral("CEF is not enabled in this build."));
    return false;
}

void BrowserSourcePlugin::stop() {
    m_running.store(false);
}

bool BrowserSourcePlugin::isRunning() const {
    return false;
}

VideoFrame BrowserSourcePlugin::captureVideoFrame() {
    return {};
}

QSize BrowserSourcePlugin::nativeResolution() const {
    QMutexLocker lock(&m_mutex);
    return m_config.resolution;
}

double BrowserSourcePlugin::nativeFps() const {
    QMutexLocker lock(&m_mutex);
    return m_config.fps;
}

QSize BrowserSourcePlugin::outputResolution() const {
    QMutexLocker lock(&m_mutex);
    return m_config.resolution;
}

double BrowserSourcePlugin::outputFps() const {
    QMutexLocker lock(&m_mutex);
    return m_config.fps;
}

QWidget* BrowserSourcePlugin::settingsWidget() {
    return nullptr;
}

QString BrowserSourcePlugin::lastError() const {
    QMutexLocker lock(&m_mutex);
    return m_lastError;
}

void BrowserSourcePlugin::publishFrame(
    const QImage& frame,
    int64_t timestamp) {
    if (frame.isNull()) {
        return;
    }
    QMutexLocker lock(&m_mutex);
    m_currentFrame = frame;
    m_currentTimestamp = timestamp;
    m_frameNumber.fetch_add(1);
}

void BrowserSourcePlugin::setError(const QString& error) {
    QMutexLocker lock(&m_mutex);
    m_lastError = error;
}

} // namespace WeaR

#endif
