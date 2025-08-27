#pragma once
#include <QObject>

class PluginManager : public QObject {
    Q_OBJECT
public:
    explicit PluginManager(QObject* parent = nullptr);
    void loadPlugin(const QString& path);
};
