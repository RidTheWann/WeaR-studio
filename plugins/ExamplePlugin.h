#pragma once
#include <QObject>

class ExamplePlugin : public QObject {
    Q_OBJECT
public:
    explicit ExamplePlugin(QObject* parent = nullptr);
    void doSomething();
};
