// ==============================================================================
// WeaR-studio AudioMixer Implementation
// ==============================================================================

#include "AudioMixer.h"

#include <QDateTime>
#include <QDebug>
#include <algorithm>
#include <cmath>

namespace WeaR {

AudioMixer& AudioMixer::instance() {
    static AudioMixer s_instance;
    return s_instance;
}

AudioMixer::AudioMixer(QObject* parent)
    : QObject(parent)
{
}

int AudioMixer::addTrack(const QString& name, ISource* source, float volume) {
    QMutexLocker lock(&m_mutex);
    int id = m_nextTrackId++;

    AudioTrack track;
    track.id = id;
    track.name = name;
    track.source = source;
    track.volume = std::clamp(volume, 0.0f, 2.0f);
    track.muted = false;
    track.currentPeak = 0.0f;

    m_tracks.insert(id, track);
    qDebug() << "Audio track added:" << name << "(ID:" << id << ")";

    lock.unlock();
    emit trackAdded(id, name);
    return id;
}

int AudioMixer::addTrack(ISource* source, float volume) {
    return addTrack(source ? source->name() : "Track", source, volume);
}

int AudioMixer::addTrack(std::shared_ptr<ISource> source, float volume) {
    return addTrack(source ? source->name() : "Track", source.get(), volume);
}

int AudioMixer::addTrack(const QString& name, std::shared_ptr<ISource> source, float volume) {
    return addTrack(name, source.get(), volume);
}

int AudioMixer::trackCount() const {
    QMutexLocker lock(&m_mutex);
    return m_tracks.size();
}

bool AudioMixer::removeTrack(int trackId) {
    QMutexLocker lock(&m_mutex);
    if (!m_tracks.contains(trackId)) return false;

    m_tracks.remove(trackId);
    qDebug() << "Audio track removed ID:" << trackId;

    lock.unlock();
    emit trackRemoved(trackId);
    return true;
}

void AudioMixer::clearTracks() {
    QMutexLocker lock(&m_mutex);
    QList<int> ids = m_tracks.keys();
    m_tracks.clear();
    lock.unlock();

    for (int id : ids) {
        emit trackRemoved(id);
    }
}

QList<AudioTrack> AudioMixer::tracks() const {
    QMutexLocker lock(&m_mutex);
    return m_tracks.values();
}

AudioTrack AudioMixer::track(int trackId) const {
    QMutexLocker lock(&m_mutex);
    return m_tracks.value(trackId);
}

void AudioMixer::setVolume(int trackId, float volume) {
    QMutexLocker lock(&m_mutex);
    if (!m_tracks.contains(trackId)) return;

    m_tracks[trackId].volume = std::clamp(volume, 0.0f, 2.0f);
    float vol = m_tracks[trackId].volume;

    lock.unlock();
    emit volumeChanged(trackId, vol);
    emit trackVolumeChanged(trackId, vol);
}

float AudioMixer::volume(int trackId) const {
    QMutexLocker lock(&m_mutex);
    return m_tracks.contains(trackId) ? m_tracks[trackId].volume : 0.0f;
}

void AudioMixer::setMuted(int trackId, bool muted) {
    QMutexLocker lock(&m_mutex);
    if (!m_tracks.contains(trackId)) return;

    m_tracks[trackId].muted = muted;

    lock.unlock();
    emit muteChanged(trackId, muted);
    emit trackMuteChanged(trackId, muted);
}

bool AudioMixer::isMuted(int trackId) const {
    QMutexLocker lock(&m_mutex);
    return m_tracks.contains(trackId) ? m_tracks[trackId].muted : false;
}

float AudioMixer::peakLevel(int trackId) const {
    QMutexLocker lock(&m_mutex);
    return m_tracks.contains(trackId) ? m_tracks[trackId].currentPeak : 0.0f;
}

AudioFrame AudioMixer::mixTracks(int sampleCount) {
    AudioFrame mixed;
    mixed.sampleRate = 48000;
    mixed.channels = 2;
    mixed.timestamp = QDateTime::currentMSecsSinceEpoch() * 1000;

    int totalFloats = sampleCount * 2;
    mixed.samples.resize(totalFloats, 0.0f);

    QMap<int, float> levels;

    QMutexLocker lock(&m_mutex);
    if (m_tracks.isEmpty()) {
        return mixed;
    }

    for (auto it = m_tracks.begin(); it != m_tracks.end(); ++it) {
        AudioTrack& track = it.value();
        float rawPeak = 0.0f;

        if (track.source) {
            AudioFrame srcFrame = track.source->captureAudioFrame();
            int frameFloats = static_cast<int>(srcFrame.samples.size());
            int count = std::min(totalFloats, frameFloats);

            for (int i = 0; i < count; ++i) {
                float sample = srcFrame.samples[i];
                float absSample = std::fabs(sample);
                if (absSample > rawPeak) {
                    rawPeak = absSample;
                }

                if (!track.muted) {
                    mixed.samples[i] += sample * track.volume;
                }
            }
        }

        // Apply decay to peak level for smooth VU meter response
        float targetPeak = track.muted ? 0.0f : std::min(rawPeak * track.volume, 1.0f);
        if (targetPeak >= track.currentPeak) {
            track.currentPeak = targetPeak;
        } else {
            track.currentPeak = track.currentPeak * 0.88f; // Exponential decay
        }
        levels.insert(it.key(), track.currentPeak);
    }

    // Soft limiter / clamp mixed audio to [-1.0f, 1.0f]
    for (int i = 0; i < totalFloats; ++i) {
        mixed.samples[i] = std::clamp(mixed.samples[i], -1.0f, 1.0f);
    }

    lock.unlock();
    emit levelsUpdated(levels);
    return mixed;
}

} // namespace WeaR
