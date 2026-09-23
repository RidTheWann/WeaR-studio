// ==============================================================================
// WeaR-studio AudioMixerDock Header
// Dock widget providing real-time audio volume sliders, mute, and VU meters
// ==============================================================================

#pragma once

#include <QDockWidget>
#include <QWidget>
#include <QSlider>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMap>
#include <QPainter>
#include <QTimer>

#include "AudioMixer.h"

namespace WeaR {

/**
 * @brief Custom VU level meter widget with green/yellow/red color gradients
 */
class AudioMeterWidget : public QWidget {
    Q_OBJECT

public:
    explicit AudioMeterWidget(QWidget* parent = nullptr);

    void setLevel(float level);
    [[nodiscard]] float level() const { return m_level; }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    float m_level = 0.0f;
};

/**
 * @brief Widget representing a single audio track in the mixer
 */
class AudioTrackWidget : public QWidget {
    Q_OBJECT

public:
    explicit AudioTrackWidget(const AudioTrack& track, QWidget* parent = nullptr);

    int trackId() const { return m_trackId; }
    void updateLevel(float level);
    void updateMuteState(bool muted);
    void updateVolume(float volume);

signals:
    void volumeChanged(int trackId, float volume);
    void muteToggled(int trackId, bool muted);

private slots:
    void onSliderValueChanged(int value);
    void onMuteClicked();

private:
    int m_trackId = -1;
    QLabel* m_nameLabel = nullptr;
    AudioMeterWidget* m_meterWidget = nullptr;
    QSlider* m_volumeSlider = nullptr;
    QLabel* m_volumeLabel = nullptr;
    QPushButton* m_muteButton = nullptr;
};

/**
 * @brief Audio Mixer Dock widget for WeaR Studio
 */
class AudioMixerDock : public QDockWidget {
    Q_OBJECT

public:
    explicit AudioMixerDock(QWidget* parent = nullptr);
    ~AudioMixerDock() override = default;

public slots:
    void refreshTracks();
    void onLevelsUpdated(const QMap<int, float>& levels);

private:
    void setupUi();

    QWidget* m_container = nullptr;
    QVBoxLayout* m_tracksLayout = nullptr;
    QMap<int, AudioTrackWidget*> m_trackWidgets;
};

} // namespace WeaR
