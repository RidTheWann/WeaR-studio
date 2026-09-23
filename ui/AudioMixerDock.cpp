// ==============================================================================
// WeaR-studio AudioMixerDock Implementation
// Real-time audio volume control, mute, and VU meter display
// ==============================================================================

#include "AudioMixerDock.h"

#include <QLinearGradient>
#include <QStyle>
#include <algorithm>
#include <cmath>

namespace WeaR {

// ==============================================================================
// AudioMeterWidget
// ==============================================================================
AudioMeterWidget::AudioMeterWidget(QWidget* parent)
    : QWidget(parent)
{
    setFixedHeight(12);
    setMinimumWidth(100);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void AudioMeterWidget::setLevel(float level) {
    m_level = std::clamp(level, 0.0f, 1.0f);
    update();
}

void AudioMeterWidget::paintEvent(QPaintEvent* /*event*/) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, false);

    // Background
    QRect bgRect = rect();
    painter.fillRect(bgRect, QColor(30, 30, 30));

    if (m_level <= 0.001f) {
        return;
    }

    int fillWidth = static_cast<int>(bgRect.width() * m_level);
    if (fillWidth < 1) fillWidth = 1;

    QRect fillRect(bgRect.x(), bgRect.y(), fillWidth, bgRect.height());

    // Three-segment or gradient level: Green -> Yellow -> Red
    QLinearGradient gradient(0, 0, bgRect.width(), 0);
    gradient.setColorAt(0.0, QColor(46, 204, 113));    // Green
    gradient.setColorAt(0.7, QColor(46, 204, 113));    // Green up to 70%
    gradient.setColorAt(0.75, QColor(241, 196, 15));   // Yellow
    gradient.setColorAt(0.85, QColor(243, 156, 18));   // Orange-Yellow
    gradient.setColorAt(0.9, QColor(231, 76, 60));     // Red
    gradient.setColorAt(1.0, QColor(192, 57, 43));     // Dark Red

    painter.fillRect(fillRect, gradient);

    // Subtle border
    painter.setPen(QColor(60, 60, 60));
    painter.drawRect(bgRect.adjusted(0, 0, -1, -1));
}

// ==============================================================================
// AudioTrackWidget
// ==============================================================================
AudioTrackWidget::AudioTrackWidget(const AudioTrack& track, QWidget* parent)
    : QWidget(parent)
    , m_trackId(track.id)
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(4, 4, 4, 4);
    mainLayout->setSpacing(4);

    // Header row: Track name + Volume % + Mute button
    auto* headerLayout = new QHBoxLayout();
    headerLayout->setContentsMargins(0, 0, 0, 0);

    m_nameLabel = new QLabel(track.name, this);
    m_nameLabel->setStyleSheet("font-weight: bold; color: #ffffff;");
    headerLayout->addWidget(m_nameLabel);
    headerLayout->addStretch();

    int initialVolPct = static_cast<int>(std::round(track.volume * 100.0f));
    m_volumeLabel = new QLabel(QString("%1%").arg(initialVolPct), this);
    m_volumeLabel->setFixedWidth(40);
    m_volumeLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_volumeLabel->setStyleSheet("color: #aaaaaa;");
    headerLayout->addWidget(m_volumeLabel);

    m_muteButton = new QPushButton(track.muted ? "Muted" : "Mute", this);
    m_muteButton->setCheckable(true);
    m_muteButton->setChecked(track.muted);
    m_muteButton->setFixedWidth(54);
    m_muteButton->setFixedHeight(22);
    if (track.muted) {
        m_muteButton->setStyleSheet("background-color: #c0392b; color: white;");
    }
    connect(m_muteButton, &QPushButton::clicked, this, &AudioTrackWidget::onMuteClicked);
    headerLayout->addWidget(m_muteButton);

    mainLayout->addLayout(headerLayout);

    // VU Meter
    m_meterWidget = new AudioMeterWidget(this);
    mainLayout->addWidget(m_meterWidget);

    // Volume Slider (0 - 100%)
    m_volumeSlider = new QSlider(Qt::Horizontal, this);
    m_volumeSlider->setRange(0, 100);
    m_volumeSlider->setValue(initialVolPct);
    m_volumeSlider->setFixedHeight(18);
    connect(m_volumeSlider, &QSlider::valueChanged, this, &AudioTrackWidget::onSliderValueChanged);
    mainLayout->addWidget(m_volumeSlider);

    // Thin separator line at the bottom
    auto* separator = new QFrame(this);
    separator->setFrameShape(QFrame::HLine);
    separator->setFrameShadow(QFrame::Sunken);
    separator->setStyleSheet("color: #444444;");
    mainLayout->addWidget(separator);
}

void AudioTrackWidget::updateLevel(float level) {
    if (m_meterWidget) {
        m_meterWidget->setLevel(level);
    }
}

void AudioTrackWidget::updateMuteState(bool muted) {
    if (m_muteButton) {
        m_muteButton->blockSignals(true);
        m_muteButton->setChecked(muted);
        m_muteButton->setText(muted ? "Muted" : "Mute");
        if (muted) {
            m_muteButton->setStyleSheet("background-color: #c0392b; color: white;");
        } else {
            m_muteButton->setStyleSheet("");
        }
        m_muteButton->blockSignals(false);
    }
}

void AudioTrackWidget::updateVolume(float volume) {
    int pct = static_cast<int>(std::round(volume * 100.0f));
    if (m_volumeSlider && m_volumeSlider->value() != pct) {
        m_volumeSlider->blockSignals(true);
        m_volumeSlider->setValue(pct);
        m_volumeSlider->blockSignals(false);
    }
    if (m_volumeLabel) {
        m_volumeLabel->setText(QString("%1%").arg(pct));
    }
}

void AudioTrackWidget::onSliderValueChanged(int value) {
    float vol = value / 100.0f;
    if (m_volumeLabel) {
        m_volumeLabel->setText(QString("%1%").arg(value));
    }
    emit volumeChanged(m_trackId, vol);
}

void AudioTrackWidget::onMuteClicked() {
    bool muted = m_muteButton->isChecked();
    m_muteButton->setText(muted ? "Muted" : "Mute");
    if (muted) {
        m_muteButton->setStyleSheet("background-color: #c0392b; color: white;");
    } else {
        m_muteButton->setStyleSheet("");
    }
    emit muteToggled(m_trackId, muted);
}

// ==============================================================================
// AudioMixerDock
// ==============================================================================
AudioMixerDock::AudioMixerDock(QWidget* parent)
    : QDockWidget("Audio Mixer", parent)
{
    setObjectName("audioMixerDock");
    setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);

    setupUi();

    // Connect to AudioMixer signals
    auto& mixer = AudioMixer::instance();
    connect(&mixer, &AudioMixer::levelsUpdated,
            this, &AudioMixerDock::onLevelsUpdated, Qt::QueuedConnection);
    connect(&mixer, &AudioMixer::trackAdded,
            this, &AudioMixerDock::refreshTracks, Qt::QueuedConnection);
    connect(&mixer, &AudioMixer::trackRemoved,
            this, &AudioMixerDock::refreshTracks, Qt::QueuedConnection);
    connect(&mixer, &AudioMixer::trackMuteChanged, this, [this](int trackId, bool muted) {
        if (m_trackWidgets.contains(trackId)) {
            m_trackWidgets[trackId]->updateMuteState(muted);
        }
    }, Qt::QueuedConnection);
    connect(&mixer, &AudioMixer::trackVolumeChanged, this, [this](int trackId, float volume) {
        if (m_trackWidgets.contains(trackId)) {
            m_trackWidgets[trackId]->updateVolume(volume);
        }
    }, Qt::QueuedConnection);

    refreshTracks();
}

void AudioMixerDock::setupUi() {
    m_container = new QWidget(this);
    m_tracksLayout = new QVBoxLayout(m_container);
    m_tracksLayout->setContentsMargins(8, 8, 8, 8);
    m_tracksLayout->setSpacing(6);
    m_tracksLayout->addStretch();

    setWidget(m_container);
}

void AudioMixerDock::refreshTracks() {
    // Clear existing widgets
    for (auto* widget : m_trackWidgets) {
        m_tracksLayout->removeWidget(widget);
        delete widget;
    }
    m_trackWidgets.clear();

    const auto tracks = AudioMixer::instance().tracks();
    for (const auto& track : tracks) {
        auto* trackWidget = new AudioTrackWidget(track, m_container);

        connect(trackWidget, &AudioTrackWidget::volumeChanged, this, [](int trackId, float volume) {
            AudioMixer::instance().setTrackVolume(trackId, volume);
        });
        connect(trackWidget, &AudioTrackWidget::muteToggled, this, [](int trackId, bool muted) {
            AudioMixer::instance().setTrackMuted(trackId, muted);
        });

        // Insert before stretch item
        int insertPos = std::max(0, m_tracksLayout->count() - 1);
        m_tracksLayout->insertWidget(insertPos, trackWidget);
        m_trackWidgets.insert(track.id, trackWidget);
    }
}

void AudioMixerDock::onLevelsUpdated(const QMap<int, float>& levels) {
    for (auto it = levels.begin(); it != levels.end(); ++it) {
        int trackId = it.key();
        float level = it.value();

        if (m_trackWidgets.contains(trackId)) {
            m_trackWidgets[trackId]->updateLevel(level);
        }
    }
}

} // namespace WeaR
