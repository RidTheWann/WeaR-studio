#pragma once
// =============================================================================
// WeaR-studio Browser Source
// CEF Windowless/Off-Screen Rendering source.
// =============================================================================

#include <ISource.h>

#include <QObject>
#include <QImage>
#include <QMutex>

#include <atomic>
#include <memory>

namespace WeaR {

class BrowserSourcePlugin final : public QObject, public ISource {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID WEAR_SOURCE_IID FILE "BrowserSourcePlugin.json")
    Q_INTERFACES(WeaR::ISource)

public:
    explicit BrowserSourcePlugin(QObject* parent = nullptr);
    ~BrowserSourcePlugin() override;

    PluginInfo info() const override;
    QString name() const override { return QStringLiteral("Browser Source"); }
    QString version() const override { return QStringLiteral("0.1"); }
    PluginType type() const override { return PluginType::Source; }
    PluginCapability capabilities() const override;

    bool initialize() override;
    void shutdown() override;
    bool isActive() const override;

    bool configure(const SourceConfig& config) override;
    SourceConfig config() const override;

    bool start() override;
    void stop() override;
    bool isRunning() const override;

    VideoFrame captureVideoFrame() override;

    QSize nativeResolution() const override;
    double nativeFps() const override;
    QSize outputResolution() const override;
    double outputFps() const override;

    QWidget* settingsWidget() override;
    QString lastError() const override;

    // Called by the CEF OSR render callback. Thread-safe latest-frame handoff.
    void publishFrame(const QImage& frame, int64_t timestamp);

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;

    mutable QMutex m_mutex;
    SourceConfig m_config;
    QString m_url;
    QString m_lastError;

    std::atomic<bool> m_initialized{false};
    std::atomic<bool> m_running{false};
    std::atomic<int64_t> m_frameNumber{0};

    QImage m_currentFrame;
    int64_t m_currentTimestamp = 0;

    void setError(const QString& error);

    Q_DISABLE_COPY_MOVE(BrowserSourcePlugin)
};

} // namespace WeaR
