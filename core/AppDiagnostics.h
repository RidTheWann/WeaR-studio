#pragma once

#include <QString>

namespace WeaR {

/**
 * @brief Application diagnostics: persistent log capture and crash artifacts.
 *
 * Logs are stored under the per-user application data directory and are
 * intentionally independent from the install directory so a non-admin user
 * can always write diagnostics.
 */
class AppDiagnostics final {
public:
    static bool initialize();
    static void shutdown();

    static QString logDirectory();
    static QString logFilePath();
    static QString crashFilePrefix();

    static void writeDiagnostic(const QString& message);
    static void writeCrashNote(const QString& message);
};

} // namespace WeaR
