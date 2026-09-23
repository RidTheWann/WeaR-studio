#pragma once
// =============================================================================
// WeaR-studio Virtual Camera Manager
// Windows 11 Media Foundation Virtual Camera integration.
// =============================================================================

#include <QObject>
#include <QImage>
#include <QMutex>
#include <QString>

#include <atomic>
#include <memory>

namespace WeaR {

class VirtualCameraManager final : public QObject {
    Q_OBJECT

public:
    static VirtualCameraManager& instance();

    VirtualCameraManager(const VirtualCameraManager&) = delete;
    VirtualCameraManager& operator=(const VirtualCameraManager&) = delete;

    ~VirtualCameraManager() override;

    bool start();
    void stop();
    bool isRunning() const { return m_running.load(); }
    QString lastError() const;

    // Called once per composited render frame. This only swaps a shared QImage;
    // the named-pipe server performs all cross-process I/O on its own thread.
    void pushFrame(const QImage& frame);

    QString deviceName() const {
        return QStringLiteral("WeaR Studio Virtual Camera");
    }

signals:
    void stateChanged(bool running);
    void errorOccurred(const QString& error);

private:
    explicit VirtualCameraManager(QObject* parent = nullptr);

    class Impl;
    std::unique_ptr<Impl> m_impl;

    mutable QMutex m_mutex;
    QImage m_latestFrame;
    QString m_lastError;
    std::atomic<bool> m_running{false};

    void setError(const QString& error);
};

} // namespace WeaR
