// ==============================================================================
// WeaR-studio AudioMixer Unit Test
// Tests AudioMixer track addition, removal, volume, mute, and mixTracks()
// ==============================================================================

#include <QCoreApplication>
#include <QDebug>
#include <iostream>
#include <cassert>
#include <cmath>

#include "AudioMixer.h"
#include "ISource.h"

using namespace WeaR;

// Dummy test audio source that outputs constant float values
class DummyAudioSource : public ISource {
public:
    explicit DummyAudioSource(const QString& name, float sampleValue)
        : m_name(name), m_sampleValue(sampleValue) {}

    PluginInfo info() const override {
        PluginInfo pi;
        pi.id = "dummy." + m_name;
        pi.name = m_name;
        return pi;
    }
    QString name() const override { return m_name; }
    QString version() const override { return "1.0.0"; }

    bool initialize() override { return true; }
    void shutdown() override {}
    bool isActive() const override { return m_running; }

    bool configure(const SourceConfig&) override { return true; }
    SourceConfig config() const override { return SourceConfig{}; }

    bool start() override { m_running = true; return true; }
    void stop() override { m_running = false; }
    bool isRunning() const override { return m_running; }

    VideoFrame captureVideoFrame() override { return VideoFrame{}; }
    AudioFrame captureAudioFrame() override {
        AudioFrame frame;
        frame.sampleRate = 48000;
        frame.channels = 2;
        frame.timestamp = 0;
        frame.samples.resize(800 * 2, m_sampleValue);
        return frame;
    }

    PluginCapability capabilities() const override { return PluginCapability::HasAudio; }

    QSize nativeResolution() const override { return QSize{}; }
    double nativeFps() const override { return 0.0; }
    QSize outputResolution() const override { return QSize{}; }
    double outputFps() const override { return 0.0; }

private:
    QString m_name;
    float m_sampleValue = 0.0f;
    bool m_running = false;
};

bool runTests() {
    auto& mixer = AudioMixer::instance();

    // Ensure clean initial state
    auto initialTracks = mixer.tracks();
    for (const auto& t : initialTracks) {
        mixer.removeTrack(t.id);
    }
    assert(mixer.trackCount() == 0);
    std::cout << "[PASS] Clean initial state" << std::endl;

    // Test 1: Add track
    auto sourceA = std::make_shared<DummyAudioSource>("TrackA", 0.4f);
    int idA = mixer.addTrack(sourceA);
    assert(idA >= 0);
    assert(mixer.trackCount() == 1);
    assert(mixer.track(idA).id == idA);
    assert(mixer.track(idA).name == "TrackA");
    std::cout << "[PASS] Add track" << std::endl;

    // Test 2: Mix single track at 1.0 volume
    int samplesToMix = 100;
    AudioFrame mixed = mixer.mixTracks(samplesToMix);
    assert(mixed.samples.size() == samplesToMix * 2);
    for (float s : mixed.samples) {
        assert(std::abs(s - 0.4f) < 1e-4f);
    }
    std::cout << "[PASS] Single track mix (unity gain)" << std::endl;

    // Test 3: Track volume control (50%)
    mixer.setTrackVolume(idA, 0.5f);
    mixed = mixer.mixTracks(samplesToMix);
    for (float s : mixed.samples) {
        assert(std::abs(s - 0.2f) < 1e-4f);
    }
    std::cout << "[PASS] Track volume scaling (50%)" << std::endl;

    // Test 4: Track mute
    mixer.setTrackMuted(idA, true);
    mixed = mixer.mixTracks(samplesToMix);
    for (float s : mixed.samples) {
        assert(std::abs(s - 0.0f) < 1e-5f);
    }
    std::cout << "[PASS] Track mute" << std::endl;

    // Unmute Track A and restore volume
    mixer.setTrackMuted(idA, false);
    mixer.setTrackVolume(idA, 1.0f);

    // Test 5: Multi-track mixing
    auto sourceB = std::make_shared<DummyAudioSource>("TrackB", 0.3f);
    int idB = mixer.addTrack(sourceB);
    assert(mixer.trackCount() == 2);

    mixed = mixer.mixTracks(samplesToMix);
    for (float s : mixed.samples) {
        // Expected: 0.4 + 0.3 = 0.7
        assert(std::abs(s - 0.7f) < 1e-4f);
    }
    std::cout << "[PASS] Multi-track mix (0.4 + 0.3 = 0.7)" << std::endl;

    // Test 6: Limiting / Clamping
    auto sourceC = std::make_shared<DummyAudioSource>("TrackC", 0.8f);
    int idC = mixer.addTrack(sourceC);
    // TrackA (0.4) + TrackB (0.3) + TrackC (0.8) = 1.5 -> clamped to 1.0
    mixed = mixer.mixTracks(samplesToMix);
    for (float s : mixed.samples) {
        assert(s <= 1.0f && s >= -1.0f);
        assert(std::abs(s - 1.0f) < 1e-4f);
    }
    std::cout << "[PASS] Multi-track limiting / clamping (clamped to 1.0)" << std::endl;

    // Test 7: Remove track
    mixer.removeTrack(idC);
    assert(mixer.trackCount() == 2);
    mixer.removeTrack(idB);
    assert(mixer.trackCount() == 1);
    mixer.removeTrack(idA);
    assert(mixer.trackCount() == 0);
    std::cout << "[PASS] Track removal" << std::endl;

    // Empty mix should return silence
    mixed = mixer.mixTracks(samplesToMix);
    assert(mixed.samples.size() == samplesToMix * 2);
    for (float s : mixed.samples) {
        assert(s == 0.0f);
    }
    std::cout << "[PASS] Empty mixer produces silence" << std::endl;

    return true;
}

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    std::cout << "Running AudioMixer Unit Tests..." << std::endl;

    if (runTests()) {
        std::cout << "All AudioMixer tests PASSED!" << std::endl;
        return 0;
    } else {
        std::cerr << "AudioMixer tests FAILED!" << std::endl;
        return 1;
    }
}
