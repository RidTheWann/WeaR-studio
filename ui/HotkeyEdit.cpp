#include "HotkeyEdit.h"

#include <QKeyEvent>

namespace WeaR {

HotkeyEdit::HotkeyEdit(QWidget* parent)
    : QLineEdit(parent) {
    setReadOnly(true);
    setPlaceholderText(QStringLiteral("Press a key combination"));
}

QKeySequence HotkeyEdit::sequence() const {
    return m_sequence;
}

void HotkeyEdit::setSequence(const QKeySequence& sequence) {
    if (m_sequence == sequence) {
        return;
    }

    m_sequence = sequence;
    setText(sequence.toString(QKeySequence::PortableText));
    emit sequenceChanged(sequence);
}

void HotkeyEdit::keyPressEvent(QKeyEvent* event) {
    if (!event) return;

    if (event->key() == Qt::Key_Backspace ||
        event->key() == Qt::Key_Delete) {
        setSequence(QKeySequence());
        event->accept();
        return;
    }

    if (event->isAutoRepeat()) {
        event->accept();
        return;
    }

    const Qt::KeyboardModifiers relevantModifiers =
        event->modifiers() &
        (Qt::ControlModifier |
         Qt::AltModifier |
         Qt::ShiftModifier |
         Qt::MetaModifier);

    if (relevantModifiers == Qt::NoModifier &&
        (event->key() == Qt::Key_unknown ||
         event->key() == Qt::Key_Control ||
         event->key() == Qt::Key_Alt ||
         event->key() == Qt::Key_Shift ||
         event->key() == Qt::Key_Meta)) {
        event->accept();
        return;
    }

    QKeyCombination combination(relevantModifiers,
                                static_cast<Qt::Key>(event->key()));
    setSequence(QKeySequence(combination));
    event->accept();
}

} // namespace WeaR
