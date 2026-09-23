#pragma once
// ==============================================================================
// WeaR-studio Main Window
// Professional OBS-like streaming interface
// ==============================================================================

#include <QMainWindow>
#include <QTimer>
#include <memory>

#include <SceneTransition.h>
#include <StreamManager.h>
#include <EncoderManager.h>
#include <RecordingManager.h>

class QListWidget;
class QListWidgetItem;
class QLabel;
class QLineEdit;
class QPushButton;
class QDockWidget;
class QComboBox;
class QSpinBox;

namespace WeaR {

class PreviewWidget;
class AudioMixerDock;
class ISource;
class ChromaKeyFilter;
class GaussianBlurFilter;
class ColorCorrectionFilter;

/**
 * @brief Main application window
 * 
 * Professional streaming interface with:
 * - Central preview widget
 * - Scenes dock for scene management
 * - Sources dock for source management  
 * - Controls dock for streaming controls
 */
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

private slots:
    // Scene management
    void onSceneSelected(QListWidgetItem* current, QListWidgetItem* previous);
    void onAddScene();
    void onRemoveScene();
    
    // Source management
    void onSourceSelected(QListWidgetItem* current, QListWidgetItem* previous);
    void onAddSource();
    void onRemoveSource();
    
    // Streaming controls
    void onStartStreaming();
    void onStopStreaming();
    void onRecordClicked();
    void onPauseRecordingClicked();
    void onApplyFilter();
    void onTransitionTypeChanged(int index);
    void onTransitionDurationChanged(int durationMs);
    void onBrowseRecordingPath();
    void onSettingsClicked();
    void onSaveProfile();
    void onLoadProfile();
    void onSaveSceneCollection();
    void onLoadSceneCollection();
    
    // Updates
    void onPreviewFrame(const QImage& frame);
    void updateStatistics();
    void updateStreamState();
    void updateRecordingState();

private:
    void setupUI();
    void setupMenuBar();
    void setupCentralWidget();
    void setupDocks();
    void setupConnections();
    void initializeManagers();
    
    void createScenesDock();
    void createSourcesDock();
    void createAudioMixerDock();
    void createControlsDock();
    
    void refreshScenesList();
    void refreshSourcesList();
    void updateStreamButton();
    bool applyProfile(
        const StreamSettings& stream,
        const EncoderSettings& encoder,
        const RecordingSettings& recording,
        const QSize& outputResolution,
        double targetFps,
        bool encoderOutputEnabled,
        bool recordingOutputEnabled);
    
    // Central widget
    PreviewWidget* m_previewWidget = nullptr;
    
    // Docks
    QDockWidget* m_scenesDock = nullptr;
    QDockWidget* m_sourcesDock = nullptr;
    AudioMixerDock* m_audioMixerDock = nullptr;
    QDockWidget* m_controlsDock = nullptr;
    
    // Audio sources
    std::shared_ptr<ISource> m_desktopAudio;
    std::shared_ptr<ISource> m_micAudio;
    
    // Scenes dock widgets
    QListWidget* m_scenesList = nullptr;
    QPushButton* m_addSceneBtn = nullptr;
    QPushButton* m_removeSceneBtn = nullptr;
    
    // Sources dock widgets
    QListWidget* m_sourcesList = nullptr;
    QPushButton* m_addSourceBtn = nullptr;
    QPushButton* m_removeSourceBtn = nullptr;
    
    // Controls dock widgets
    QPushButton* m_startStreamBtn = nullptr;
    QPushButton* m_settingsBtn = nullptr;
    QPushButton* m_recordBtn = nullptr;
    QPushButton* m_pauseRecordBtn = nullptr;
    QLabel* m_recordDurationLabel = nullptr;

    // Scene transition controls.
    QComboBox* m_transitionTypeCombo = nullptr;
    QSpinBox* m_transitionDurationSpin = nullptr;

    // Basic built-in video filter controls.
    QComboBox* m_filterCombo = nullptr;
    QPushButton* m_applyFilterBtn = nullptr;
    
    // Status widgets
    QLabel* m_statusLabel = nullptr;
    QLabel* m_fpsLabel = nullptr;
    QLabel* m_bitrateLabel = nullptr;
    QLabel* m_compositingLabel = nullptr;
    QLabel* m_durationLabel = nullptr;
    
    // Timers
    QTimer* m_statsTimer = nullptr;

    int m_micTrackId = -1;

    std::unique_ptr<ChromaKeyFilter> m_chromaKeyFilter;
    std::unique_ptr<GaussianBlurFilter> m_gaussianBlurFilter;
    std::unique_ptr<ColorCorrectionFilter> m_colorCorrectionFilter;
};

} // namespace WeaR
