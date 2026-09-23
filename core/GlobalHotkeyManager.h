#pragma once

#include <QObject>
#include <QKeySequence>
#include <QAbstractNativeEventFilter>
#include <QMap>
#include <QString>

#include <functional>

namespace WeaR {

enum class GlobalHotkeyAction {
    StartStream,
    StopStream,
    StartRecord,
    StopRecord,
    NextScene,
    ToggleMicMute
};

struct GlobalHotkeyBinding {
    GlobalHotkeyAction action;
    QKeySequence sequence;
    bool enabled = true;
};

class GlobalHotkeyManager final : public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT

public:
    static GlobalHotkeyManager& instance();

    GlobalHotkeyManager(const GlobalHotkeyManager&) = delete;
    GlobalHotkeyManager& operator=(const GlobalHotkeyManager&) = delete;

    ~GlobalHotkeyManager() override;

    bool initialize();
    void shutdown();

    bool setBindings(const QList<GlobalHotkeyBinding>& bindings);
    QList<GlobalHotkeyBinding> bindings() const;

    bool setBinding(GlobalHotkeyAction action, const QKeySequence& sequence);
    QKeySequence binding(GlobalHotkeyAction action) const;

    void setCallback(GlobalHotkeyAction action, std::function<void()> callback);

    QString lastError() const;

    static QString actionId(GlobalHotkeyAction action);
    static QString actionName(GlobalHotkeyAction action);

    bool nativeEventFilter(
        const QByteArray& eventType,
        void* message,
        qintptr* result) override;

signals:
    void hotkeyActivated(WeaR::GlobalHotkeyAction action);
    void registrationError(
        WeaR::GlobalHotkeyAction action,
        const QString& sequence,
        const QString& error);

private:
    explicit GlobalHotkeyManager(QObject* parent = nullptr);

    struct RegisteredHotkey {
        int nativeId = 0;
        GlobalHotkeyAction action = GlobalHotkeyAction::StartStream;
        QKeySequence sequence;
    };

    mutable QMutex m_mutex;
    QMap<GlobalHotkeyAction, GlobalHotkeyBinding> m_bindings;
    QMap<int, RegisteredHotkey> m_registered;
    QMap<GlobalHotkeyAction, std::function<void()>> m_callbacks;
    QString m_lastError;
    bool m_initialized = false;

    bool registerBindingLocked(const GlobalHotkeyBinding& binding);
    void unregisterAllLocked();
    void activateAction(GlobalHotkeyAction action);
};

} // namespace WeaR
