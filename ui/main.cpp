// ==============================================================================
// WeaR-studio Entry Point
// ==============================================================================

#include "WeaRApp.h"
#include "MainWindow.h"

#include <QDebug>

int main(int argc, char* argv[]) {
    // Create application with dark theme
    WeaR::WeaRApp app(argc, argv);
    
    // Enable high DPI scaling
    app.setAttribute(Qt::AA_UseHighDpiPixmaps);
    
    qDebug() << "Starting WeaR Studio...";
    
    // Create and show main window
    WeaR::MainWindow mainWindow;
    mainWindow.show();
    
    qDebug() << "WeaR Studio ready";
    
    return app.exec();
}
