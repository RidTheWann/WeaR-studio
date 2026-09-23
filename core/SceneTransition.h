#pragma once
// =============================================================================
// WeaR-studio scene transition primitives
// Non-blocking visual transition compositor used by SceneManager.
// =============================================================================

#include <QImage>
#include <QString>

namespace WeaR {

enum class SceneTransitionType {
    Cut = 0,
    Fade = 1,
    Slide = 2
};

struct SceneTransition {
    static QString typeName(SceneTransitionType type);
    static bool isAnimated(SceneTransitionType type);
    static QImage compose(
        const QImage& fromFrame,
        const QImage& toFrame,
        SceneTransitionType type,
        double progress);
};

} // namespace WeaR
