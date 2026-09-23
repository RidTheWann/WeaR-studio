// =============================================================================
// WeaR-studio CEF runtime bootstrap implementation
// =============================================================================

#include "BrowserSourceRuntime.h"

#ifdef WEAR_ENABLE_CEF

#include "include/cef_app.h"

#include <QCoreApplication>

#include <mutex>

namespace WeaR::BrowserSourceRuntime {
namespace {

class BrowserRuntimeApp final : public CefApp {
public:
    IMPLEMENT_REFCOUNTING(BrowserRuntimeApp);
};

std::mutex g_mutex;
CefRefPtr<BrowserRuntimeApp> g_app;
bool g_initialized = false;
QString g_lastError;

} // namespace

int executeSubProcess(int argc, char* argv[]) {
    CefMainArgs mainArgs(argc, argv);
    CefRefPtr<BrowserRuntimeApp> app(new BrowserRuntimeApp);
    return CefExecuteProcess(mainArgs, app, nullptr);
}

bool initialize(int argc, char* argv[]) {
    std::lock_guard<std::mutex> lock(g_mutex);

    if (g_initialized) {
        return true;
    }

    CefEnableHighDPISupport();

    CefMainArgs mainArgs(argc, argv);
    g_app = new BrowserRuntimeApp;

    CefSettings settings;
    settings.no_sandbox = true;
    settings.windowless_rendering_enabled = true;
    settings.multi_threaded_message_loop = true;

    if (!CefInitialize(mainArgs, settings, g_app.get(), nullptr)) {
        g_app = nullptr;
        g_lastError = QStringLiteral("CefInitialize() failed.");
        return false;
    }

    g_initialized = true;
    g_lastError.clear();
    return true;
}

void shutdown() {
    std::lock_guard<std::mutex> lock(g_mutex);

    if (!g_initialized) {
        return;
    }

    CefShutdown();
    g_app = nullptr;
    g_initialized = false;
}

bool isInitialized() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_initialized;
}

QString lastError() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_lastError;
}

} // namespace WeaR::BrowserSourceRuntime

#else

namespace WeaR::BrowserSourceRuntime {

QString lastError() {
    return QStringLiteral("Browser Source was built without CEF support.");
}

} // namespace WeaR::BrowserSourceRuntime

#endif
