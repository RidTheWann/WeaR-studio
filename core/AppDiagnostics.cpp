#include "AppDiagnostics.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QStandardPaths>
#include <QTextStream>
#include <QThread>

#include <atomic>
#include <cstdlib>
#include <exception>
#include <mutex>

#ifdef Q_OS_WIN
#include <Windows.h>
#include <DbgHelp.h>
#include <process.h>
#endif

namespace WeaR {

namespace {

QMutex g_logMutex;
QFile g_logFile;
QtMessageHandler g_previousHandler = nullptr;
std::terminate_handler g_previousTerminateHandler = nullptr;
std::atomic<bool> g_initialized{false};
QString g_logDirectory;
QString g_logFilePath;

QString defaultLogDirectory() {
    QString root = QStandardPaths::writableLocation(
        QStandardPaths::AppLocalDataLocation);
    if (root.isEmpty()) {
#ifdef Q_OS_WIN
        root = qEnvironmentVariable("LOCALAPPDATA");
#else
        root = QStandardPaths::writableLocation(
            QStandardPaths::GenericDataLocation);
#endif
    }

    if (root.isEmpty()) {
        root = QDir::tempPath();
    }

    return QDir(root).filePath(QStringLiteral("logs"));
}

void writeLineUnlocked(const QString& line) {
    if (!g_logFile.isOpen()) {
        return;
    }

    QTextStream stream(&g_logFile);
    stream << line << Qt::endl;
    g_logFile.flush();
}

void writeLine(const QString& line) {
    QMutexLocker lock(&g_logMutex);
    writeLineUnlocked(line);
}

QString messageTypeName(QtMsgType type) {
    switch (type) {
        case QtDebugMsg: return QStringLiteral("DEBUG");
        case QtInfoMsg: return QStringLiteral("INFO");
        case QtWarningMsg: return QStringLiteral("WARNING");
        case QtCriticalMsg: return QStringLiteral("CRITICAL");
        case QtFatalMsg: return QStringLiteral("FATAL");
    }
    return QStringLiteral("UNKNOWN");
}

void messageHandler(
    QtMsgType type,
    const QMessageLogContext& context,
    const QString& message) {
    const QString timestamp = QDateTime::currentDateTimeUtc()
        .toString(Qt::ISODateWithMs);

    const QString source = context.file
        ? QStringLiteral("%1:%2").arg(context.file).arg(context.line)
        : QStringLiteral("unknown");

    const QString line = QStringLiteral("[%1] [%2] [tid=%3] [%4] %5")
        .arg(timestamp,
             messageTypeName(type),
             QString::number(reinterpret_cast<quintptr>(QThread::currentThreadId()), 16),
             source,
             message);

    writeLine(line);

#ifdef Q_OS_WIN
    OutputDebugStringW(reinterpret_cast<const wchar_t*>(line.utf16()));
    OutputDebugStringW(L"\r\n");
#endif

    if (g_previousHandler) {
        g_previousHandler(type, context, message);
    }
}

void terminateHandler() noexcept {
    QString message = QStringLiteral(
        "std::terminate invoked; process will exit.");
    if (std::current_exception()) {
        try {
            std::rethrow_exception(std::current_exception());
        } catch (const std::exception& ex) {
            message += QStringLiteral(" Exception: %1")
                .arg(QString::fromLocal8Bit(ex.what()));
        } catch (...) {
            message += QStringLiteral(" Exception type unavailable.");
        }
    }

    AppDiagnostics::writeCrashNote(message);

    if (g_previousTerminateHandler) {
        g_previousTerminateHandler();
    }

    std::abort();
}

#ifdef Q_OS_WIN
LONG WINAPI unhandledExceptionFilter(EXCEPTION_POINTERS* exceptionInfo) {
    const QString timestamp =
        QDateTime::currentDateTimeUtc().toString("yyyyMMdd_HHmmss_zzz");
    const QString base = QDir(g_logDirectory).filePath(
        QStringLiteral("crash_%1").arg(timestamp));

    const QString crashLog = base + QStringLiteral(".log");
    {
        QFile file(crashLog);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text |
                      QIODevice::Truncate)) {
            QTextStream stream(&file);
            stream << "WeaR Studio unhandled exception\n";
            stream << "UTC: "
                   << QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)
                   << "\n";
            stream << "Process: " << QCoreApplication::applicationPid() << "\n";
            stream << "Exception code: 0x"
                   << QString::number(
                          exceptionInfo && exceptionInfo->ExceptionRecord
                              ? exceptionInfo->ExceptionRecord->ExceptionCode
                              : 0,
                          16)
                   << "\n";
            stream << "Exception address: 0x"
                   << QString::number(
                          exceptionInfo && exceptionInfo->ExceptionRecord
                              ? reinterpret_cast<quintptr>(
                                    exceptionInfo->ExceptionRecord->ExceptionAddress)
                              : 0,
                          16)
                   << "\n";
        }
    }

    const QString dumpPath = base + QStringLiteral(".dmp");
    HANDLE fileHandle = CreateFileW(
        reinterpret_cast<LPCWSTR>(dumpPath.utf16()),
        GENERIC_WRITE,
        FILE_SHARE_READ,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);

    if (fileHandle != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION exceptionData{};
        exceptionData.ThreadId = GetCurrentThreadId();
        exceptionData.ExceptionPointers = exceptionInfo;
        exceptionData.ClientPointers = FALSE;

        const MINIDUMP_TYPE dumpType =
            static_cast<MINIDUMP_TYPE>(
                MiniDumpWithDataSegs |
                MiniDumpWithHandleData |
                MiniDumpWithThreadInfo |
                MiniDumpWithIndirectlyReferencedMemory |
                MiniDumpScanMemory);

        MiniDumpWriteDump(
            GetCurrentProcess(),
            GetCurrentProcessId(),
            fileHandle,
            dumpType,
            exceptionInfo ? &exceptionData : nullptr,
            nullptr,
            nullptr);

        CloseHandle(fileHandle);
    }

    AppDiagnostics::writeCrashNote(
        QStringLiteral(
            "Unhandled Windows exception captured. Crash artifacts: %1(.log/.dmp)")
            .arg(base));

    return EXCEPTION_EXECUTE_HANDLER;
}
#endif

} // namespace

bool AppDiagnostics::initialize() {
    if (g_initialized.exchange(true)) {
        return true;
    }

    g_logDirectory = defaultLogDirectory();
    if (!QDir().mkpath(g_logDirectory)) {
        g_initialized = false;
        return false;
    }

    g_logFilePath = QDir(g_logDirectory).filePath(
        QStringLiteral("wear-studio.log"));

    QFileInfo info(g_logFilePath);
    if (info.exists() && info.size() > 10 * 1024 * 1024) {
        const QString rotated =
            g_logFilePath + QStringLiteral(".1");
        QFile::remove(rotated);
        QFile::rename(g_logFilePath, rotated);
    }

    g_logFile.setFileName(g_logFilePath);
    if (!g_logFile.open(QIODevice::Append | QIODevice::Text)) {
        g_initialized = false;
        return false;
    }

    g_previousHandler = qInstallMessageHandler(messageHandler);
    g_previousTerminateHandler = std::set_terminate(terminateHandler);

#ifdef Q_OS_WIN
    SetUnhandledExceptionFilter(unhandledExceptionFilter);
#endif

    writeLine(
        QStringLiteral(
            "===== WeaR Studio diagnostics started %1 =====")
            .arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)));
    writeLine(
        QStringLiteral("Version=%1 PID=%2 Qt=%3")
            .arg(
                QCoreApplication::applicationVersion(),
                QString::number(QCoreApplication::applicationPid()),
                QString::fromLatin1(qVersion())));
    writeLine(QStringLiteral("Log directory=%1").arg(g_logDirectory));

    return true;
}

void AppDiagnostics::shutdown() {
    if (!g_initialized.exchange(false)) {
        return;
    }

#ifdef Q_OS_WIN
    SetUnhandledExceptionFilter(nullptr);
#endif

    std::set_terminate(g_previousTerminateHandler);
    qInstallMessageHandler(g_previousHandler);

    QMutexLocker lock(&g_logMutex);
    if (g_logFile.isOpen()) {
        writeLineUnlocked(
            QStringLiteral(
                "===== WeaR Studio diagnostics stopped %1 =====")
                .arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)));
        g_logFile.close();
    }
}

QString AppDiagnostics::logDirectory() {
    return g_logDirectory;
}

QString AppDiagnostics::logFilePath() {
    return g_logFilePath;
}

QString AppDiagnostics::crashFilePrefix() {
    return QDir(g_logDirectory).filePath(QStringLiteral("crash_"));
}

void AppDiagnostics::writeDiagnostic(const QString& message) {
    writeLine(QStringLiteral("[%1] [DIAGNOSTIC] %2")
        .arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs),
             message));
}

void AppDiagnostics::writeCrashNote(const QString& message) {
    writeLine(QStringLiteral("[%1] [CRASH] %2")
        .arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs),
             message));
}

} // namespace WeaR
