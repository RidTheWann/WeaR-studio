#include "AppDiagnostics.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QDebug>

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    if (!WeaR::AppDiagnostics::initialize()) {
        qCritical() << "Diagnostics initialization failed";
        return 1;
    }

    const QString path = WeaR::AppDiagnostics::logFilePath();
    qInfo() << "DIAGNOSTICS_TEST_MARKER";

    QFileInfo info(path);
    const bool exists = info.exists() && info.size() > 0;

    WeaR::AppDiagnostics::shutdown();

    if (!exists) {
        qCritical() << "Log file was not created:" << path;
        return 1;
    }

    qDebug() << "APP_DIAGNOSTICS: PASS" << path;
    return 0;
}
