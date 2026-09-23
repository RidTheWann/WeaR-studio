#include "GlobalHotkeyManager.h"

#include <QCoreApplication>
#include <QMutexLocker>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace WeaR {

namespace {

QList<GlobalHotkeyBinding> defaultBindings() {
    return {
        {GlobalHotkeyAction::StartStream, QKeySequence(QStringLiteral("Ctrl+Alt+F5")), true},
        {GlobalHotkeyAction::StopStream, QKeySequence(QStringLiteral("Ctrl+Alt+F6")), true},
        {GlobalHotkeyAction::StartRecord, QKeySequence(QStringLiteral("Ctrl+Alt+Shift+F5")), true},
        {GlobalHotkeyAction::StopRecord, QKeySequence(QStringLiteral("Ctrl+Alt+Shift+F6")), true},
        {GlobalHotkeyAction::NextScene, QKeySequence(QStringLiteral("Ctrl+Alt+F7")), true},
        {GlobalHotkeyAction::ToggleMicMute, QKeySequence(QStringLiteral("Ctrl+Alt+F8")), true}
    };
}

#ifdef Q_OS_WIN
int keyToNativeVirtualKey(Qt::Key key) {
    const int value = static_cast<int>(key);

    if (value >= Qt::Key_A && value <= Qt::Key_Z) {
        return 'A' + (value - Qt::Key_A);
    }
    if (value >= Qt::Key_0 && value <= Qt::Key_9) {
        return '0' + (value - Qt::Key_0);
    }
    if (value >= Qt::Key_F1 && value <= Qt::Key_F24) {
        return VK_F1 + (value - Qt::Key_F1);
    }

    switch (key) {
        case Qt::Key_Escape: return VK_ESCAPE;
        case Qt::Key_Tab:
        case Qt::Key_Backtab: return VK_TAB;
        case Qt::Key_Backspace: return VK_BACK;
        case Qt::Key_Return:
        case Qt::Key_Enter: return VK_RETURN;
        case Qt::Key_Space: return VK_SPACE;
        case Qt::Key_Insert: return VK_INSERT;
        case Qt::Key_Delete: return VK_DELETE;
        case Qt::Key_Home: return VK_HOME;
        case Qt::Key_End: return VK_END;
        case Qt::Key_PageUp: return VK_PRIOR;
        case Qt::Key_PageDown: return VK_NEXT;
        case Qt::Key_Left: return VK_LEFT;
        case Qt::Key_Right: return VK_RIGHT;
        case Qt::Key_Up: return VK_UP;
        case Qt::Key_Down: return VK_DOWN;
        case Qt::Key_Pause: return VK_PAUSE;
        case Qt::Key_Print: return VK_SNAPSHOT;
        case Qt::Key_0:
        case Qt::Key_1:
        case Qt::Key_2:
        case Qt::Key_3:
        case Qt::Key_4:
        case Qt::Key_5:
        case Qt::Key_6:
        case Qt::Key_7:
        case Qt::Key_8:
        case Qt::Key_9:
            return value;
        default:
            return 0;
    }
}
#endif

} // namespace

GlobalHotkeyManager& GlobalHotkeyManager::instance() {
    static GlobalHotkeyManager manager;
    return manager;
}

GlobalHotkeyManager::GlobalHotkeyManager(QObject* parent)
    : QObject(parent) {
    for (const auto& binding : defaultBindings()) {
        m_bindings.insert(binding.action, binding);
    }
}

GlobalHotkeyManager::~GlobalHotkeyManager() {
    shutdown();
}

bool GlobalHotkeyManager::initialize() {
    QMutexLocker lock(&m_mutex);
    if (m_initialized) {
        return true;
    }

#ifdef Q_OS_WIN
    if (!QCoreApplication::instance()) {
        m_lastError = QStringLiteral("QCoreApplication is required for global hotkeys.");
        return false;
    }
    QCoreApplication::instance()->installNativeEventFilter(this);
    m_initialized = true;
#else
    m_lastError = QStringLiteral(
        "Global OS hotkeys are currently implemented for Windows.");
    m_initialized = false;
    return false;
#endif

    return true;
}

void GlobalHotkeyManager::shutdown() {
    QMutexLocker lock(&m_mutex);

#ifdef Q_OS_WIN
    unregisterAllLocked();
    if (m_initialized && QCoreApplication::instance()) {
        QCoreApplication::instance()->removeNativeEventFilter(this);
    }
#endif

    m_initialized = false;
}

bool GlobalHotkeyManager::setBindings(
    const QList<GlobalHotkeyBinding>& bindings) {
    QMutexLocker lock(&m_mutex);

    const QMap<GlobalHotkeyAction, GlobalHotkeyBinding> oldBindings = m_bindings;
    QMap<GlobalHotkeyAction, GlobalHotkeyBinding> next;
    for (const auto& binding : bindings) {
        next.insert(binding.action, binding);
    }

    if (!m_initialized) {
        m_bindings = next;
        QSettings settings;
        settings.beginGroup(QStringLiteral("Hotkeys"));
        for (const auto& binding : next) {
            settings.setValue(
                actionId(binding.action),
                binding.sequence.toString(QKeySequence::PortableText));
        }
        settings.endGroup();
        return true;
    }

#ifdef Q_OS_WIN
    unregisterAllLocked();

    for (const auto& binding : next) {
        if (!binding.enabled || binding.sequence.isEmpty()) {
            continue;
        }

        if (!registerBindingLocked(binding)) {
            unregisterAllLocked();

            m_lastError = QString();
            for (const auto& oldBinding : oldBindings) {
                if (!oldBinding.enabled || oldBinding.sequence.isEmpty()) {
                    continue;
                }
                if (!registerBindingLocked(oldBinding)) {
                    // If an old hotkey can no longer be registered, leave it
                    // disabled rather than retaining a misleading in-memory
                    // registration state.
                    continue;
                }
            }

            m_bindings = oldBindings;
            return false;
        }
    }
#endif

    m_bindings = next;

    QSettings settings;
    settings.beginGroup(QStringLiteral("Hotkeys"));
    settings.remove(QString());
    for (const auto& binding : next) {
        settings.setValue(
            actionId(binding.action),
            binding.sequence.toString(QKeySequence::PortableText));
    }
    settings.endGroup();

    m_lastError.clear();
    return true;
}

QList<GlobalHotkeyBinding> GlobalHotkeyManager::bindings() const {
    QMutexLocker lock(&m_mutex);
    return m_bindings.values();
}

bool GlobalHotkeyManager::setBinding(
    GlobalHotkeyAction action,
    const QKeySequence& sequence) {
    QList<GlobalHotkeyBinding> next = bindings();

    bool found = false;
    for (auto& binding : next) {
        if (binding.action == action) {
            binding.sequence = sequence;
            binding.enabled = !sequence.isEmpty();
            found = true;
            break;
        }
    }

    if (!found) {
        next.append({action, sequence, !sequence.isEmpty()});
    }

    return setBindings(next);
}

QKeySequence GlobalHotkeyManager::binding(
    GlobalHotkeyAction action) const {
    QMutexLocker lock(&m_mutex);
    return m_bindings.contains(action)
        ? m_bindings.value(action).sequence
        : QKeySequence();
}

void GlobalHotkeyManager::setCallback(
    GlobalHotkeyAction action,
    std::function<void()> callback) {
    QMutexLocker lock(&m_mutex);
    m_callbacks.insert(action, std::move(callback));
}

QString GlobalHotkeyManager::lastError() const {
    QMutexLocker lock(&m_mutex);
    return m_lastError;
}

QString GlobalHotkeyManager::actionId(GlobalHotkeyAction action) {
    switch (action) {
        case GlobalHotkeyAction::StartStream: return QStringLiteral("startStream");
        case GlobalHotkeyAction::StopStream: return QStringLiteral("stopStream");
        case GlobalHotkeyAction::StartRecord: return QStringLiteral("startRecord");
        case GlobalHotkeyAction::StopRecord: return QStringLiteral("stopRecord");
        case GlobalHotkeyAction::NextScene: return QStringLiteral("nextScene");
        case GlobalHotkeyAction::ToggleMicMute: return QStringLiteral("toggleMicMute");
    }
    return QStringLiteral("unknown");
}

QString GlobalHotkeyManager::actionName(GlobalHotkeyAction action) {
    switch (action) {
        case GlobalHotkeyAction::StartStream: return QStringLiteral("Start stream");
        case GlobalHotkeyAction::StopStream: return QStringLiteral("Stop stream");
        case GlobalHotkeyAction::StartRecord: return QStringLiteral("Start recording");
        case GlobalHotkeyAction::StopRecord: return QStringLiteral("Stop recording");
        case GlobalHotkeyAction::NextScene: return QStringLiteral("Switch scene (next)");
        case GlobalHotkeyAction::ToggleMicMute: return QStringLiteral("Mute mic");
    }
    return QStringLiteral("Unknown");
}

#ifdef Q_OS_WIN

bool GlobalHotkeyManager::registerBindingLocked(
    const GlobalHotkeyBinding& binding) {
    const Qt::Key key = binding.sequence[0].key();
    if (key == Qt::Key_unknown) {
        m_lastError = QStringLiteral(
            "%1: unsupported key sequence. Use one key combination.")
            .arg(actionName(binding.action));
        emit registrationError(
            binding.action,
            binding.sequence.toString(),
            m_lastError);
        return false;
    }

    const int vk = keyToNativeVirtualKey(key);
    if (vk == 0) {
        m_lastError = QStringLiteral(
            "%1: unsupported key %2.")
            .arg(actionName(binding.action), key);
        emit registrationError(
            binding.action,
            binding.sequence.toString(),
            m_lastError);
        return false;
    }

    const Qt::KeyboardModifiers qtModifiers = binding.sequence[0].keyboardModifiers();
    UINT modifiers = 0;

    if (qtModifiers.testFlag(Qt::ControlModifier)) modifiers |= MOD_CONTROL;
    if (qtModifiers.testFlag(Qt::AltModifier)) modifiers |= MOD_ALT;
    if (qtModifiers.testFlag(Qt::ShiftModifier)) modifiers |= MOD_SHIFT;
    if (qtModifiers.testFlag(Qt::MetaModifier)) modifiers |= MOD_WIN;

    // Bare function keys and bare alphanumerics are allowed, matching native
    // RegisterHotKey semantics. Avoid MOD_NOREPEAT because it is not available
    // on all Windows SDK targets used by downstream builds.
    const int nativeId = 0x5700 + static_cast<int>(binding.action);

    if (!RegisterHotKey(nullptr, nativeId, modifiers, static_cast<UINT>(vk))) {
        const DWORD error = GetLastError();
        m_lastError = QStringLiteral(
            "%1 (%2) could not be registered. Win32 error %3.")
            .arg(actionName(binding.action),
                 binding.sequence.toString(),
                 QString::number(error));
        emit registrationError(
            binding.action,
            binding.sequence.toString(),
            m_lastError);
        return false;
    }

    m_registered.insert(nativeId, RegisteredHotkey{
        nativeId,
        binding.action,
        binding.sequence
    });
    return true;
}

void GlobalHotkeyManager::unregisterAllLocked() {
    for (auto it = m_registered.cbegin(); it != m_registered.cend(); ++it) {
        UnregisterHotKey(nullptr, it.key());
    }
    m_registered.clear();
}

#endif

bool GlobalHotkeyManager::nativeEventFilter(
    const QByteArray& eventType,
    void* message,
    qintptr* result) {
    Q_UNUSED(eventType);

#ifdef Q_OS_WIN
    MSG* msg = static_cast<MSG*>(message);
    if (!msg || msg->message != WM_HOTKEY) {
        return false;
    }

    GlobalHotkeyAction action;
    {
        QMutexLocker lock(&m_mutex);
        const int nativeId = static_cast<int>(msg->wParam);
        if (!m_registered.contains(nativeId)) {
            return false;
        }
        action = m_registered.value(nativeId).action;
    }

    if (result) {
        *result = 0;
    }

    activateAction(action);
    return true;
#else
    Q_UNUSED(message);
    Q_UNUSED(result);
    return false;
#endif
}

void GlobalHotkeyManager::activateAction(GlobalHotkeyAction action) {
    std::function<void()> callback;
    {
        QMutexLocker lock(&m_mutex);
        if (m_callbacks.contains(action)) {
            callback = m_callbacks.value(action);
        }
    }

    emit hotkeyActivated(action);

    if (callback) {
        callback();
    }
}

} // namespace WeaR
