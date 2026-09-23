// ==============================================================================
// WeaR-studio RecordingManager Integration Test
// Produces MP4/MKV/FLV samples through the independent recording pipeline.
// ==============================================================================

#include <QCoreApplication>
#include <QDir>
#include <QImage>
#include <QProcessEnvironment>
#include <QThread>

#include <iostream>
#include <vector>
#include <cmath>
#include <QFileInfo>

#include "RecordingManager.h"

using namespace WeaR;

namespace {

QString outputDirectory() {
    const QString configured = qEnvironmentVariable("WEAR_RECORDING_TEST_OUTPUT_DIR");
    if (!configured.isEmpty()) {
        return configured;
    }

    return QDir::temp().filePath(
        QString("wear-recording-test-%1").arg(QCoreApplication::applicationPid()));
}

QImage makeFrame(int index, int width, int height) {
    QImage image(width, height, QImage::Format_ARGB32);
    const int r = (index * 17) % 255;
    const int g = (index * 31) % 255;
    const int b = (index * 47) % 255;
    image.fill(qRgba(r, g, b, 255));
    return image;
}

AudioFrame makeAudioFrame(int samplesPerTick) {
    AudioFrame frame;
    frame.sampleRate = 48000;
    frame.channels = 2;
    frame.samples.resize(static_cast<std::size_t>(samplesPerTick * 2));

    static double phase = 0.0;
    constexpr double step = 2.0 * 3.14159265358979323846 * 440.0 / 48000.0;
    for (int i = 0; i < samplesPerTick; ++i) {
        const float sample = static_cast<float>(0.05 * std::sin(phase));
        frame.samples[static_cast<std::size_t>(i * 2)] = sample;
        frame.samples[static_cast<std::size_t>(i * 2 + 1)] = sample;
        phase += step;
        if (phase >= 2.0 * 3.14159265358979323846) {
            phase -= 2.0 * 3.14159265358979323846;
        }
    }
    return frame;
}

QString extension(RecordingFormat format) {
    switch (format) {
        case RecordingFormat::MP4: return "mp4";
        case RecordingFormat::MKV: return "mkv";
        case RecordingFormat::FLV: return "flv";
    }
    return "mkv";
}

bool runOne(RecordingFormat format, const QString& dir) {
    auto& recorder = RecordingManager::instance();

    RecordingSettings settings;
    settings.outputPath = QDir(dir).filePath(QString("recording-sample.%1").arg(extension(format)));
    settings.format = format;
    settings.width = 320;
    settings.height = 180;
    settings.fpsNum = 60;
    settings.fpsDen = 1;
    settings.videoBitrate = 1200;
    settings.maxVideoBitrate = 1800;
    settings.bufferSize = 2400;
    settings.crf = 28;
    settings.qp = 24;
    settings.encoderType = EncoderType::X264;
    settings.preset = EncoderPreset::UltraFast;
    settings.rateControl = RateControlMode::CRF;
    settings.keyframeInterval = 1;
    settings.bFrames = 0;
    settings.audioEnabled = true;
    settings.audioSampleRate = 48000;
    settings.audioChannels = 2;
    settings.audioBitrate = 96;

    if (!recorder.configure(settings)) {
        std::cerr << "configure() failed for " << extension(format).toStdString() << std::endl;
        return false;
    }

    if (!recorder.startRecording()) {
        std::cerr << "startRecording() failed for " << extension(format).toStdString() << std::endl;
        return false;
    }

    for (int i = 0; i < 45; ++i) {
        recorder.pushFrame(makeFrame(i, settings.width, settings.height));
        recorder.pushAudioFrame(makeAudioFrame(800));
        // Pace input close to the 60 FPS render cadence so the integration test
        // exercises the normal producer/consumer path instead of filling the
        // bounded raw-frame queue.
        QThread::msleep(16);
    }

    if (!recorder.pauseRecording()) {
        std::cerr << "pauseRecording() failed" << std::endl;
        recorder.stopRecording();
        return false;
    }

    QThread::msleep(50);

    if (!recorder.resumeRecording()) {
        std::cerr << "resumeRecording() failed" << std::endl;
        recorder.stopRecording();
        return false;
    }

    for (int i = 45; i < 90; ++i) {
        recorder.pushFrame(makeFrame(i, settings.width, settings.height));
        recorder.pushAudioFrame(makeAudioFrame(800));
        QThread::msleep(16);
    }

    recorder.stopRecording();

    const QFileInfo info(settings.outputPath);
    const bool validFile = info.exists() && info.size() > 4096;
    if (!validFile) {
        std::cerr << "Recording file missing or unexpectedly small: "
                  << settings.outputPath.toStdString() << std::endl;
        return false;
    }

    std::cout << "[PASS] " << extension(format).toStdString()
              << " recording: " << settings.outputPath.toStdString()
              << " (" << info.size() << " bytes)" << std::endl;
    return true;
}

} // namespace

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    const QString dir = outputDirectory();
    if (!QDir().mkpath(dir)) {
        std::cerr << "Unable to create test output directory: "
                  << dir.toStdString() << std::endl;
        return 1;
    }

    std::cout << "RecordingManager output directory: "
              << dir.toStdString() << std::endl;

    const std::vector<RecordingFormat> formats = {
        RecordingFormat::MP4,
        RecordingFormat::MKV,
        RecordingFormat::FLV
    };

    for (const auto format : formats) {
        if (!runOne(format, dir)) {
            return 1;
        }
    }

    std::cout << "All RecordingManager tests PASSED!" << std::endl;
    return 0;
}
