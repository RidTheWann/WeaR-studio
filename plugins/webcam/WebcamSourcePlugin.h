#pragma once
// =============================================================================
// WeaR-studio Webcam Source
// Windows Media Foundation webcam capture with non-blocking latest-frame API.
// =============================================================================
#include <ISource.h>
#include <QObject>
#include <QMutex>
#include <QImage>
#include <QStringList>
#include <atomic>
#include <memory>
#include <thread>

namespace WeaR {
class WebcamSourcePlugin final : public QObject, public ISource {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID WEAR_SOURCE_IID FILE "WebcamSourcePlugin.json")
    Q_INTERFACES(WeaR::ISource)
public:
    explicit WebcamSourcePlugin(QObject* parent = nullptr);
    ~WebcamSourcePlugin() override;
    PluginInfo info() const override;
    QString name() const override { return QStringLiteral("Webcam"); }
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
    QStringList availableDevices() const override;
    QWidget* settingsWidget() override;
    QString lastError() const override;
signals:
    void frameCaptured(int64_t timestamp);
    void captureError(const QString& error);
private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
    mutable QMutex m_mutex;
    SourceConfig m_config;
    QString m_selectedDeviceId;
    QString m_lastError;
    std::atomic<bool> m_initialized{false};
    std::atomic<bool> m_running{false};
    std::atomic<int64_t> m_frameNumber{0};
    QImage m_currentFrame;
    int64_t m_currentTimestamp = 0;
    QSize m_nativeResolution;
    double m_nativeFps = 30.0;
    void setError(const QString& error);
    void publishFrame(const QImage& frame, int64_t timestamp);
    Q_DISABLE_COPY_MOVE(WebcamSourcePlugin)
};
} // namespace WeaR
