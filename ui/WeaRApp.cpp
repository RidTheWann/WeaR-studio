// ==============================================================================
// WeaR-studio Application Implementation
// Professional dark-themed streaming application
// ==============================================================================

#include "WeaRApp.h"

#include <QStyleFactory>
#include <QFont>
#include <QDebug>

namespace WeaR {

WeaRApp::WeaRApp(int& argc, char** argv)
    : QApplication(argc, argv)
{
    // Set application metadata
    setApplicationName(displayName());
    setApplicationVersion(version());
    setOrganizationName(QStringLiteral("WeaR-studio"));
    setOrganizationDomain(QStringLiteral("wear-studio.com"));
    
    // Setup the dark theme
    setupDarkTheme();
    setupStylesheet();
    
    qDebug() << "WeaR Studio" << version() << "initialized";
}

WeaRApp::~WeaRApp() = default;

void WeaRApp::setupDarkTheme() {
    // Use Fusion style for consistent cross-platform look
    setStyle(QStyleFactory::create("Fusion"));
    
    // Create dark palette
    QPalette darkPalette;
    
    // Window colors
    QColor darkColor(45, 45, 48);           // Main background
    QColor darkerColor(30, 30, 32);         // Darker elements
    QColor lighterColor(62, 62, 66);        // Lighter elements
    QColor textColor(220, 220, 220);        // Primary text
    QColor disabledText(128, 128, 128);     // Disabled text
    QColor accentColor(0, 122, 204);        // Accent (blue)
    QColor highlightColor(51, 153, 255);    // Selection highlight
    QColor linkColor(86, 156, 214);         // Links
    
    // Base colors
    darkPalette.setColor(QPalette::Window, darkColor);
    darkPalette.setColor(QPalette::WindowText, textColor);
    darkPalette.setColor(QPalette::Base, darkerColor);
    darkPalette.setColor(QPalette::AlternateBase, darkColor);
    darkPalette.setColor(QPalette::ToolTipBase, darkColor);
    darkPalette.setColor(QPalette::ToolTipText, textColor);
    darkPalette.setColor(QPalette::Text, textColor);
    darkPalette.setColor(QPalette::Button, darkColor);
    darkPalette.setColor(QPalette::ButtonText, textColor);
    darkPalette.setColor(QPalette::BrightText, Qt::white);
    darkPalette.setColor(QPalette::Link, linkColor);
    darkPalette.setColor(QPalette::LinkVisited, linkColor.darker(120));
    
    // Highlight colors
    darkPalette.setColor(QPalette::Highlight, highlightColor);
    darkPalette.setColor(QPalette::HighlightedText, Qt::white);
    
    // Disabled colors
    darkPalette.setColor(QPalette::Disabled, QPalette::WindowText, disabledText);
    darkPalette.setColor(QPalette::Disabled, QPalette::Text, disabledText);
    darkPalette.setColor(QPalette::Disabled, QPalette::ButtonText, disabledText);
    darkPalette.setColor(QPalette::Disabled, QPalette::HighlightedText, disabledText);
    darkPalette.setColor(QPalette::Disabled, QPalette::Base, darkColor);
    darkPalette.setColor(QPalette::Disabled, QPalette::Button, darkColor.darker(110));
    
    // Apply palette
    setPalette(darkPalette);
    
    // Set default font
    QFont defaultFont("Segoe UI", 9);
    setFont(defaultFont);
}

void WeaRApp::setupStylesheet() {
    // Modern flat design stylesheet
    QString stylesheet = R"(
        /* ============================================= */
        /* Global Styles                                 */
        /* ============================================= */
        
        QMainWindow {
            background-color: #2d2d30;
        }
        
        QToolTip {
            background-color: #1e1e1e;
            color: #dcdcdc;
            border: 1px solid #3e3e42;
            padding: 4px;
            border-radius: 2px;
        }
        
        /* ============================================= */
        /* Dock Widgets                                  */
        /* ============================================= */
        
        QDockWidget {
            titlebar-close-icon: url(close.png);
            titlebar-normal-icon: url(float.png);
            font-weight: bold;
            color: #dcdcdc;
        }
        
        QDockWidget::title {
            background-color: #3e3e42;
            padding: 6px;
            border-bottom: 1px solid #252526;
        }
        
        QDockWidget::close-button, QDockWidget::float-button {
            background: transparent;
            border: none;
            padding: 2px;
        }
        
        QDockWidget::close-button:hover, QDockWidget::float-button:hover {
            background-color: #525257;
            border-radius: 2px;
        }
        
        /* ============================================= */
        /* Push Buttons                                  */
        /* ============================================= */
        
        QPushButton {
            background-color: #3e3e42;
            border: 1px solid #555555;
            border-radius: 4px;
            color: #dcdcdc;
            padding: 6px 16px;
            min-height: 24px;
            font-weight: 500;
        }
        
        QPushButton:hover {
            background-color: #4e4e52;
            border-color: #666666;
        }
        
        QPushButton:pressed {
            background-color: #2d2d30;
        }
        
        QPushButton:disabled {
            background-color: #2d2d30;
            color: #808080;
            border-color: #404040;
        }
        
        QPushButton#startStreamBtn {
            background-color: #107c10;
            border-color: #0d6d0d;
        }
        
        QPushButton#startStreamBtn:hover {
            background-color: #0e8c0e;
        }
        
        QPushButton#stopStreamBtn {
            background-color: #c42b1c;
            border-color: #a82515;
        }
        
        QPushButton#stopStreamBtn:hover {
            background-color: #d43b2c;
        }
        
        /* ============================================= */
        /* List Widgets                                  */
        /* ============================================= */
        
        QListWidget {
            background-color: #1e1e1e;
            border: 1px solid #3e3e42;
            border-radius: 4px;
            outline: none;
        }
        
        QListWidget::item {
            padding: 8px;
            border-bottom: 1px solid #2d2d30;
        }
        
        QListWidget::item:hover {
            background-color: #3e3e42;
        }
        
        QListWidget::item:selected {
            background-color: #094771;
            color: white;
        }
        
        /* ============================================= */
        /* Line Edits                                    */
        /* ============================================= */
        
        QLineEdit {
            background-color: #1e1e1e;
            border: 1px solid #3e3e42;
            border-radius: 4px;
            color: #dcdcdc;
            padding: 6px;
            selection-background-color: #264f78;
        }
        
        QLineEdit:focus {
            border-color: #007acc;
        }
        
        QLineEdit:disabled {
            background-color: #2d2d30;
            color: #808080;
        }
        
        /* ============================================= */
        /* Labels                                        */
        /* ============================================= */
        
        QLabel {
            color: #dcdcdc;
        }
        
        QLabel#statusLabel {
            color: #9cdcfe;
            font-weight: bold;
        }
        
        QLabel#errorLabel {
            color: #f14c4c;
        }
        
        QLabel#successLabel {
            color: #89d185;
        }
        
        /* ============================================= */
        /* Scroll Bars                                   */
        /* ============================================= */
        
        QScrollBar:vertical {
            background-color: #1e1e1e;
            width: 12px;
            margin: 0;
        }
        
        QScrollBar::handle:vertical {
            background-color: #5a5a5f;
            min-height: 20px;
            border-radius: 6px;
            margin: 2px;
        }
        
        QScrollBar::handle:vertical:hover {
            background-color: #787878;
        }
        
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
            height: 0;
        }
        
        QScrollBar:horizontal {
            background-color: #1e1e1e;
            height: 12px;
            margin: 0;
        }
        
        QScrollBar::handle:horizontal {
            background-color: #5a5a5f;
            min-width: 20px;
            border-radius: 6px;
            margin: 2px;
        }
        
        QScrollBar::handle:horizontal:hover {
            background-color: #787878;
        }
        
        QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal {
            width: 0;
        }
        
        /* ============================================= */
        /* Menu Bar                                      */
        /* ============================================= */
        
        QMenuBar {
            background-color: #2d2d30;
            border-bottom: 1px solid #3e3e42;
        }
        
        QMenuBar::item {
            padding: 6px 12px;
        }
        
        QMenuBar::item:selected {
            background-color: #3e3e42;
        }
        
        QMenu {
            background-color: #2d2d30;
            border: 1px solid #3e3e42;
        }
        
        QMenu::item {
            padding: 6px 24px;
        }
        
        QMenu::item:selected {
            background-color: #094771;
        }
        
        QMenu::separator {
            height: 1px;
            background-color: #3e3e42;
            margin: 4px 8px;
        }
        
        /* ============================================= */
        /* Status Bar                                    */
        /* ============================================= */
        
        QStatusBar {
            background-color: #007acc;
            color: white;
        }
        
        QStatusBar::item {
            border: none;
        }
        
        /* ============================================= */
        /* Group Box                                     */
        /* ============================================= */
        
        QGroupBox {
            border: 1px solid #3e3e42;
            border-radius: 4px;
            margin-top: 12px;
            padding-top: 8px;
        }
        
        QGroupBox::title {
            subcontrol-origin: margin;
            left: 8px;
            padding: 0 4px;
            color: #9cdcfe;
        }
    )";
    
    setStyleSheet(stylesheet);
}

} // namespace WeaR
