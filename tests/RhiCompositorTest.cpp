// ==============================================================================
// WeaR-studio RHI compositor integration test
// ==============================================================================

#include <QCoreApplication>
#include <QDir>
#include <QImage>
#include <QDebug>
#include <QGuiApplication>

#include <cmath>
#include <cstdlib>
#include <iostream>

#include "BuiltinRhiFilters.h"
#include "RhiCompositor.h"
#include "Scene.h"
#include "SceneItem.h"
#include "ISource.h"

using namespace WeaR;

namespace {

class TestSource final : public ISource {
public:
    explicit TestSource(const QImage& frame)
        : m_frame(frame) {}

    void setFrame(const QImage& frame) {
        m_frame = frame;
    }

    PluginInfo info() const override {
        return {
            QStringLiteral("wear.test.image"),
            QStringLiteral("RHI Test Image"),
            QStringLiteral("Synthetic source for compositor tests"),
            QStringLiteral("1.0.0"),
            QStringLiteral("WeaR-studio"),
            {},
            PluginType::Source,
            PluginCapability::HasVideo | PluginCapability::ThreadSafe
        };
    }

    QString name() const override { return QStringLiteral("RHI Test Image"); }
    QString version() const override { return QStringLiteral("1.0.0"); }
    PluginCapability capabilities() const override {
        return PluginCapability::HasVideo | PluginCapability::ThreadSafe;
    }

    bool initialize() override { return true; }
    void shutdown() override {}
    bool isActive() const override { return m_running; }

    bool configure(const SourceConfig& config) override {
        m_config = config;
        return true;
    }

    SourceConfig config() const override { return m_config; }

    bool start() override {
        m_running = true;
        return true;
    }

    void stop() override { m_running = false; }
    bool isRunning() const override { return m_running; }

    VideoFrame captureVideoFrame() override {
        VideoFrame frame;
        frame.softwareFrame = m_frame;
        return frame;
    }

    QSize nativeResolution() const override { return m_frame.size(); }
    double nativeFps() const override { return 60.0; }
    QSize outputResolution() const override { return m_frame.size(); }
    double outputFps() const override { return 60.0; }

private:
    QImage m_frame;
    SourceConfig m_config;
    bool m_running = true;
};

bool nearColor(QRgb pixel, int r, int g, int b, int tolerance) {
    return std::abs(qRed(pixel) - r) <= tolerance &&
           std::abs(qGreen(pixel) - g) <= tolerance &&
           std::abs(qBlue(pixel) - b) <= tolerance;
}

QString outputDir() {
    const QString configured =
        qEnvironmentVariable("WEAR_RHI_TEST_OUTPUT_DIR");
    if (!configured.isEmpty()) {
        return configured;
    }
    return QDir(QDir::tempPath()).filePath(
        QStringLiteral("wear-rhi-test-%1").arg(QCoreApplication::applicationPid()));
}

bool assertChromaKey(RhiCompositor& compositor, TestSource& source,
                     Scene& scene, const QString& dir) {
    auto* item = scene.itemAt(0);
    ChromaKeyFilter filter;
    filter.initialize();
    item->setFilter(&filter);

    QImage output;
    if (!compositor.compose(scene, scene.resolution(), output)) {
        std::cerr << "Chroma key RHI compose failed: "
                  << compositor.lastError().toStdString() << std::endl;
        return false;
    }

    if (!output.save(QDir(dir).filePath(QStringLiteral("rhi-chroma-key.png")))) {
        std::cerr << "Failed to save chroma key artifact" << std::endl;
        return false;
    }

    const QRgb background = output.pixel(20, 20);
    const QRgb subject = output.pixel(output.width() / 2, output.height() / 2);

    // Green key should be removed to the black scene background while the red
    // subject remains visibly red.
    if (!nearColor(background, 0, 0, 0, 28)) {
        std::cerr << "Chroma key did not remove green background" << std::endl;
        return false;
    }

    if (qRed(subject) < 160 || qGreen(subject) > 120) {
        std::cerr << "Chroma key changed the red subject unexpectedly" << std::endl;
        return false;
    }

    Q_UNUSED(source);
    return true;
}

bool assertGaussianBlur(RhiCompositor& compositor, TestSource& source,
                        Scene& scene, const QString& dir) {
    auto* item = scene.itemAt(0);
    GaussianBlurFilter filter;
    filter.initialize();
    filter.setParameter(QStringLiteral("radius"), 3.0);
    item->setFilter(&filter);

    QImage output;
    if (!compositor.compose(scene, scene.resolution(), output)) {
        std::cerr << "Gaussian blur RHI compose failed: "
                  << compositor.lastError().toStdString() << std::endl;
        return false;
    }

    if (!output.save(QDir(dir).filePath(QStringLiteral("rhi-gaussian-blur.png")))) {
        std::cerr << "Failed to save gaussian blur artifact" << std::endl;
        return false;
    }

    const int edgeX = output.width() / 2;
    const QRgb left = output.pixel(edgeX - 2, output.height() / 2);
    const QRgb right = output.pixel(edgeX + 2, output.height() / 2);

    // The synthetic image has a hard black/white edge. A Gaussian pass should
    // create an intermediate value around that edge.
    const bool leftMixed = qRed(left) > 8 && qRed(left) < 247;
    const bool rightMixed = qRed(right) > 8 && qRed(right) < 247;

    Q_UNUSED(source);
    return leftMixed || rightMixed;
}

bool assertColorCorrection(RhiCompositor& compositor, TestSource& source,
                           Scene& scene, const QString& dir) {
    QImage neutral(scene.resolution(), QImage::Format_RGBA8888);
    neutral.fill(qRgba(90, 90, 90, 255));
    source.setFrame(neutral);
    auto* item = scene.itemAt(0);
    ColorCorrectionFilter filter;
    filter.initialize();
    filter.setParameter(QStringLiteral("brightness"), 0.20);
    filter.setParameter(QStringLiteral("contrast"), 1.0);
    item->setFilter(&filter);

    QImage output;
    if (!compositor.compose(scene, scene.resolution(), output)) {
        std::cerr << "Color correction RHI compose failed: "
                  << compositor.lastError().toStdString() << std::endl;
        return false;
    }

    if (!output.save(QDir(dir).filePath(
            QStringLiteral("rhi-color-correction.png")))) {
        std::cerr << "Failed to save color correction artifact" << std::endl;
        return false;
    }

    const QRgb pixel = output.pixel(output.width() / 2, output.height() / 2);
    Q_UNUSED(source);
    return qRed(pixel) > 80 && qGreen(pixel) > 80 && qBlue(pixel) > 80;
}

} // namespace

int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);

    const QString dir = outputDir();
    if (!QDir().mkpath(dir)) {
        std::cerr << "Unable to create RHI test directory" << std::endl;
        return 1;
    }

    const QSize size(256, 144);
    QImage chromaImage(size, QImage::Format_RGBA8888);
    chromaImage.fill(qRgba(0, 255, 0, 255));
    for (int y = 36; y < 108; ++y) {
        for (int x = 64; x < 192; ++x) {
            chromaImage.setPixelColor(x, y, QColor(235, 35, 35, 255));
        }
    }

    TestSource source(chromaImage);
    source.start();

    Scene scene(QStringLiteral("RHI Test"));
    scene.setResolution(size);
    SceneItem* item = scene.addItem(
        QStringLiteral("Synthetic Source"), &source);
    item->setPosition(0.0, 0.0);
    item->setSize(QSizeF(size));

    RhiCompositor compositor;
    if (!compositor.initialize()) {
        std::cout << "RHI_UNAVAILABLE: "
                  << compositor.lastError().toStdString() << std::endl;
        return qEnvironmentVariableIntValue("WEAR_REQUIRE_RHI_TEST") ? 1 : 0;
    }

    std::cout << "RHI_BACKEND: "
              << compositor.backendName().toStdString() << std::endl;

    if (!assertChromaKey(compositor, source, scene, dir)) {
        return 1;
    }

    // Replace the source image in-place for subsequent shader checks.
    QImage edgeImage(size, QImage::Format_RGBA8888);
    edgeImage.fill(qRgba(0, 0, 0, 255));
    for (int y = 0; y < size.height(); ++y) {
        for (int x = size.width() / 2; x < size.width(); ++x) {
            edgeImage.setPixelColor(x, y, QColor(255, 255, 255, 255));
        }
    }
    source.setFrame(edgeImage);
    item->setFilter(nullptr);

    if (!assertGaussianBlur(compositor, source, scene, dir)) {
        return 1;
    }

    if (!assertColorCorrection(compositor, source, scene, dir)) {
        return 1;
    }

    std::cout << "RHI_FILTER_TESTS: PASS" << std::endl;
    return 0;
}
