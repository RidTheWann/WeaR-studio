// =============================================================================
// WeaR-studio scene transition implementation
// =============================================================================

#include "SceneTransition.h"

#include <QPainter>

#include <algorithm>

namespace WeaR {

QString SceneTransition::typeName(SceneTransitionType type) {
    switch (type) {
        case SceneTransitionType::Fade:
            return QStringLiteral("Fade");
        case SceneTransitionType::Slide:
            return QStringLiteral("Slide");
        case SceneTransitionType::Cut:
        default:
            return QStringLiteral("Cut");
    }
}

bool SceneTransition::isAnimated(SceneTransitionType type) {
    return type != SceneTransitionType::Cut;
}

QImage SceneTransition::compose(
    const QImage& fromFrame,
    const QImage& toFrame,
    SceneTransitionType type,
    double progress) {
    if (toFrame.isNull()) {
        return fromFrame;
    }
    if (fromFrame.isNull() || type == SceneTransitionType::Cut) {
        return toFrame;
    }

    const double p = std::clamp(progress, 0.0, 1.0);
    if (p <= 0.0) {
        return fromFrame;
    }
    if (p >= 1.0) {
        return toFrame;
    }

    const QSize size = toFrame.size();
    QImage output(size, QImage::Format_ARGB32_Premultiplied);
    output.fill(Qt::transparent);

    QPainter painter(&output);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.setClipRect(QRect(QPoint(0, 0), size));

    if (type == SceneTransitionType::Fade) {
        painter.drawImage(QPoint(0, 0), fromFrame);
        painter.setOpacity(p);
        painter.drawImage(QPoint(0, 0), toFrame);
    } else {
        const qreal offset =
            static_cast<qreal>(p * static_cast<double>(size.width()));
        painter.drawImage(
            QRectF(-offset, 0.0, size.width(), size.height()),
            fromFrame);
        painter.drawImage(
            QRectF(size.width() - offset, 0.0, size.width(), size.height()),
            toFrame);
    }

    painter.end();
    return output;
}

} // namespace WeaR
