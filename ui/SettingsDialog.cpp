#include "SettingsDialog.h"
#include "HotkeyEdit.h"

#include "AudioMixer.h"
#include "EncoderManager.h"
#include "RecordingManager.h"
#include "SceneManager.h"
#include "StreamManager.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QFileDialog>
#include <QHBoxLayout>
#include <algorithm>

namespace WeaR {

namespace {

template <typename T>
void addEnumItems(QComboBox* combo, const QList<QString>& names) {
    combo->clear();
    for (int i = 0; i < names.size(); ++i) {
        combo->addItem(names.at(i), i);
    }
}

} // namespace

SettingsDialog::SettingsDialog(int micTrackId, QWidget* parent)
    : QDialog(parent),
      m_micTrackId(micTrackId) {
    setWindowTitle(QStringLiteral("WeaR Studio Settings"));
    setMinimumSize(760, 620);
    resize(900, 720);

    auto* root = new QVBoxLayout(this);
    m_tabs = new QTabWidget(this);

    buildOutputTab();
    buildVideoTab();
    buildAudioTab();
    buildHotkeysTab();
    buildAdvancedTab();

    root->addWidget(m_tabs, 1);

    m_buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
        Qt::Horizontal,
        this);
    connect(m_buttons, &QDialogButtonBox::accepted, this, &SettingsDialog::accept);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &SettingsDialog::reject);
    root->addWidget(m_buttons);

    loadFromManagers();
}

void SettingsDialog::buildOutputTab() {
    auto* tab = new QWidget(this);
    auto* root = new QVBoxLayout(tab);

    auto* streamGroup = new QGroupBox(QStringLiteral("Streaming output"), tab);
    auto* streamForm = new QFormLayout(streamGroup);

    m_streamService = new QComboBox(streamGroup);
    addEnumItems<void>(
        m_streamService,
        { "Custom", "Twitch", "YouTube", "Facebook", "Kick", "TikTok" });

    m_streamUrl = new QLineEdit(streamGroup);
    m_streamKey = new QLineEdit(streamGroup);
    m_streamKey->setEchoMode(QLineEdit::Password);
    streamForm->addRow(QStringLiteral("Service:"), m_streamService);
    streamForm->addRow(QStringLiteral("RTMP URL:"), m_streamUrl);
    streamForm->addRow(QStringLiteral("Stream key:"), m_streamKey);

    root->addWidget(streamGroup);

    auto* outputGroup = new QGroupBox(QStringLiteral("Compositing output"), tab);
    auto* outputForm = new QFormLayout(outputGroup);

    m_outputWidth = new QSpinBox(outputGroup);
    m_outputWidth->setRange(160, 7680);
    m_outputHeight = new QSpinBox(outputGroup);
    m_outputHeight->setRange(120, 4320);
    m_outputFps = new QDoubleSpinBox(outputGroup);
    m_outputFps->setRange(1.0, 240.0);
    m_outputFps->setDecimals(2);

    outputForm->addRow(QStringLiteral("Width:"), m_outputWidth);
    outputForm->addRow(QStringLiteral("Height:"), m_outputHeight);
    outputForm->addRow(QStringLiteral("FPS:"), m_outputFps);

    root->addWidget(outputGroup);
    root->addStretch();
    m_tabs->addTab(tab, QStringLiteral("Output"));
}

void SettingsDialog::buildVideoTab() {
    auto* tab = new QWidget(this);
    auto* form = new QFormLayout(tab);

    m_encoderType = new QComboBox(tab);
    addEnumItems<void>(
        m_encoderType,
        { "NVENC H.264", "NVENC HEVC", "AMF H.264", "AMF HEVC",
          "QSV H.264", "QSV HEVC", "x264", "x265", "Auto" });

    m_encoderPreset = new QComboBox(tab);
    addEnumItems<void>(
        m_encoderPreset,
        { "UltraFast", "SuperFast", "VeryFast", "Faster", "Fast",
          "Medium", "Slow", "Slower", "VerySlow", "Placebo" });

    m_rateControl = new QComboBox(tab);
    addEnumItems<void>(m_rateControl, { "CBR", "VBR", "CRF", "CQP" });

    auto spin = [tab](int min, int max, int value) {
        auto* box = new QSpinBox(tab);
        box->setRange(min, max);
        box->setValue(value);
        return box;
    };

    m_videoBitrate = spin(100, 200000, 6000);
    m_maxVideoBitrate = spin(100, 200000, 8000);
    m_videoBuffer = spin(100, 400000, 12000);
    m_crf = spin(0, 51, 23);
    m_qp = spin(0, 63, 20);
    m_keyframeInterval = spin(0, 60, 2);
    m_bFrames = spin(0, 16, 0);
    m_encoderThreads = spin(0, 64, 0);

    m_profile = new QLineEdit(tab);
    m_level = new QLineEdit(tab);

    form->addRow(QStringLiteral("Encoder:"), m_encoderType);
    form->addRow(QStringLiteral("Preset:"), m_encoderPreset);
    form->addRow(QStringLiteral("Rate control:"), m_rateControl);
    form->addRow(QStringLiteral("Bitrate (kbps):"), m_videoBitrate);
    form->addRow(QStringLiteral("Max bitrate (kbps):"), m_maxVideoBitrate);
    form->addRow(QStringLiteral("Buffer (kbps):"), m_videoBuffer);
    form->addRow(QStringLiteral("CRF:"), m_crf);
    form->addRow(QStringLiteral("QP:"), m_qp);
    form->addRow(QStringLiteral("Keyframe interval (s):"), m_keyframeInterval);
    form->addRow(QStringLiteral("B-frames:"), m_bFrames);
    form->addRow(QStringLiteral("H.264 profile:"), m_profile);
    form->addRow(QStringLiteral("Level:"), m_level);
    form->addRow(QStringLiteral("Software threads (0=auto):"), m_encoderThreads);

    m_tabs->addTab(tab, QStringLiteral("Video"));
}

void SettingsDialog::buildAudioTab() {
    auto* tab = new QWidget(this);
    auto* form = new QFormLayout(tab);

    m_audioEnabled = new QCheckBox(QStringLiteral("Enable audio"), tab);
    m_audioSampleRate = new QSpinBox(tab);
    m_audioSampleRate->setRange(8000, 192000);
    m_audioChannels = new QSpinBox(tab);
    m_audioChannels->setRange(1, 8);
    m_audioBitrate = new QSpinBox(tab);
    m_audioBitrate->setRange(32, 512);

    m_micMuted = new QCheckBox(QStringLiteral("Mute microphone"), tab);

    form->addRow(m_audioEnabled);
    form->addRow(QStringLiteral("Sample rate:"), m_audioSampleRate);
    form->addRow(QStringLiteral("Channels:"), m_audioChannels);
    form->addRow(QStringLiteral("Bitrate (kbps):"), m_audioBitrate);
    form->addRow(m_micMuted);

    if (m_micTrackId < 0) {
        m_micMuted->setEnabled(false);
        m_micMuted->setToolTip(
            QStringLiteral("Microphone track is not available."));
    }

    m_tabs->addTab(tab, QStringLiteral("Audio"));
}

void SettingsDialog::buildHotkeysTab() {
    auto* tab = new QWidget(this);
    auto* form = new QFormLayout(tab);

    const auto add = [this, tab, form](GlobalHotkeyAction action) {
        auto* editor = new HotkeyEdit(tab);
        editor->setToolTip(QStringLiteral(
            "Press a single key combination. Backspace/Delete clears it."));
        m_hotkeyEdits.insert(action, editor);
        form->addRow(GlobalHotkeyManager::actionName(action) + QStringLiteral(":"), editor);
    };

    add(GlobalHotkeyAction::StartStream);
    add(GlobalHotkeyAction::StopStream);
    add(GlobalHotkeyAction::StartRecord);
    add(GlobalHotkeyAction::StopRecord);
    add(GlobalHotkeyAction::NextScene);
    add(GlobalHotkeyAction::ToggleMicMute);

    auto* note = new QLabel(
        QStringLiteral("Windows: these are true global hotkeys and work while "
                       "WeaR Studio is not focused. Use Backspace/Delete to disable one."),
        tab);
    note->setWordWrap(true);

    auto* layout = new QVBoxLayout(tab);
    layout->addLayout(form);
    layout->addWidget(note);
    layout->addStretch();

    m_tabs->addTab(tab, QStringLiteral("Hotkeys"));
}

void SettingsDialog::buildAdvancedTab() {
    auto* tab = new QWidget(this);
    auto* root = new QVBoxLayout(tab);

    auto* stream = new QGroupBox(QStringLiteral("Connection"), tab);
    auto* streamForm = new QFormLayout(stream);

    m_connectTimeout = new QSpinBox(stream);
    m_connectTimeout->setRange(1, 120);
    m_reconnectDelay = new QSpinBox(stream);
    m_reconnectDelay->setRange(0, 120);
    m_maxReconnectAttempts = new QSpinBox(stream);
    m_maxReconnectAttempts->setRange(0, 100);
    m_sendBufferSize = new QSpinBox(stream);
    m_sendBufferSize->setRange(4096, 16 * 1024 * 1024);
    m_sendBufferSize->setSingleStep(64 * 1024);

    streamForm->addRow(QStringLiteral("Connect timeout (s):"), m_connectTimeout);
    streamForm->addRow(QStringLiteral("Reconnect delay (s):"), m_reconnectDelay);
    streamForm->addRow(QStringLiteral("Max reconnect attempts:"), m_maxReconnectAttempts);
    streamForm->addRow(QStringLiteral("Send buffer (bytes):"), m_sendBufferSize);
    root->addWidget(stream);

    auto* recording = new QGroupBox(QStringLiteral("Local recording"), tab);
    auto* recordForm = new QFormLayout(recording);

    m_recordPath = new QLineEdit(recording);
    auto* browse = new QPushButton(QStringLiteral("Browse..."), recording);
    auto* pathRow = new QHBoxLayout();
    pathRow->addWidget(m_recordPath, 1);
    pathRow->addWidget(browse);
    recordForm->addRow(QStringLiteral("Path:"), pathRow);

    m_recordFormat = new QComboBox(recording);
    addEnumItems<void>(m_recordFormat, { "MKV", "MP4", "FLV" });

    m_recordWidth = new QSpinBox(recording);
    m_recordWidth->setRange(160, 7680);
    m_recordHeight = new QSpinBox(recording);
    m_recordHeight->setRange(120, 4320);
    m_recordFps = new QSpinBox(recording);
    m_recordFps->setRange(1, 240);
    m_recordBitrate = new QSpinBox(recording);
    m_recordBitrate->setRange(100, 200000);
    m_recordMaxBitrate = new QSpinBox(recording);
    m_recordMaxBitrate->setRange(100, 300000);
    m_recordBuffer = new QSpinBox(recording);
    m_recordBuffer->setRange(100, 500000);
    m_recordCrf = new QSpinBox(recording);
    m_recordCrf->setRange(0, 51);
    m_recordQp = new QSpinBox(recording);
    m_recordQp->setRange(0, 63);
    m_recordEncoder = new QComboBox(recording);
    addEnumItems<void>(
        m_recordEncoder,
        { "NVENC H.264", "NVENC HEVC", "AMF H.264", "AMF HEVC",
          "QSV H.264", "QSV HEVC", "x264", "x265", "Auto" });
    m_recordPreset = new QComboBox(recording);
    addEnumItems<void>(
        m_recordPreset,
        { "UltraFast", "SuperFast", "VeryFast", "Faster", "Fast",
          "Medium", "Slow", "Slower", "VerySlow", "Placebo" });
    m_recordRateControl = new QComboBox(recording);
    addEnumItems<void>(m_recordRateControl, { "CBR", "VBR", "CRF", "CQP" });
    m_recordKeyframe = new QSpinBox(recording);
    m_recordKeyframe->setRange(0, 60);
    m_recordBFrames = new QSpinBox(recording);
    m_recordBFrames->setRange(0, 16);
    m_recordThreads = new QSpinBox(recording);
    m_recordThreads->setRange(0, 64);

    recordForm->addRow(QStringLiteral("Format:"), m_recordFormat);
    recordForm->addRow(QStringLiteral("Width:"), m_recordWidth);
    recordForm->addRow(QStringLiteral("Height:"), m_recordHeight);
    recordForm->addRow(QStringLiteral("FPS:"), m_recordFps);
    recordForm->addRow(QStringLiteral("Video bitrate:"), m_recordBitrate);
    recordForm->addRow(QStringLiteral("Max bitrate:"), m_recordMaxBitrate);
    recordForm->addRow(QStringLiteral("Buffer:"), m_recordBuffer);
    recordForm->addRow(QStringLiteral("CRF:"), m_recordCrf);
    recordForm->addRow(QStringLiteral("QP:"), m_recordQp);
    recordForm->addRow(QStringLiteral("Encoder:"), m_recordEncoder);
    recordForm->addRow(QStringLiteral("Preset:"), m_recordPreset);
    recordForm->addRow(QStringLiteral("Rate control:"), m_recordRateControl);
    recordForm->addRow(QStringLiteral("Keyframe interval:"), m_recordKeyframe);
    recordForm->addRow(QStringLiteral("B-frames:"), m_recordBFrames);
    recordForm->addRow(QStringLiteral("Threads:"), m_recordThreads);

    connect(browse, &QPushButton::clicked, this, [this]() {
        const QString selected = QFileDialog::getSaveFileName(
            this,
            QStringLiteral("Recording output"),
            m_recordPath->text(),
            QStringLiteral(
                "MKV Video (*.mkv);;MP4 Video (*.mp4);;FLV Video (*.flv);;All Files (*)"));
        if (!selected.isEmpty()) {
            m_recordPath->setText(selected);
        }
    });

    root->addWidget(recording);
    root->addStretch();
    m_tabs->addTab(tab, QStringLiteral("Advanced"));
}

void SettingsDialog::loadFromManagers() {
    const auto stream = StreamManager::instance().settings();
    const auto encoder = EncoderManager::instance().settings();
    const auto recording = RecordingManager::instance().settings();
    const auto& sceneManager = SceneManager::instance();
    const auto bindings = GlobalHotkeyManager::instance().bindings();

    m_streamUrl->setText(stream.url);
    m_streamKey->setText(stream.streamKey);
    m_streamService->setCurrentIndex(static_cast<int>(stream.service));

    m_outputWidth->setValue(sceneManager.outputResolution().width());
    m_outputHeight->setValue(sceneManager.outputResolution().height());
    m_outputFps->setValue(sceneManager.targetFps());

    m_encoderType->setCurrentIndex(static_cast<int>(encoder.encoderType));
    m_encoderPreset->setCurrentIndex(static_cast<int>(encoder.preset));
    m_rateControl->setCurrentIndex(static_cast<int>(encoder.rateControl));
    m_videoBitrate->setValue(encoder.bitrate);
    m_maxVideoBitrate->setValue(encoder.maxBitrate);
    m_videoBuffer->setValue(encoder.bufferSize);
    m_crf->setValue(encoder.crf);
    m_qp->setValue(encoder.qp);
    m_keyframeInterval->setValue(encoder.keyframeInterval);
    m_bFrames->setValue(encoder.bFrames);
    m_profile->setText(encoder.profile);
    m_level->setText(encoder.level);
    m_encoderThreads->setValue(encoder.threads);

    m_audioEnabled->setChecked(encoder.audioEnabled);
    m_audioSampleRate->setValue(encoder.audioSampleRate);
    m_audioChannels->setValue(encoder.audioChannels);
    m_audioBitrate->setValue(encoder.audioBitrate);
    m_micMuted->setChecked(
        m_micTrackId >= 0 &&
        AudioMixer::instance().isMuted(m_micTrackId));

    for (const auto& binding : bindings) {
        if (m_hotkeyEdits.contains(binding.action)) {
            m_hotkeyEdits.value(binding.action)->setSequence(binding.sequence);
        }
    }

    m_connectTimeout->setValue(stream.connectTimeout);
    m_reconnectDelay->setValue(stream.reconnectDelay);
    m_maxReconnectAttempts->setValue(stream.maxReconnectAttempts);
    m_sendBufferSize->setValue(stream.sendBufferSize);

    m_recordPath->setText(recording.outputPath);
    m_recordFormat->setCurrentIndex(static_cast<int>(recording.format));
    m_recordWidth->setValue(recording.width);
    m_recordHeight->setValue(recording.height);
    m_recordFps->setValue(recording.fpsNum / std::max(1, recording.fpsDen));
    m_recordBitrate->setValue(recording.videoBitrate);
    m_recordMaxBitrate->setValue(recording.maxVideoBitrate);
    m_recordBuffer->setValue(recording.bufferSize);
    m_recordCrf->setValue(recording.crf);
    m_recordQp->setValue(recording.qp);
    m_recordEncoder->setCurrentIndex(static_cast<int>(recording.encoderType));
    m_recordPreset->setCurrentIndex(static_cast<int>(recording.preset));
    m_recordRateControl->setCurrentIndex(static_cast<int>(recording.rateControl));
    m_recordKeyframe->setValue(recording.keyframeInterval);
    m_recordBFrames->setValue(recording.bFrames);
    m_recordThreads->setValue(recording.threads);
}

bool SettingsDialog::applyToManagers() {
    if (StreamManager::instance().isConnected() ||
        RecordingManager::instance().isRecording() ||
        RecordingManager::instance().isPaused() ||
        EncoderManager::instance().isRunning()) {
        QMessageBox::warning(
            this,
            QStringLiteral("Settings"),
            QStringLiteral(
                "Stop streaming and recording before applying output settings."));
        return false;
    }

    StreamSettings stream = StreamManager::instance().settings();
    stream.url = m_streamUrl->text().trimmed();
    stream.streamKey = m_streamKey->text();
    stream.service = static_cast<StreamService>(m_streamService->currentData().toInt());
    stream.connectTimeout = m_connectTimeout->value();
    stream.reconnectDelay = m_reconnectDelay->value();
    stream.maxReconnectAttempts = m_maxReconnectAttempts->value();
    stream.sendBufferSize = m_sendBufferSize->value();
    stream.videoWidth = m_outputWidth->value();
    stream.videoHeight = m_outputHeight->value();
    stream.videoFpsNum = qRound64(m_outputFps->value() * 1000.0);
    stream.videoFpsDen = 1000;
    stream.videoBitrate = m_videoBitrate->value();
    stream.audioEnabled = m_audioEnabled->isChecked();
    stream.audioSampleRate = m_audioSampleRate->value();
    stream.audioChannels = m_audioChannels->value();
    stream.audioBitrate = m_audioBitrate->value();

    EncoderSettings encoder = EncoderManager::instance().settings();
    encoder.width = m_outputWidth->value();
    encoder.height = m_outputHeight->value();
    encoder.fpsNum = stream.videoFpsNum;
    encoder.fpsDen = stream.videoFpsDen;
    encoder.bitrate = m_videoBitrate->value();
    encoder.maxBitrate = m_maxVideoBitrate->value();
    encoder.bufferSize = m_videoBuffer->value();
    encoder.crf = m_crf->value();
    encoder.qp = m_qp->value();
    encoder.encoderType =
        static_cast<EncoderType>(m_encoderType->currentData().toInt());
    encoder.preset =
        static_cast<EncoderPreset>(m_encoderPreset->currentData().toInt());
    encoder.rateControl =
        static_cast<RateControlMode>(m_rateControl->currentData().toInt());
    encoder.keyframeInterval = m_keyframeInterval->value();
    encoder.bFrames = m_bFrames->value();
    encoder.profile = m_profile->text().trimmed();
    encoder.level = m_level->text().trimmed();
    encoder.threads = m_encoderThreads->value();
    encoder.audioEnabled = m_audioEnabled->isChecked();
    encoder.audioSampleRate = m_audioSampleRate->value();
    encoder.audioChannels = m_audioChannels->value();
    encoder.audioBitrate = m_audioBitrate->value();

    RecordingSettings recording = RecordingManager::instance().settings();
    recording.outputPath = m_recordPath->text().trimmed();
    recording.format = static_cast<RecordingFormat>(m_recordFormat->currentData().toInt());
    recording.width = m_recordWidth->value();
    recording.height = m_recordHeight->value();
    recording.fpsNum = m_recordFps->value();
    recording.fpsDen = 1;
    recording.videoBitrate = m_recordBitrate->value();
    recording.maxVideoBitrate = m_recordMaxBitrate->value();
    recording.bufferSize = m_recordBuffer->value();
    recording.crf = m_recordCrf->value();
    recording.qp = m_recordQp->value();
    recording.encoderType = static_cast<EncoderType>(
        m_recordEncoder->currentData().toInt());
    recording.preset = static_cast<EncoderPreset>(
        m_recordPreset->currentData().toInt());
    recording.rateControl = static_cast<RateControlMode>(
        m_recordRateControl->currentData().toInt());
    recording.keyframeInterval = m_recordKeyframe->value();
    recording.bFrames = m_recordBFrames->value();
    recording.threads = m_recordThreads->value();
    recording.audioEnabled = m_audioEnabled->isChecked();
    recording.audioSampleRate = m_audioSampleRate->value();
    recording.audioChannels = m_audioChannels->value();
    recording.audioBitrate = m_audioBitrate->value();

    QList<GlobalHotkeyBinding> hotkeys;
    for (auto it = m_hotkeyEdits.cbegin();
         it != m_hotkeyEdits.cend();
         ++it) {
        hotkeys.append({
            it.key(),
            it.value()->sequence(),
            !it.value()->sequence().isEmpty()
        });
    }

    if (!GlobalHotkeyManager::instance().setBindings(hotkeys)) {
        QMessageBox::warning(
            this,
            QStringLiteral("Hotkeys"),
            GlobalHotkeyManager::instance().lastError());
        return false;
    }

    if (!StreamManager::instance().configure(stream)) {
        QMessageBox::warning(
            this,
            QStringLiteral("Settings"),
            QStringLiteral("Stream settings could not be applied."));
        return false;
    }

    if (!EncoderManager::instance().configure(encoder)) {
        QMessageBox::warning(
            this,
            QStringLiteral("Settings"),
            QStringLiteral("Video/audio settings could not be applied."));
        return false;
    }

    if (!RecordingManager::instance().configure(recording)) {
        QMessageBox::warning(
            this,
            QStringLiteral("Settings"),
            QStringLiteral("Recording settings could not be applied."));
        return false;
    }

    SceneManager::instance().setOutputResolution(
        QSize(m_outputWidth->value(), m_outputHeight->value()));
    SceneManager::instance().setTargetFps(m_outputFps->value());

    if (m_micTrackId >= 0) {
        AudioMixer::instance().setMuted(
            m_micTrackId,
            m_micMuted->isChecked());
    }

    return true;
}

void SettingsDialog::accept() {
    if (applyToManagers()) {
        QDialog::accept();
    }
}

} // namespace WeaR
