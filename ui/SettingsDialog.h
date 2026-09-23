#pragma once

#include "GlobalHotkeyManager.h"

#include <QDialog>
#include <QMap>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QSpinBox;
class QDoubleSpinBox;
class QTabWidget;
class QDialogButtonBox;

namespace WeaR {

class HotkeyEdit;

class SettingsDialog final : public QDialog {
    Q_OBJECT

public:
    explicit SettingsDialog(int micTrackId, QWidget* parent = nullptr);

private:
    void buildOutputTab();
    void buildVideoTab();
    void buildAudioTab();
    void buildHotkeysTab();
    void buildAdvancedTab();

    void loadFromManagers();
    bool applyToManagers();

    void accept() override;

    QTabWidget* m_tabs = nullptr;
    QDialogButtonBox* m_buttons = nullptr;

    // Output
    QLineEdit* m_streamUrl = nullptr;
    QLineEdit* m_streamKey = nullptr;
    QComboBox* m_streamService = nullptr;
    QSpinBox* m_outputWidth = nullptr;
    QSpinBox* m_outputHeight = nullptr;
    QDoubleSpinBox* m_outputFps = nullptr;

    // Video
    QComboBox* m_encoderType = nullptr;
    QComboBox* m_encoderPreset = nullptr;
    QComboBox* m_rateControl = nullptr;
    QSpinBox* m_videoBitrate = nullptr;
    QSpinBox* m_maxVideoBitrate = nullptr;
    QSpinBox* m_videoBuffer = nullptr;
    QSpinBox* m_crf = nullptr;
    QSpinBox* m_qp = nullptr;
    QSpinBox* m_keyframeInterval = nullptr;
    QSpinBox* m_bFrames = nullptr;
    QLineEdit* m_profile = nullptr;
    QLineEdit* m_level = nullptr;
    QSpinBox* m_encoderThreads = nullptr;

    // Audio
    QCheckBox* m_audioEnabled = nullptr;
    QSpinBox* m_audioSampleRate = nullptr;
    QSpinBox* m_audioChannels = nullptr;
    QSpinBox* m_audioBitrate = nullptr;
    QCheckBox* m_micMuted = nullptr;
    int m_micTrackId = -1;

    // Hotkeys
    QMap<GlobalHotkeyAction, HotkeyEdit*> m_hotkeyEdits;

    // Advanced
    QSpinBox* m_connectTimeout = nullptr;
    QSpinBox* m_reconnectDelay = nullptr;
    QSpinBox* m_maxReconnectAttempts = nullptr;
    QSpinBox* m_sendBufferSize = nullptr;
    QLineEdit* m_recordPath = nullptr;
    QComboBox* m_recordFormat = nullptr;
    QSpinBox* m_recordWidth = nullptr;
    QSpinBox* m_recordHeight = nullptr;
    QSpinBox* m_recordFps = nullptr;
    QSpinBox* m_recordBitrate = nullptr;
    QSpinBox* m_recordMaxBitrate = nullptr;
    QSpinBox* m_recordBuffer = nullptr;
    QSpinBox* m_recordCrf = nullptr;
    QSpinBox* m_recordQp = nullptr;
    QComboBox* m_recordEncoder = nullptr;
    QComboBox* m_recordPreset = nullptr;
    QComboBox* m_recordRateControl = nullptr;
    QSpinBox* m_recordKeyframe = nullptr;
    QSpinBox* m_recordBFrames = nullptr;
    QSpinBox* m_recordThreads = nullptr;
};

} // namespace WeaR
