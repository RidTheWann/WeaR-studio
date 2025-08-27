#pragma once
#include <QObject>

class StreamManager : public QObject {
    Q_OBJECT
public:
    explicit StreamManager(QObject* parent = nullptr);
    bool startStreaming();
    void stopStreaming();
};
