// ==============================================================================
// WeaR-studio compositing benchmark
// Compares QPainter scene composition to the Qt RHI compositor at equal
// resolution/FPS/layer count and reports CPU time/estimated CPU budget.
// ==============================================================================

#include <QElapsedTimer>
#include <QGuiApplication>
#include <QImage>
#include <QDir>
#include <QThread>

#include <cmath>
#include <iostream>
#include <vector>
#include <stdexcept>

#include "RhiCompositor.h"
#include "Scene.h"
#include "SceneItem.h"
#include "ISource.h"

#ifdef Q_OS_WIN
#  include <windows.h>
#elif defined(Q_OS_UNIX)
#  include <time.h>
#endif

using namespace WeaR;

namespace {

class BenchmarkSource final : public ISource {
public:
    explicit BenchmarkSource(const QImage& image) : m_image(image) {}

    PluginInfo info() const override {
        return {
            QStringLiteral("wear.test.benchmark"),
            QStringLiteral("Benchmark Source"),
            QStringLiteral("Synthetic benchmark source"),
            QStringLiteral("1.0.0"),
            QStringLiteral("WeaR-studio"),
            {},
            PluginType::Source,
            PluginCapability::HasVideo | PluginCapability::ThreadSafe
        };
    }

    QString name() const override { return QStringLiteral("Benchmark Source"); }
    QString version() const override { return QStringLiteral("1.0.0"); }
    PluginCapability capabilities() const override {
        return PluginCapability::HasVideo | PluginCapability::ThreadSafe;
    }

    bool initialize() override { return true; }
    void shutdown() override {}
    bool isActive() const override { return true; }
    bool configure(const SourceConfig& config) override {
        m_config = config;
        return true;
    }
    SourceConfig config() const override { return m_config; }
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
    SourceConfig m_config;
};

qint64 threadCpuNsecs() {
#ifdef Q_OS_WIN
    FILETIME creation{};
    FILETIME exit{};
    FILETIME kernel{};
    FILETIME user{};
    if (!GetThreadTimes(GetCurrentThread(), &creation, &exit, &kernel, &user)) {
        return -1;
    }
    ULARGE_INTEGER k{};
    k.LowPart = kernel.dwLowDateTime;
    k.HighPart = kernel.dwHighDateTime;
    ULARGE_INTEGER u{};
    u.LowPart = user.dwLowDateTime;
    u.HighPart = user.dwHighDateTime;
    return static_cast<qint64>((k.QuadPart + u.QuadPart) * 100ULL);
#elif defined(Q_OS_UNIX)
    timespec ts{};
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts) != 0) {
        return -1;
    }
    return static_cast<qint64>(ts.tv_sec) * 1000000000LL +
           static_cast<qint64>(ts.tv_nsec);
#else
    return -1;
#endif
}

struct BenchmarkResult {
    double cpuMsPerFrame = 0.0;
    double wallMsPerFrame = 0.0;
    double cpuBudgetPercentAtTargetFps = 0.0;
    double effectiveFps = 0.0;
};

BenchmarkResult runQPainter(Scene& scene, int frames) {
    for (int i = 0; i < 15; ++i) {
        (void)scene.render();
    }

    QElapsedTimer wall;
    wall.start();
    const qint64 cpuStart = threadCpuNsecs();

    for (int i = 0; i < frames; ++i) {
        (void)scene.render();
    }

    const qint64 cpuEnd = threadCpuNsecs();
    const double wallMs = wall.nsecsElapsed() / 1000000.0;
    const double cpuMs = cpuStart >= 0 && cpuEnd >= cpuStart
        ? static_cast<double>(cpuEnd - cpuStart) / 1000000.0
        : wallMs;

    return {
        cpuMs / frames,
        wallMs / frames,
        (cpuMs / frames) / (1000.0 / 60.0) * 100.0,
        1000.0 / (wallMs / frames)
    };
}

BenchmarkResult runRhi(RhiCompositor& compositor, Scene& scene, int frames) {
    QImage output;
    for (int i = 0; i < 15; ++i) {
        if (!compositor.compose(scene, scene.resolution(), output)) {
            throw std::runtime_error(
                compositor.lastError().toStdString());
        }
    }

    QElapsedTimer wall;
    wall.start();
    const qint64 cpuStart = threadCpuNsecs();

    for (int i = 0; i < frames; ++i) {
        if (!compositor.compose(scene, scene.resolution(), output)) {
            throw std::runtime_error(
                compositor.lastError().toStdString());
        }
    }

    const qint64 cpuEnd = threadCpuNsecs();
    const double wallMs = wall.nsecsElapsed() / 1000000.0;
    const double cpuMs = cpuStart >= 0 && cpuEnd >= cpuStart
        ? static_cast<double>(cpuEnd - cpuStart) / 1000000.0
        : wallMs;

    return {
        cpuMs / frames,
        wallMs / frames,
        (cpuMs / frames) / (1000.0 / 60.0) * 100.0,
        1000.0 / (wallMs / frames)
    };
}

} // namespace

int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);

    const int width = qEnvironmentVariableIntValue("WEAR_BENCHMARK_WIDTH") > 0
        ? qEnvironmentVariableIntValue("WEAR_BENCHMARK_WIDTH")
        : 1920;
    const int height = qEnvironmentVariableIntValue("WEAR_BENCHMARK_HEIGHT") > 0
        ? qEnvironmentVariableIntValue("WEAR_BENCHMARK_HEIGHT")
        : 1080;
    const int layers = qEnvironmentVariableIntValue("WEAR_BENCHMARK_LAYERS") > 0
        ? qEnvironmentVariableIntValue("WEAR_BENCHMARK_LAYERS")
        : 4;
    const int frames = qEnvironmentVariableIntValue("WEAR_BENCHMARK_FRAMES") > 0
        ? qEnvironmentVariableIntValue("WEAR_BENCHMARK_FRAMES")
        : 60;

    const QSize size(width, height);
    QImage layerImage(size, QImage::Format_RGBA8888);

    for (int y = 0; y < height; ++y) {
        auto* scan = reinterpret_cast<QRgb*>(layerImage.scanLine(y));
        for (int x = 0; x < width; ++x) {
            const int band = (x / 64 + y / 64) % 4;
            scan[x] = band == 0
                ? qRgba(220, 70, 70, 220)
                : band == 1
                    ? qRgba(70, 220, 120, 190)
                    : band == 2
                        ? qRgba(80, 120, 230, 180)
                        : qRgba(220, 200, 80, 160);
        }
    }

    Scene scene(QStringLiteral("Compositing Benchmark"));
    scene.setResolution(size);

    std::vector<std::unique_ptr<BenchmarkSource>> sources;
    sources.reserve(static_cast<std::size_t>(layers));

    for (int i = 0; i < layers; ++i) {
        sources.push_back(std::make_unique<BenchmarkSource>(layerImage));
        SceneItem* item = scene.addItem(
            QStringLiteral("Layer %1").arg(i + 1),
            sources.back().get());
        const double inset = i * 24.0;
        item->setPosition(inset, inset);
        item->setSize(QSizeF(
            std::max(64.0, static_cast<double>(width) - inset * 2.0),
            std::max(64.0, static_cast<double>(height) - inset * 2.0)));
        item->setOpacity(0.75);
    }

    std::cout << "COMPOSITING_BENCHMARK resolution="
              << width << "x" << height
              << " fps=60 layers=" << layers
              << " frames=" << frames << std::endl;

    const BenchmarkResult cpu = runQPainter(scene, frames);

    RhiCompositor compositor;
    if (!compositor.initialize()) {
        std::cout << "RHI_UNAVAILABLE: "
                  << compositor.lastError().toStdString() << std::endl;
        return 0;
    }

    const BenchmarkResult rhi = runRhi(compositor, scene, frames);

    const double reduction = cpu.cpuMsPerFrame > 0.0
        ? (cpu.cpuMsPerFrame - rhi.cpuMsPerFrame) /
              cpu.cpuMsPerFrame * 100.0
        : 0.0;

    std::cout << "QPAINTER cpu_ms_frame=" << cpu.cpuMsPerFrame
              << " wall_ms_frame=" << cpu.wallMsPerFrame
              << " cpu_budget_at_60fps=" << cpu.cpuBudgetPercentAtTargetFps
              << " effective_fps=" << cpu.effectiveFps << std::endl;

    std::cout << "RHI backend=" << compositor.backendName().toStdString()
              << " cpu_ms_frame=" << rhi.cpuMsPerFrame
              << " wall_ms_frame=" << rhi.wallMsPerFrame
              << " cpu_budget_at_60fps=" << rhi.cpuBudgetPercentAtTargetFps
              << " effective_fps=" << rhi.effectiveFps << std::endl;

    std::cout << "CPU_REDUCTION_PERCENT=" << reduction << std::endl;

    return 0;
}
