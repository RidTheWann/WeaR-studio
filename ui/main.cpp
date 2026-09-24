// ==============================================================================
// WeaR-studio Entry Point
// ==============================================================================

#include "WeaRApp.h"
#include "MainWindow.h"
#include <AppDiagnostics.h>

#include <QCoreApplication>
#include <QDebug>
#include <QCommandLineParser>

#include <cstring>

namespace {

bool isSmokeTestArgument(int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--smoke-test") == 0) {
            return true;
        }
    }

    return false;
}

} // namespace

int main(int argc, char* argv[]) {
    if (isSmokeTestArgument(argc, argv)) {
        QCoreApplication app(argc, argv);

        qDebug() << "Packaged runtime smoke test passed.";
        return 0;
    }

    WeaR::WeaRApp app(argc, argv);

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    app.setAttribute(Qt::AA_UseHighDpiPixmaps);
#endif

    QCommandLineParser parser;
    parser.setApplicationDescription("WeaR Studio");
    parser.addHelpOption();
    parser.process(app);

    qDebug() << "Starting WeaR Studio...";

    WeaR::MainWindow mainWindow;
    mainWindow.show();

    qDebug() << "WeaR Studio ready";

    return app.exec();
}
