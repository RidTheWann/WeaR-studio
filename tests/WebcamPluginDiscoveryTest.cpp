// =============================================================================
// WeaR-studio webcam plugin discovery test
// =============================================================================

#include "PluginManager.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    const QString pluginDir =
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("plugins"));

    auto& manager = WeaR::PluginManager::instance();
    manager.setPluginsDirectory(pluginDir);

    const int discovered = manager.discoverPlugins();
    manager.loadAllPlugins();

    WeaR::ISource* webcam = manager.source(QStringLiteral("wear.source.webcam"));
    if (!webcam) {
        qCritical() << "Webcam plugin was not discovered/loaded from" << pluginDir
                    << "discovered=" << discovered;
        return 1;
    }

    if (!WeaR::hasCapability(webcam->capabilities(), WeaR::PluginCapability::HasVideo)) {
        qCritical() << "Webcam plugin does not advertise video capability";
        return 1;
    }

    // CI normally has no physical camera. An empty device list is acceptable;
    // the important integration guarantee is that the source plugin loads
    // independently and does not break the other source plugins.
    qDebug() << "WEBCAM_PLUGIN: PASS"
             << "devices=" << webcam->availableDevices().size();
    return 0;
}
