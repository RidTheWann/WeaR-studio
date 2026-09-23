// ==============================================================================
// WeaR-studio Entry Point
// ==============================================================================

#include "WeaRApp.h"
#include "MainWindow.h"
#include <AppDiagnostics.h>

#include <QDebug>
#include <QCommandLineParser>

int main(int argc, char* argv[]) {
    // Create application with dark theme
    WeaR::WeaRApp app(argc, argv);
    
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    app.setAttribute(Qt::AA_UseHighDpiPixmaps);
#endif
    
    QCommandLineParser parser;
    parser.setApplicationDescription("WeaR Studio");
    parser.addHelpOption();
    QCommandLineOption smokeOption(
        QStringList() << "smoke-test",
        "Load the packaged executable and exit without opening the main window.");
    parser.addOption(smokeOption);
    parser.process(app);

    qDebug() << "Starting WeaR Studio...";

    if (parser.isSet(smokeOption)) {
        qDebug() << "Packaged runtime smoke test passed.";
        WeaR::AppDiagnostics::shutdown();
        return 0;
    }

    // Create and show main window
    WeaR::MainWindow mainWindow;
    mainWindow.show();
    
    qDebug() << "WeaR Studio ready";
    
    return app.exec();
}
