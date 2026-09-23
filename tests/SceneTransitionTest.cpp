// =============================================================================
// WeaR-studio scene transition tests
// =============================================================================

#include "SceneManager.h"
#include "SceneTransition.h"

#include <QGuiApplication>
#include <QDebug>
#include <QThread>

#include <cmath>

namespace {

bool approximately(double value, double expected, double tolerance) {
    return std::abs(value - expected) <= tolerance;
}

int pixelRed(const QImage& image, int x = 0, int y = 0) {
    return image.pixelColor(x, y).red();
}

} // namespace

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);

    QImage from(QSize(64, 36), QImage::Format_ARGB32_Premultiplied);
    from.fill(Qt::black);

    QImage to(QSize(64, 36), QImage::Format_ARGB32_Premultiplied);
    to.fill(QColor(255, 0, 0));

    const QImage cut = WeaR::SceneTransition::compose(
        from, to, WeaR::SceneTransitionType::Cut, 0.0);
    if (pixelRed(cut) != 255) {
        qCritical() << "CUT transition failed";
        return 1;
    }

    const QImage fade = WeaR::SceneTransition::compose(
        from, to, WeaR::SceneTransitionType::Fade, 0.5);
    if (!approximately(pixelRed(fade, 32, 18), 128.0, 2.0)) {
        qCritical() << "FADE transition failed:" << pixelRed(fade, 32, 18);
        return 1;
    }

    const QImage slide = WeaR::SceneTransition::compose(
        from, to, WeaR::SceneTransitionType::Slide, 0.5);
    if (pixelRed(slide, 16, 18) > 8 || pixelRed(slide, 48, 18) < 247) {
        qCritical() << "SLIDE transition failed";
        return 1;
    }

    // Integration test: SceneManager keeps its normal render timer and performs
    // the transition inside renderFrame() without sleeping or starting a second
    // render timer.
    qputenv("WEAR_COMPOSITOR", QByteArray("qpainter"));
    auto& manager = WeaR::SceneManager::instance();
    manager.stopRenderLoop();
    manager.setOutputResolution(64, 36);

    WeaR::Scene* sceneA =
        manager.createScene(QStringLiteral("Transition A"));
    WeaR::Scene* sceneB =
        manager.createScene(QStringLiteral("Transition B"));
    sceneA->setBackgroundColor(Qt::black);
    sceneB->setBackgroundColor(Qt::red);

    manager.setTransitionType(WeaR::SceneTransitionType::Fade);
    manager.setTransitionDuration(WeaR::SceneTransitionType::Fade, 50);
    manager.setActiveScene(sceneA);
    manager.renderFrame();

    manager.setActiveScene(sceneB);
    if (!manager.isTransitionActive()) {
        qCritical() << "SceneManager did not start Fade transition";
        return 1;
    }

    QThread::msleep(25);
    const QImage midFrame = manager.renderFrame();
    const int midRed = pixelRed(midFrame, 32, 18);
    if (!manager.isTransitionActive() || midRed < 40 || midRed > 220) {
        qCritical() << "SceneManager Fade integration failed" << midRed;
        return 1;
    }

    QThread::msleep(40);
    const QImage finalFrame = manager.renderFrame();
    if (manager.isTransitionActive() || pixelRed(finalFrame, 32, 18) < 247) {
        qCritical() << "SceneManager Fade completion failed";
        return 1;
    }

    manager.setTransitionType(WeaR::SceneTransitionType::Slide);
    manager.setTransitionDuration(WeaR::SceneTransitionType::Slide, 40);
    manager.setActiveScene(sceneA);
    if (!manager.isTransitionActive()) {
        qCritical() << "SceneManager did not start Slide transition";
        return 1;
    }

    QThread::msleep(50);
    const QImage slideFrame = manager.renderFrame();
    if (manager.isTransitionActive() || pixelRed(slideFrame, 32, 18) > 8) {
        qCritical() << "SceneManager Slide completion failed";
        return 1;
    }

    manager.setTransitionType(WeaR::SceneTransitionType::Cut);
    manager.setActiveScene(sceneB);
    if (manager.isTransitionActive()) {
        qCritical() << "SceneManager Cut transition did not remain immediate";
        return 1;
    }

    qDebug() << "SCENE_TRANSITIONS: PASS";
    return 0;
}
