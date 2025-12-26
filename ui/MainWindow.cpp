// ==============================================================================
// WeaR-studio Main Window Implementation
// Professional OBS-like streaming interface
// ==============================================================================

#include "MainWindow.h"
#include "PreviewWidget.h"

#include <SceneManager.h>
#include <StreamManager.h>
#include <EncoderManager.h>
#include <CaptureManager.h>
#include <PluginManager.h>
#include <Scene.h>
#include <SceneItem.h>

#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QStatusBar>
#include <QDockWidget>
#include <QListWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QGroupBox>
#include <QMessageBox>
#include <QInputDialog>
#include <QDebug>

namespace WeaR {

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle("WeaR Studio");
    setMinimumSize(1280, 720);
    resize(1600, 900);
    
    setupUI();
    setupConnections();
    initializeManagers();
}

MainWindow::~MainWindow() {
    // Stop streaming if running
    StreamManager::instance().stopStream();
    
    // Stop scene rendering
    SceneManager::instance().stopRenderLoop();
}

void MainWindow::setupUI() {
    setupMenuBar();
    setupCentralWidget();
    setupDocks();
    
    // Setup status bar
    m_statusLabel = new QLabel("Ready");
    m_statusLabel->setObjectName("statusLabel");
    
    m_fpsLabel = new QLabel("FPS: --");
    m_bitrateLabel = new QLabel("Bitrate: --");
    m_durationLabel = new QLabel("Duration: 00:00:00");
    
    statusBar()->addWidget(m_statusLabel, 1);
    statusBar()->addPermanentWidget(m_fpsLabel);
    statusBar()->addPermanentWidget(m_bitrateLabel);
    statusBar()->addPermanentWidget(m_durationLabel);
    
    // Setup stats timer
    m_statsTimer = new QTimer(this);
    m_statsTimer->setInterval(1000);
    connect(m_statsTimer, &QTimer::timeout, this, &MainWindow::updateStatistics);
}

void MainWindow::setupMenuBar() {
    // File menu
    QMenu* fileMenu = menuBar()->addMenu("&File");
    
    QAction* newSceneAction = fileMenu->addAction("&New Scene");
    newSceneAction->setShortcut(QKeySequence::New);
    connect(newSceneAction, &QAction::triggered, this, &MainWindow::onAddScene);
    
    fileMenu->addSeparator();
    
    QAction* settingsAction = fileMenu->addAction("&Settings...");
    settingsAction->setShortcut(QKeySequence("Ctrl+,"));
    connect(settingsAction, &QAction::triggered, this, &MainWindow::onSettingsClicked);
    
    fileMenu->addSeparator();
    
    QAction* exitAction = fileMenu->addAction("E&xit");
    exitAction->setShortcut(QKeySequence::Quit);
    connect(exitAction, &QAction::triggered, this, &QWidget::close);
    
    // View menu
    QMenu* viewMenu = menuBar()->addMenu("&View");
    // Dock visibility actions will be added after docks are created
    
    // Stream menu
    QMenu* streamMenu = menuBar()->addMenu("&Stream");
    
    QAction* startAction = streamMenu->addAction("&Start Streaming");
    startAction->setShortcut(QKeySequence("F5"));
    connect(startAction, &QAction::triggered, this, &MainWindow::onStartStreaming);
    
    QAction* stopAction = streamMenu->addAction("S&top Streaming");
    stopAction->setShortcut(QKeySequence("F6"));
    connect(stopAction, &QAction::triggered, this, &MainWindow::onStopStreaming);
    
    // Help menu
    QMenu* helpMenu = menuBar()->addMenu("&Help");
    
    QAction* aboutAction = helpMenu->addAction("&About WeaR Studio");
    connect(aboutAction, &QAction::triggered, [this]() {
        QMessageBox::about(this, "About WeaR Studio",
            "<h2>WeaR Studio</h2>"
            "<p>Version 0.1</p>"
            "<p>Professional streaming software built with Qt and FFmpeg.</p>"
            "<p>Copyright © 2024 WeaR-studio</p>");
    });
}

void MainWindow::setupCentralWidget() {
    m_previewWidget = new PreviewWidget(this);
    setCentralWidget(m_previewWidget);
}

void MainWindow::setupDocks() {
    createScenesDock();
    createSourcesDock();
    createControlsDock();
    
    // Position docks
    addDockWidget(Qt::LeftDockWidgetArea, m_scenesDock);
    addDockWidget(Qt::LeftDockWidgetArea, m_sourcesDock);
    addDockWidget(Qt::RightDockWidgetArea, m_controlsDock);
    
    // Add dock toggle actions to View menu
    QMenu* viewMenu = menuBar()->findChild<QMenu*>();
    if (viewMenu && viewMenu->title() == "&View") {
        viewMenu->addAction(m_scenesDock->toggleViewAction());
        viewMenu->addAction(m_sourcesDock->toggleViewAction());
        viewMenu->addAction(m_controlsDock->toggleViewAction());
    }
}

void MainWindow::createScenesDock() {
    m_scenesDock = new QDockWidget("Scenes", this);
    m_scenesDock->setObjectName("scenesDock");
    m_scenesDock->setFeatures(QDockWidget::DockWidgetMovable | 
                              QDockWidget::DockWidgetFloatable);
    
    QWidget* container = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout(container);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);
    
    // Scene list
    m_scenesList = new QListWidget();
    m_scenesList->setAlternatingRowColors(false);
    layout->addWidget(m_scenesList);
    
    // Buttons
    QHBoxLayout* btnLayout = new QHBoxLayout();
    
    m_addSceneBtn = new QPushButton("+");
    m_addSceneBtn->setFixedWidth(32);
    m_addSceneBtn->setToolTip("Add Scene");
    
    m_removeSceneBtn = new QPushButton("-");
    m_removeSceneBtn->setFixedWidth(32);
    m_removeSceneBtn->setToolTip("Remove Scene");
    
    btnLayout->addWidget(m_addSceneBtn);
    btnLayout->addWidget(m_removeSceneBtn);
    btnLayout->addStretch();
    
    layout->addLayout(btnLayout);
    
    m_scenesDock->setWidget(container);
}

void MainWindow::createSourcesDock() {
    m_sourcesDock = new QDockWidget("Sources", this);
    m_sourcesDock->setObjectName("sourcesDock");
    m_sourcesDock->setFeatures(QDockWidget::DockWidgetMovable | 
                               QDockWidget::DockWidgetFloatable);
    
    QWidget* container = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout(container);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);
    
    // Source list
    m_sourcesList = new QListWidget();
    m_sourcesList->setAlternatingRowColors(false);
    layout->addWidget(m_sourcesList);
    
    // Buttons
    QHBoxLayout* btnLayout = new QHBoxLayout();
    
    m_addSourceBtn = new QPushButton("+");
    m_addSourceBtn->setFixedWidth(32);
    m_addSourceBtn->setToolTip("Add Source");
    
    m_removeSourceBtn = new QPushButton("-");
    m_removeSourceBtn->setFixedWidth(32);
    m_removeSourceBtn->setToolTip("Remove Source");
    
    btnLayout->addWidget(m_addSourceBtn);
    btnLayout->addWidget(m_removeSourceBtn);
    btnLayout->addStretch();
    
    layout->addLayout(btnLayout);
    
    m_sourcesDock->setWidget(container);
}

void MainWindow::createControlsDock() {
    m_controlsDock = new QDockWidget("Controls", this);
    m_controlsDock->setObjectName("controlsDock");
    m_controlsDock->setFeatures(QDockWidget::DockWidgetMovable | 
                                QDockWidget::DockWidgetFloatable);
    
    QWidget* container = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout(container);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(12);
    
    // Stream settings group
    QGroupBox* streamGroup = new QGroupBox("Stream Settings");
    QVBoxLayout* streamLayout = new QVBoxLayout(streamGroup);
    
    QLabel* urlLabel = new QLabel("Stream URL:");
    m_streamUrlEdit = new QLineEdit();
    m_streamUrlEdit->setPlaceholderText("rtmp://live.twitch.tv/app");
    m_streamUrlEdit->setText("rtmp://live.twitch.tv/app");
    
    QLabel* keyLabel = new QLabel("Stream Key:");
    m_streamKeyEdit = new QLineEdit();
    m_streamKeyEdit->setPlaceholderText("Enter stream key");
    m_streamKeyEdit->setEchoMode(QLineEdit::Password);
    
    streamLayout->addWidget(urlLabel);
    streamLayout->addWidget(m_streamUrlEdit);
    streamLayout->addWidget(keyLabel);
    streamLayout->addWidget(m_streamKeyEdit);
    
    layout->addWidget(streamGroup);
    
    // Action buttons
    QGroupBox* actionsGroup = new QGroupBox("Actions");
    QVBoxLayout* actionsLayout = new QVBoxLayout(actionsGroup);
    
    m_startStreamBtn = new QPushButton("Start Streaming");
    m_startStreamBtn->setObjectName("startStreamBtn");
    m_startStreamBtn->setMinimumHeight(40);
    
    m_settingsBtn = new QPushButton("Settings");
    m_settingsBtn->setMinimumHeight(32);
    
    actionsLayout->addWidget(m_startStreamBtn);
    actionsLayout->addWidget(m_settingsBtn);
    
    layout->addWidget(actionsGroup);
    
    // Spacer
    layout->addStretch();
    
    m_controlsDock->setWidget(container);
}

void MainWindow::setupConnections() {
    // Scene management
    connect(m_scenesList, &QListWidget::currentItemChanged,
            this, &MainWindow::onSceneSelected);
    connect(m_addSceneBtn, &QPushButton::clicked, this, &MainWindow::onAddScene);
    connect(m_removeSceneBtn, &QPushButton::clicked, this, &MainWindow::onRemoveScene);
    
    // Source management
    connect(m_sourcesList, &QListWidget::currentItemChanged,
            this, &MainWindow::onSourceSelected);
    connect(m_addSourceBtn, &QPushButton::clicked, this, &MainWindow::onAddSource);
    connect(m_removeSourceBtn, &QPushButton::clicked, this, &MainWindow::onRemoveSource);
    
    // Streaming controls
    connect(m_startStreamBtn, &QPushButton::clicked, [this]() {
        if (StreamManager::instance().isStreaming()) {
            onStopStreaming();
        } else {
            onStartStreaming();
        }
    });
    connect(m_settingsBtn, &QPushButton::clicked, this, &MainWindow::onSettingsClicked);
    
    // Stream state changes
    connect(&StreamManager::instance(), &StreamManager::stateChanged,
            this, &MainWindow::updateStreamState);
}

void MainWindow::initializeManagers() {
    // Initialize capture manager
    CaptureManager::instance().initialize();
    
    // Initialize plugin manager
    PluginManager::instance().discoverPlugins();
    PluginManager::instance().loadAllPlugins();
    
    // Configure encoder
    EncoderSettings encSettings;
    encSettings.width = 1920;
    encSettings.height = 1080;
    encSettings.fpsNum = 60;
    encSettings.bitrate = 6000;
    EncoderManager::instance().configure(encSettings);
    
    // Set up scene manager preview callback
    SceneManager::instance().setPreviewCallback([this](const QImage& frame) {
        // Use queued connection to update UI from render thread
        QMetaObject::invokeMethod(m_previewWidget, "updateFrame",
                                  Qt::QueuedConnection,
                                  Q_ARG(QImage, frame));
    });
    
    // Refresh UI
    refreshScenesList();
    refreshSourcesList();
    
    // Start render loop
    SceneManager::instance().startRenderLoop();
    
    // Start stats timer
    m_statsTimer->start();
    
    m_statusLabel->setText("Ready");
    qDebug() << "Managers initialized";
}

void MainWindow::onSceneSelected(QListWidgetItem* current, QListWidgetItem* /*previous*/) {
    if (!current) return;
    
    QString sceneName = current->text();
    Scene* scene = SceneManager::instance().sceneByName(sceneName);
    
    if (scene) {
        SceneManager::instance().setActiveScene(scene);
        refreshSourcesList();
    }
}

void MainWindow::onAddScene() {
    bool ok;
    QString name = QInputDialog::getText(this, "New Scene",
                                         "Scene name:", QLineEdit::Normal,
                                         QString("Scene %1").arg(SceneManager::instance().sceneCount() + 1),
                                         &ok);
    if (ok && !name.isEmpty()) {
        SceneManager::instance().createScene(name);
        refreshScenesList();
    }
}

void MainWindow::onRemoveScene() {
    QListWidgetItem* current = m_scenesList->currentItem();
    if (!current) return;
    
    if (SceneManager::instance().sceneCount() <= 1) {
        QMessageBox::warning(this, "Cannot Remove", "At least one scene is required.");
        return;
    }
    
    QString sceneName = current->text();
    Scene* scene = SceneManager::instance().sceneByName(sceneName);
    
    if (scene) {
        SceneManager::instance().removeScene(scene);
        refreshScenesList();
    }
}

void MainWindow::onSourceSelected(QListWidgetItem* current, QListWidgetItem* /*previous*/) {
    Q_UNUSED(current);
    // Could highlight/select the source in the scene
}

void MainWindow::onAddSource() {
    Scene* activeScene = SceneManager::instance().activeScene();
    if (!activeScene) return;
    
    // Get available source types from plugin manager
    QStringList sourceTypes;
    sourceTypes << "Screen Capture";
    sourceTypes << "Color Source";
    
    // Add sources from plugin manager
    for (ISource* source : PluginManager::instance().availableSources()) {
        if (source) {
            sourceTypes << source->name();
        }
    }
    
    bool ok;
    QString sourceType = QInputDialog::getItem(this, "Add Source",
                                               "Select source type:",
                                               sourceTypes, 0, false, &ok);
    if (!ok || sourceType.isEmpty()) return;
    
    // Get source name
    QString sourceName = QInputDialog::getText(this, "Add Source",
                                               "Source name:", QLineEdit::Normal,
                                               sourceType, &ok);
    if (!ok || sourceName.isEmpty()) return;
    
    // Create the source
    ISource* source = nullptr;
    
    if (sourceType == "Screen Capture") {
        source = &CaptureManager::instance();
        if (!source->isRunning()) {
            // Set a default capture target
            auto targets = CaptureManager::instance().enumerateMonitors();
            if (!targets.isEmpty()) {
                CaptureManager::instance().setTarget(targets.first());
                CaptureManager::instance().start();
            }
        }
    } else if (sourceType == "Color Source") {
        source = PluginManager::instance().createSource("wear.source.color");
        if (source) {
            source->start();
        }
    } else {
        // Try to get from plugin manager
        for (ISource* s : PluginManager::instance().availableSources()) {
            if (s && s->name() == sourceType) {
                source = s;
                if (!source->isRunning()) {
                    source->start();
                }
                break;
            }
        }
    }
    
    if (source) {
        activeScene->addItem(sourceName, source);
        refreshSourcesList();
    }
}

void MainWindow::onRemoveSource() {
    Scene* activeScene = SceneManager::instance().activeScene();
    if (!activeScene) return;
    
    QListWidgetItem* current = m_sourcesList->currentItem();
    if (!current) return;
    
    QString sourceName = current->text();
    SceneItem* item = activeScene->itemByName(sourceName);
    
    if (item) {
        activeScene->removeItem(item);
        refreshSourcesList();
    }
}

void MainWindow::onStartStreaming() {
    QString url = m_streamUrlEdit->text().trimmed();
    QString key = m_streamKeyEdit->text().trimmed();
    
    if (url.isEmpty()) {
        QMessageBox::warning(this, "Missing URL", "Please enter a stream URL.");
        return;
    }
    
    // Configure stream settings
    StreamSettings settings;
    settings.url = url;
    settings.streamKey = key;
    settings.videoWidth = 1920;
    settings.videoHeight = 1080;
    settings.videoFpsNum = 60;
    settings.videoBitrate = 6000;
    
    StreamManager::instance().configure(settings);
    
    // Start encoder first
    if (!EncoderManager::instance().isRunning()) {
        EncoderManager::instance().start();
    }
    
    // Connect encoder to stream
    EncoderManager::instance().setPacketCallback([](const EncodedPacket& pkt) {
        StreamManager::instance().writePacket(pkt.data, pkt.size,
                                              pkt.pts, pkt.dts, pkt.isKeyframe);
    });
    
    // Enable encoder output from scene manager
    SceneManager::instance().setEncoderOutputEnabled(true);
    
    // Start streaming
    if (StreamManager::instance().startStream()) {
        m_statusLabel->setText("Connecting...");
    } else {
        QMessageBox::critical(this, "Stream Error", "Failed to start streaming.");
    }
}

void MainWindow::onStopStreaming() {
    SceneManager::instance().setEncoderOutputEnabled(false);
    EncoderManager::instance().stop();
    StreamManager::instance().stopStream();
    m_statusLabel->setText("Stopped");
}

void MainWindow::onSettingsClicked() {
    QMessageBox::information(this, "Settings", 
                             "Settings dialog coming soon!\n\n"
                             "Configure output resolution, bitrate, encoder, etc.");
}

void MainWindow::onPreviewFrame(const QImage& frame) {
    m_previewWidget->updateFrame(frame);
}

void MainWindow::updateStatistics() {
    // Render stats
    RenderStatistics renderStats = SceneManager::instance().statistics();
    m_fpsLabel->setText(QString("FPS: %1").arg(renderStats.currentFps, 0, 'f', 1));
    
    // Stream stats
    if (StreamManager::instance().isStreaming()) {
        StreamStatistics streamStats = StreamManager::instance().statistics();
        m_bitrateLabel->setText(QString("Bitrate: %1 kbps")
                                .arg(streamStats.currentBitrateKbps, 0, 'f', 0));
        
        // Duration
        int64_t ms = streamStats.streamDurationMs;
        int seconds = (ms / 1000) % 60;
        int minutes = (ms / 60000) % 60;
        int hours = ms / 3600000;
        m_durationLabel->setText(QString("Duration: %1:%2:%3")
                                 .arg(hours, 2, 10, QChar('0'))
                                 .arg(minutes, 2, 10, QChar('0'))
                                 .arg(seconds, 2, 10, QChar('0')));
    } else {
        m_bitrateLabel->setText("Bitrate: --");
        m_durationLabel->setText("Duration: 00:00:00");
    }
}

void MainWindow::updateStreamState() {
    StreamState state = StreamManager::instance().state();
    
    switch (state) {
        case StreamState::Stopped:
            m_statusLabel->setText("Ready");
            m_startStreamBtn->setText("Start Streaming");
            m_startStreamBtn->setObjectName("startStreamBtn");
            break;
            
        case StreamState::Connecting:
            m_statusLabel->setText("Connecting...");
            m_startStreamBtn->setText("Connecting...");
            m_startStreamBtn->setEnabled(false);
            break;
            
        case StreamState::Streaming:
            m_statusLabel->setText("Live");
            m_statusLabel->setObjectName("successLabel");
            m_startStreamBtn->setText("Stop Streaming");
            m_startStreamBtn->setObjectName("stopStreamBtn");
            m_startStreamBtn->setEnabled(true);
            break;
            
        case StreamState::Reconnecting:
            m_statusLabel->setText("Reconnecting...");
            m_statusLabel->setObjectName("errorLabel");
            break;
            
        case StreamState::Error:
            m_statusLabel->setText("Error");
            m_statusLabel->setObjectName("errorLabel");
            m_startStreamBtn->setText("Start Streaming");
            m_startStreamBtn->setObjectName("startStreamBtn");
            m_startStreamBtn->setEnabled(true);
            break;
    }
    
    // Force style update
    m_startStreamBtn->style()->unpolish(m_startStreamBtn);
    m_startStreamBtn->style()->polish(m_startStreamBtn);
    m_statusLabel->style()->unpolish(m_statusLabel);
    m_statusLabel->style()->polish(m_statusLabel);
}

void MainWindow::refreshScenesList() {
    m_scenesList->clear();
    
    Scene* activeScene = SceneManager::instance().activeScene();
    
    for (Scene* scene : SceneManager::instance().scenes()) {
        QListWidgetItem* item = new QListWidgetItem(scene->name());
        m_scenesList->addItem(item);
        
        if (scene == activeScene) {
            m_scenesList->setCurrentItem(item);
        }
    }
}

void MainWindow::refreshSourcesList() {
    m_sourcesList->clear();
    
    Scene* activeScene = SceneManager::instance().activeScene();
    if (!activeScene) return;
    
    for (SceneItem* item : activeScene->items()) {
        QListWidgetItem* listItem = new QListWidgetItem(item->name());
        
        // Show visibility
        if (!item->isVisible()) {
            listItem->setForeground(QColor(128, 128, 128));
        }
        
        m_sourcesList->addItem(listItem);
    }
}

} // namespace WeaR
