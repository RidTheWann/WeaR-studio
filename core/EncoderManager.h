#pragma once
#include <QObject>

class EncoderManager : public QObject {
    Q_OBJECT
public:
    explicit EncoderManager(QObject* parent = nullptr);
    bool startEncoding();
    void stopEncoding();
};
