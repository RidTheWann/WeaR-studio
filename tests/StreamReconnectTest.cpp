#include "StreamManager.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QMutex>
#include <QMutexLocker>
#include <QThread>
#include <QDebug>

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    auto& stream = WeaR::StreamManager::instance();

    WeaR::StreamSettings settings;
    settings.url = QStringLiteral("rtmp://127.0.0.1:1/wear-reconnect-test");
    settings.connectTimeout = 1;
    settings.reconnectDelay = 1;
    settings.reconnectMaxDelay = 4;
    settings.maxReconnectAttempts = 3;
    settings.audioEnabled = false;

    if (!stream.configure(settings)) {
        qCritical() << "Could not configure StreamManager";
        return 1;
    }

    QMutex mutex;
    QList<int> attempts;
    QList<qint64> timestamps;
    QElapsedTimer timer;
    timer.start();

    QObject::connect(
        &stream,
        &WeaR::StreamManager::reconnecting,
        &app,
        [&](int attempt) {
            QMutexLocker lock(&mutex);
            attempts.append(attempt);
            timestamps.append(timer.elapsed());
            qInfo() << "Reconnect attempt" << attempt
                    << "at" << timestamps.back() << "ms";
        },
        Qt::DirectConnection);

    if (!stream.startStream()) {
        qCritical() << "StreamManager failed to start worker";
        return 1;
    }

    const QElapsedTimer waitTimer = [] {
        QElapsedTimer t;
        t.start();
        return t;
    }();

    while (waitTimer.elapsed() < 6000) {
        {
            QMutexLocker lock(&mutex);
            if (attempts.size() >= 3) {
                break;
            }
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
        QThread::msleep(10);
    }

    stream.stopStream();

    QMutexLocker lock(&mutex);
    if (attempts.size() < 3) {
        qCritical() << "Expected three reconnect attempts, got"
                    << attempts.size();
        return 1;
    }

    if (attempts.at(0) != 1 ||
        attempts.at(1) != 2 ||
        attempts.at(2) != 3) {
        qCritical() << "Reconnect attempt sequence mismatch:" << attempts;
        return 1;
    }

    const qint64 firstInterval = timestamps.at(1) - timestamps.at(0);
    const qint64 secondInterval = timestamps.at(2) - timestamps.at(1);

    // Base delay is 1s, then 2s. Allow CI/network scheduling margin while
    // still requiring the second backoff interval to be materially longer.
    if (firstInterval < 700 || firstInterval > 4500) {
        qCritical() << "Unexpected first backoff interval:" << firstInterval;
        return 1;
    }

    if (secondInterval < firstInterval + 500 ||
        secondInterval > firstInterval + 4000) {
        qCritical() << "Exponential backoff was not observed:"
                    << "first=" << firstInterval
                    << "second=" << secondInterval;
        return 1;
    }

    qDebug() << "STREAM_RECONNECT: PASS"
             << "attempts=" << attempts
             << "first_interval_ms=" << firstInterval
             << "second_interval_ms=" << secondInterval;
    return 0;
}
