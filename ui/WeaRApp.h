#pragma once
// ==============================================================================
// WeaR-studio Application
// Professional dark-themed streaming application
// ==============================================================================

#include <QApplication>
#include <QPalette>

namespace WeaR {

/**
 * @brief WeaR-studio Application class
 * 
 * Custom QApplication subclass that sets up:
 * - Professional dark theme using Fusion style
 * - Global stylesheet for modern flat UI
 * - Application-wide settings and configurations
 */
class WeaRApp : public QApplication {
    Q_OBJECT

public:
    /**
     * @brief Construct the application
     * @param argc Argument count
     * @param argv Argument values
     */
    WeaRApp(int& argc, char** argv);
    ~WeaRApp() override;

    /**
     * @brief Get app version string
     */
    [[nodiscard]] static QString version() { return QStringLiteral("0.1"); }
    
    /**
     * @brief Get app display name
     */
    [[nodiscard]] static QString displayName() { return QStringLiteral("WeaR Studio"); }

private:
    void setupDarkTheme();
    void setupStylesheet();
    void initializeManagers();
};

} // namespace WeaR
