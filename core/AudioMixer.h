#pragma once
// ==============================================================================
// WeaR-studio AudioMixer
// Multi-track audio mixing engine with per-track volume, mute, and VU monitoring
// ==============================================================================

#include "ISource.h"

#include <QObject>
#include <QMutex>
#include <QMap>
#include <QList>
#include <QString>
#include <vector>

namespace WeaR {

struct AudioTrack {
    int id = 0;
    QString name;
    ISource* source = nullptr;
    float volume = 1.0f;       ///< 0.0f to 1.5f
    bool muted = false;
    float currentPeak = 0.0f;  ///< Peak magnitude [0.0, 1.0] for VU meter
};

/**
 * @brief Real-time multi-track audio mixer
 *
 * Thread-safe singleton providing audio mixing for N sources.
 * Synchronized with SceneManager render loop ticks.
 */
class AudioMixer : public QObject {
    Q_OBJECT

public:
    static AudioMixer& instance();

    AudioMixer(const AudioMixer&) = delete;
    AudioMixer& operator=(const AudioMixer&) = delete;

    ~AudioMixer() override = default;

    /**
     * @brief Add an audio track to the mixer
     * @param name Display name for the track
     * @param source Pointer to ISource delivering audio frames
     * @param volume Initial volume (default 1.0)
     * @return Assigned unique track ID
     */
    int addTrack(const QString& name, ISource* source, float volume = 1.0f);
    int addTrack(ISource* source, float volume = 1.0f);
    int addTrack(std::shared_ptr<ISource> source, float volume = 1.0f);
    int addTrack(const QString& name, std::shared_ptr<ISource> source, float volume = 1.0f);

    /**
     * @brief Remove an audio track by ID
     */
    bool removeTrack(int trackId);

    /**
     * @brief Clear all tracks
     */
    void clearTracks();

    /**
     * @brief Get number of tracks in mixer
     */
    [[nodiscard]] int trackCount() const;

    /**
     * @brief Get list of all registered tracks
     */
    [[nodiscard]] QList<AudioTrack> tracks() const;

    /**
     * @brief Get track details
     */
    [[nodiscard]] AudioTrack track(int trackId) const;

    /**
     * @brief Set track volume (0.0 to 1.5)
     */
    void setVolume(int trackId, float volume);
    void setTrackVolume(int trackId, float volume) { setVolume(trackId, volume); }

    /**
     * @brief Get track volume
     */
    [[nodiscard]] float volume(int trackId) const;

    /**
     * @brief Set track mute status
     */
    void setMuted(int trackId, bool muted);
    void setTrackMuted(int trackId, bool muted) { setMuted(trackId, muted); }

    /**
     * @brief Check if track is muted
     */
    [[nodiscard]] bool isMuted(int trackId) const;

    /**
     * @brief Get latest peak level for VU meter [0.0, 1.0]
     */
    [[nodiscard]] float peakLevel(int trackId) const;

    /**
     * @brief Mix all active tracks into a single stereo AudioFrame
     * @param sampleCount Number of samples per channel to produce (default 800 for 60fps@48kHz)
     * @return Mixed AudioFrame
     */
    AudioFrame mixTracks(int sampleCount = 800);

signals:
    void trackAdded(int trackId, const QString& name);
    void trackRemoved(int trackId);
    void volumeChanged(int trackId, float volume);
    void muteChanged(int trackId, bool muted);
    void trackVolumeChanged(int trackId, float volume);
    void trackMuteChanged(int trackId, bool muted);
    void levelsUpdated(const QMap<int, float>& levels);

private:
    explicit AudioMixer(QObject* parent = nullptr);

    mutable QMutex m_mutex;
    QMap<int, AudioTrack> m_tracks;
    int m_nextTrackId = 1;
};

} // namespace WeaR
