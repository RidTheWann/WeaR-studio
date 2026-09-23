// ==============================================================================
// WeaR-studio Entry Point
// ==============================================================================

#include "WeaRApp.h"
#include "MainWindow.h"

#include <QDebug>
#ifdef WEAR_ENABLE_CEF
#include "BrowserSourceRuntime.h"
#endif

int main(int argc, char* argv[]) {
#ifdef WEAR_ENABLE_CEF
    // CEF child processes must be handled before constructing the Qt app.
    const int cefExitCode =
        WeaR::BrowserSourceRuntime::executeSubProcess(argc, argv);
    if (cefExitCode >= 0) {
        return cefExitCode;
    }
#endif

    // Create application with dark theme
    WeaR::WeaRApp app(argc, argv);

#ifdef WEAR_ENABLE_CEF
    if (!WeaR::BrowserSourceRuntime::initialize(argc, argv)) {
        qWarning() << "CEF Browser Source disabled:"
                   << WeaR::BrowserSourceRuntime::lastError();
    }
#endif
    
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    app.setAttribute(Qt::AA_UseHighDpiPixmaps);
#endif
    
    qDebug() << "Starting WeaR Studio...";
    
    // Create and show main window
    WeaR::MainWindow mainWindow;
    mainWindow.show();
    
    qDebug() << "WeaR Studio ready";
    
    const int exitCode = app.exec();
#ifdef WEAR_ENABLE_CEF
    WeaR::BrowserSourceRuntime::shutdown();
#endif
    return exitCode;
}
