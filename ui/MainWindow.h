#pragma once
// ==============================================================================
// WeaR-studio Main Window
// Professional OBS-like streaming interface
// ==============================================================================

#include <QMainWindow>
#include <QTimer>

class QListWidget;
class QListWidgetItem;
class QLabel;
class QLineEdit;
class QPushButton;
class QDockWidget;

namespace WeaR {

class PreviewWidget;

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
    void onSettingsClicked();
    
    // Updates
    void onPreviewFrame(const QImage& frame);
    void updateStatistics();
    void updateStreamState();

private:
    void setupUI();
    void setupMenuBar();
    void setupCentralWidget();
    void setupDocks();
    void setupConnections();
    void initializeManagers();
    
    void createScenesDock();
    void createSourcesDock();
    void createControlsDock();
    
    void refreshScenesList();
    void refreshSourcesList();
    void updateStreamButton();
    
    // Central widget
    PreviewWidget* m_previewWidget = nullptr;
    
    // Docks
    QDockWidget* m_scenesDock = nullptr;
    QDockWidget* m_sourcesDock = nullptr;
    QDockWidget* m_controlsDock = nullptr;
    
    // Scenes dock widgets
    QListWidget* m_scenesList = nullptr;
    QPushButton* m_addSceneBtn = nullptr;
    QPushButton* m_removeSceneBtn = nullptr;
    
    // Sources dock widgets
    QListWidget* m_sourcesList = nullptr;
    QPushButton* m_addSourceBtn = nullptr;
    QPushButton* m_removeSourceBtn = nullptr;
    
    // Controls dock widgets
    QLineEdit* m_streamUrlEdit = nullptr;
    QLineEdit* m_streamKeyEdit = nullptr;
    QPushButton* m_startStreamBtn = nullptr;
    QPushButton* m_settingsBtn = nullptr;
    
    // Status widgets
    QLabel* m_statusLabel = nullptr;
    QLabel* m_fpsLabel = nullptr;
    QLabel* m_bitrateLabel = nullptr;
    QLabel* m_durationLabel = nullptr;
    
    // Timers
    QTimer* m_statsTimer = nullptr;
};

} // namespace WeaR
