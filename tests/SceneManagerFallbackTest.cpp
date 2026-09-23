// ==============================================================================
// WeaR-studio SceneManager fallback integration test
// ==============================================================================

#include <QGuiApplication>
#include <QImage>
#include <iostream>

#include "SceneManager.h"
#include "Scene.h"
#include "SceneItem.h"
#include "ISource.h"

using namespace WeaR;

namespace {

class FallbackSource final : public ISource {
public:
    explicit FallbackSource(const QImage& image) : m_image(image) {}

    PluginInfo info() const override {
        return {
            QStringLiteral("wear.test.fallback"),
            QStringLiteral("Fallback Source"),
            QStringLiteral("Synthetic fallback source"),
            QStringLiteral("1.0.0"),
            QStringLiteral("WeaR-studio"),
            {},
            PluginType::Source,
            PluginCapability::HasVideo
        };
    }

    QString name() const override { return QStringLiteral("Fallback Source"); }
    QString version() const override { return QStringLiteral("1.0.0"); }
    PluginCapability capabilities() const override {
        return PluginCapability::HasVideo;
    }

    bool initialize() override { return true; }
    void shutdown() override {}
    bool isActive() const override { return true; }
    bool configure(const SourceConfig&) override { return true; }
    SourceConfig config() const override { return {}; }
    bool start() override { return true; }
    void stop() override {}
    bool isRunning() const override { return true; }

    VideoFrame captureVideoFrame() override {
        VideoFrame frame;
        frame.softwareFrame = m_image;
        return frame;
    }

    QSize nativeResolution() const override { return m_image.size(); }
    double nativeFps() const override { return 60.0; }
    QSize outputResolution() const override { return m_image.size(); }
    double outputFps() const override { return 60.0; }

private:
    QImage m_image;
};

} // namespace

int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);

    SceneManager& manager = SceneManager::instance();
    manager.setOutputResolution(64, 36);

    Scene* scene = manager.createScene(QStringLiteral("QPainter Fallback Test"));
    scene->setResolution(64, 36);
    scene->setBackgroundColor(Qt::white);
    manager.setActiveScene(scene);

    QImage sourceImage(64, 36, QImage::Format_RGBA8888);
    sourceImage.fill(qRgba(220, 30, 30, 255));

    FallbackSource source(sourceImage);
    SceneItem* item = scene->addItem(QStringLiteral("Fallback Source"), &source);
    item->setPosition(0.0, 0.0);
    item->setSize(QSizeF(64, 36));
    item->setBlendMode(BlendMode::Multiply);

    QImage output = manager.renderFrame();
    if (output.isNull() || output.size() != QSize(64, 36)) {
        std::cerr << "QPainter fallback returned an invalid frame" << std::endl;
        return 1;
    }

    const QRgb pixel = output.pixel(32, 18);
    if (qRed(pixel) < 150 || qGreen(pixel) > 80 || qBlue(pixel) > 80) {
        std::cerr << "QPainter fallback blend result is incorrect" << std::endl;
        return 1;
    }

    const RenderStatistics stats = manager.statistics();
    if (stats.compositingBackend != QStringLiteral("QPainter")) {
        std::cerr << "Expected QPainter fallback backend, got: "
                  << stats.compositingBackend.toStdString() << std::endl;
        return 1;
    }

    std::cout << "QPAINTER_FALLBACK: PASS" << std::endl;
    return 0;
}
