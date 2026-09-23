#include "GlobalHotkeyManager.h"

#include <QCoreApplication>
#include <QDebug>

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    auto& manager = WeaR::GlobalHotkeyManager::instance();
    const auto bindings = manager.bindings();

    if (bindings.size() != 6) {
        qCritical() << "Expected 6 default hotkey bindings, got" << bindings.size();
        return 1;
    }

    if (manager.actionId(WeaR::GlobalHotkeyAction::StartStream) != "startStream" ||
        manager.actionId(WeaR::GlobalHotkeyAction::StopRecord) != "stopRecord" ||
        manager.actionName(WeaR::GlobalHotkeyAction::ToggleMicMute) != "Mute mic") {
        qCritical() << "Hotkey action metadata mismatch";
        return 1;
    }

#ifdef Q_OS_WIN
    if (!manager.initialize()) {
        qCritical() << "Global hotkey initialization failed:" << manager.lastError();
        return 1;
    }

    manager.setBindings({});
    if (!manager.bindings().isEmpty()) {
        qCritical() << "Hotkey disable/reset failed";
        return 1;
    }

    manager.shutdown();
#endif

    qDebug() << "GLOBAL_HOTKEY: PASS";
    return 0;
}
