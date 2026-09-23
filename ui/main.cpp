// ==============================================================================
// WeaR-studio Entry Point
// ==============================================================================

#include "WeaRApp.h"
#include "MainWindow.h"

#include <QDebug>

int main(int argc, char* argv[]) {
    // Create application with dark theme
    WeaR::WeaRApp app(argc, argv);
    
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    app.setAttribute(Qt::AA_UseHighDpiPixmaps);
#endif
    
    qDebug() << "Starting WeaR Studio...";
    
    // Create and show main window
    WeaR::MainWindow mainWindow;
    mainWindow.show();
    
    qDebug() << "WeaR Studio ready";
    
    return app.exec();
}
