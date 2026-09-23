#pragma once

#include <QKeySequence>
#include <QLineEdit>

class QKeyEvent;

namespace WeaR {

class HotkeyEdit final : public QLineEdit {
    Q_OBJECT

public:
    explicit HotkeyEdit(QWidget* parent = nullptr);

    QKeySequence sequence() const;
    void setSequence(const QKeySequence& sequence);

signals:
    void sequenceChanged(const QKeySequence& sequence);

protected:
    void keyPressEvent(QKeyEvent* event) override;

private:
    QKeySequence m_sequence;
};

} // namespace WeaR
