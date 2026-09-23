#include "BuiltinRhiFilters.h"
#include "ProjectPersistence.h"
#include "SceneManager.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QDebug>

namespace {

class TestSource final : public QObject, public WeaR::ISource {
public:
    TestSource()
        : QObject(nullptr) {}

    WeaR::PluginInfo info() const override {
        return {
            QStringLiteral("test.source"),
            QStringLiteral("Test Source"),
            QStringLiteral("Persistence test source"),
            QStringLiteral("1.0"),
            QStringLiteral("WeaR tests"),
            QString(),
            WeaR::PluginType::Source,
            WeaR::PluginCapability::HasVideo |
                WeaR::PluginCapability::ThreadSafe
        };
    }

    QString name() const override { return QStringLiteral("Test Source"); }
    QString version() const override { return QStringLiteral("1.0"); }
    WeaR::PluginType type() const override {
        return WeaR::PluginType::Source;
    }
    WeaR::PluginCapability capabilities() const override {
        return WeaR::PluginCapability::HasVideo |
               WeaR::PluginCapability::ThreadSafe;
    }

    bool initialize() override { m_active = true; return true; }
    void shutdown() override { m_running = false; m_active = false; }
    bool isActive() const override { return m_active; }

    bool configure(const WeaR::SourceConfig& config) override {
        m_config = config;
        return config.resolution.isValid() && config.fps > 0.0;
    }
    WeaR::SourceConfig config() const override { return m_config; }

    bool start() override {
        m_running = m_active = true;
        return true;
    }
    void stop() override { m_running = false; }
    bool isRunning() const override { return m_running; }

    WeaR::VideoFrame captureVideoFrame() override {
        WeaR::VideoFrame frame;
        frame.softwareFrame = QImage(
            m_config.resolution,
            QImage::Format_ARGB32_Premultiplied);
        frame.softwareFrame.fill(Qt::red);
        frame.frameNumber = ++m_frameNumber;
        return frame;
    }

    QSize nativeResolution() const override { return m_config.resolution; }
    double nativeFps() const override { return m_config.fps; }
    QSize outputResolution() const override { return m_config.resolution; }
    double outputFps() const override { return m_config.fps; }

private:
    WeaR::SourceConfig m_config{
        QSize(1280, 720), 30.0, false, {}, QStringLiteral("test-device")
    };
    bool m_active = false;
    bool m_running = false;
    int64_t m_frameNumber = 0;
};

bool saveDocument(
    const QString& path,
    const QJsonDocument& document) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    return file.write(document.toJson(QJsonDocument::Compact)) >= 0;
}

QJsonDocument readDocument(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(file.readAll());
}

bool check(bool condition, const QString& message) {
    if (!condition) qCritical() << message;
    return condition;
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir temp;
    if (!check(temp.isValid(), QStringLiteral("Temporary directory failed"))) {
        return 1;
    }

    auto& sceneManager = WeaR::SceneManager::instance();

    WeaR::StreamSettings stream;
    stream.url = QStringLiteral("rtmp://example.test/app");
    stream.streamKey = QStringLiteral("test-key");
    stream.service = WeaR::StreamService::Custom;
    stream.connectTimeout = 17;
    stream.reconnectDelay = 9;
    stream.maxReconnectAttempts = 7;
    stream.sendBufferSize = 262144;
    stream.videoWidth = 2560;
    stream.videoHeight = 1440;
    stream.videoFpsNum = 60000;
    stream.videoFpsDen = 1001;
    stream.videoBitrate = 8500;
    stream.audioEnabled = true;
    stream.audioSampleRate = 44100;
    stream.audioChannels = 2;
    stream.audioBitrate = 192;

    WeaR::EncoderSettings encoder;
    encoder.width = 2560;
    encoder.height = 1440;
    encoder.fpsNum = 60000;
    encoder.fpsDen = 1001;
    encoder.bitrate = 8500;
    encoder.maxBitrate = 11000;
    encoder.bufferSize = 22000;
    encoder.crf = 19;
    encoder.qp = 21;
    encoder.encoderType = WeaR::EncoderType::X264;
    encoder.preset = WeaR::EncoderPreset::Medium;
    encoder.rateControl = WeaR::RateControlMode::VBR;
    encoder.keyframeInterval = 3;
    encoder.bFrames = 2;
    encoder.profile = QStringLiteral("high");
    encoder.level = QStringLiteral("5.1");
    encoder.nvencLowLatency = false;
    encoder.nvencZeroLatency = true;
    encoder.threads = 8;
    encoder.audioEnabled = true;
    encoder.audioSampleRate = 44100;
    encoder.audioChannels = 2;
    encoder.audioBitrate = 192;

    WeaR::RecordingSettings recording;
    recording.outputPath = temp.filePath(QStringLiteral("session.mkv"));
    recording.format = WeaR::RecordingFormat::MKV;
    recording.width = 2560;
    recording.height = 1440;
    recording.fpsNum = 60000;
    recording.fpsDen = 1001;
    recording.videoBitrate = 14000;
    recording.maxVideoBitrate = 18000;
    recording.bufferSize = 28000;
    recording.crf = 17;
    recording.qp = 16;
    recording.encoderType = WeaR::EncoderType::NVENC_H264;
    recording.preset = WeaR::EncoderPreset::Fast;
    recording.rateControl = WeaR::RateControlMode::CRF;
    recording.keyframeInterval = 4;
    recording.bFrames = 3;
    recording.threads = 4;
    recording.audioEnabled = true;
    recording.audioSampleRate = 48000;
    recording.audioChannels = 2;
    recording.audioBitrate = 256;

    const QString profileA = temp.filePath(QStringLiteral("profile-a.json"));
    const QString profileB = temp.filePath(QStringLiteral("profile-b.json"));

    QString error;
    if (!WeaR::ProjectPersistence::saveProfile(
            profileA, stream, encoder, recording,
            QSize(2560, 1440), 59.94, &error)) {
        qCritical() << error;
        return 1;
    }

    WeaR::StreamSettings loadedStream;
    WeaR::EncoderSettings loadedEncoder;
    WeaR::RecordingSettings loadedRecording;
    QSize loadedResolution;
    double loadedFps = 0.0;

    if (!WeaR::ProjectPersistence::loadProfile(
            profileA, loadedStream, loadedEncoder, loadedRecording,
            loadedResolution, loadedFps, &error)) {
        qCritical() << error;
        return 1;
    }

    if (!WeaR::ProjectPersistence::saveProfile(
            profileB, loadedStream, loadedEncoder, loadedRecording,
            loadedResolution, loadedFps, &error)) {
        qCritical() << error;
        return 1;
    }

    if (!check(
            readDocument(profileA) == readDocument(profileB),
            QStringLiteral("Profile JSON round-trip mismatch"))) {
        return 1;
    }

    auto* source = new TestSource();
    source->initialize();
    source->configure({
        QSize(1280, 720), 30.0, false,
        QRect(10, 20, 640, 480),
        QStringLiteral("device-A")
    });
    source->start();

    auto* filter = new WeaR::ChromaKeyFilter();
    filter->initialize();
    filter->setParameter(QStringLiteral("threshold"), 0.31);
    filter->setParameter(QStringLiteral("softness"), 0.05);

    sceneManager.stopRenderLoop();
    auto scenes = sceneManager.scenes();
    for (int i = scenes.size() - 1; i >= 1; --i) {
        sceneManager.removeScene(scenes.at(i));
    }
    auto current = sceneManager.scenes();
    if (!check(!current.isEmpty(), QStringLiteral("SceneManager has no scene"))) {
        return 1;
    }

    WeaR::Scene* scene1 = current.first();
    scene1->clear();
    scene1->setName(QStringLiteral("Program"));
    scene1->setResolution(QSize(1280, 720));
    scene1->setBackgroundColor(QColor(12, 34, 56, 255));

    auto* item1 = scene1->addItem(QStringLiteral("Camera"), source);
    item1->setTransform({
        QPointF(123.5, 77.25),
        QSizeF(960.0, 540.0),
        7.5,
        QPointF(-1.0, 1.25),
        QPointF(0.25, 0.75),
        0.82,
        true,
        false
    });
    item1->setVisible(false);
    item1->setLocked(true);
    item1->setBlendMode(WeaR::BlendMode::Screen);
    item1->setFilter(filter);

    WeaR::Scene* scene2 = sceneManager.createScene(QStringLiteral("BRB"));
    scene2->setResolution(QSize(1280, 720));
    scene2->setBackgroundColor(QColor("#112233"));
    auto* item2 = scene2->addItem(QStringLiteral("Camera 2"), source);
    item2->setPosition(10.25, 20.5);
    item2->setSize(320.5, 240.25);
    item2->setOpacity(0.55);

    sceneManager.setActiveScene(scene2);

    const QString scenesA = temp.filePath(QStringLiteral("scenes-a.json"));
    const QString scenesB = temp.filePath(QStringLiteral("scenes-b.json"));

    if (!WeaR::ProjectPersistence::saveSceneCollection(
            scenesA, sceneManager, &error)) {
        qCritical() << error;
        return 1;
    }

    auto sourceResolver = [&](const QString& id) -> WeaR::ISource* {
        return id == QStringLiteral("test.source") ? source : nullptr;
    };
    auto filterResolver = [&](const QString& id) -> WeaR::IFilter* {
        return id == QStringLiteral("wear.filter.chroma_key")
            ? static_cast<WeaR::IFilter*>(filter)
            : nullptr;
    };

    if (!WeaR::ProjectPersistence::loadSceneCollection(
            scenesA, sceneManager, sourceResolver, filterResolver, &error)) {
        qCritical() << error;
        return 1;
    }

    if (!WeaR::ProjectPersistence::saveSceneCollection(
            scenesB, sceneManager, &error)) {
        qCritical() << error;
        return 1;
    }

    if (!check(
            readDocument(scenesA) == readDocument(scenesB),
            QStringLiteral("Scene Collection JSON round-trip mismatch"))) {
        return 1;
    }

    const auto restoredScenes = sceneManager.scenes();
    if (!check(restoredScenes.size() == 2, QStringLiteral("Scene count mismatch"))) return 1;
    if (!check(sceneManager.activeScene()->name() == QStringLiteral("BRB"),
               QStringLiteral("Active scene mismatch"))) return 1;
    if (!check(restoredScenes.first()->itemCount() == 1,
               QStringLiteral("Program item count mismatch"))) return 1;

    const auto restoredItem = restoredScenes.first()->itemAt(0);
    if (!check(!restoredItem->isVisible() && restoredItem->isLocked(),
               QStringLiteral("Item state mismatch"))) return 1;
    if (!check(restoredItem->blendMode() == WeaR::BlendMode::Screen,
               QStringLiteral("Blend mode mismatch"))) return 1;
    if (!check(restoredItem->filter() == filter,
               QStringLiteral("Filter resolver mismatch"))) return 1;

    filter->shutdown();
    source->shutdown();
    delete filter;
    delete source;

    qDebug() << "PROJECT_PERSISTENCE: PASS";
    return 0;
}
