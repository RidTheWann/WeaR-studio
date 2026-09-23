#pragma once
// =============================================================================
// WeaR-studio CEF runtime bootstrap
// =============================================================================

#include <QString>

namespace WeaR::BrowserSourceRuntime {

#ifdef WEAR_ENABLE_CEF
int executeSubProcess(int argc, char* argv[]);
bool initialize(int argc, char* argv[]);
void shutdown();
bool isInitialized();
#else
inline int executeSubProcess(int, char**) { return -1; }
inline bool initialize(int, char**) { return false; }
inline void shutdown() {}
inline bool isInitialized() { return false; }
#endif

QString lastError();

} // namespace WeaR::BrowserSourceRuntime
