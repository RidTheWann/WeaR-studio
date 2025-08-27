#pragma once
#include <QObject>

class CaptureManager : public QObject {
    Q_OBJECT
public:
    explicit CaptureManager(QObject* parent = nullptr);
    bool startScreenCapture();
    bool startWebcamCapture();
    bool startAudioCapture();
    void stopAll();
};
