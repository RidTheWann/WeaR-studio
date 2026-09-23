// ==============================================================================
// WeaR-studio Main Window Implementation
// Professional OBS-like streaming interface
// ==============================================================================

#include "MainWindow.h"
#include "PreviewWidget.h"
#include "AudioMixerDock.h"

#include <SceneManager.h>
#include <StreamManager.h>
#include <EncoderManager.h>
#include <RecordingManager.h>
#include <CaptureManager.h>
#include <AudioMixer.h>
#include <AudioCaptureSource.h>
#include <BuiltinRhiFilters.h>
#include <PluginManager.h>
#include "SettingsDialog.h"
#include <ProjectPersistence.h>
#include <GlobalHotkeyManager.h>
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
#include <QComboBox>
#include <QSpinBox>
#include <QFileDialog>
#include <QStandardPaths>
#include <QDir>
#include <QFileInfo>
#include <QPushButton>
#include <QGroupBox>
#include <QMessageBox>
#include <QInputDialog>
#include <QDebug>
#include <QSignalBlocker>
#include <algorithm>

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
    GlobalHotkeyManager::instance().shutdown();

    // Finalize the independent recording file before shutting down the render loop.
    SceneManager::instance().setRecordingOutputEnabled(false);
    RecordingManager::instance().stopRecording();

    // Stop streaming if running.
    SceneManager::instance().setEncoderOutputEnabled(false);
    StreamManager::instance().stopStream();
    EncoderManager::instance().stop();
    
    // Stop scene rendering
    SceneManager::instance().stopRenderLoop();

    // Release built-in filters after scene items stop referencing them.
    if (m_chromaKeyFilter) {
        m_chromaKeyFilter->shutdown();
    }
    if (m_gaussianBlurFilter) {
        m_gaussianBlurFilter->shutdown();
    }
    if (m_colorCorrectionFilter) {
        m_colorCorrectionFilter->shutdown();
    }

    // Stop audio sources
    if (m_desktopAudio) {
        m_desktopAudio->stop();
    }
    if (m_micAudio) {
        m_micAudio->stop();
    }
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
    m_compositingLabel = new QLabel("Comp: --");
    m_durationLabel = new QLabel("Duration: 00:00:00");
    
    statusBar()->addWidget(m_statusLabel, 1);
    statusBar()->addPermanentWidget(m_fpsLabel);
    statusBar()->addPermanentWidget(m_bitrateLabel);
    statusBar()->addPermanentWidget(m_compositingLabel);
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
    
    QAction* saveProfileAction = fileMenu->addAction("Save &Profile...");
    connect(saveProfileAction, &QAction::triggered,
            this, &MainWindow::onSaveProfile);

    QAction* loadProfileAction = fileMenu->addAction("&Load Profile...");
    connect(loadProfileAction, &QAction::triggered,
            this, &MainWindow::onLoadProfile);

    QAction* saveCollectionAction =
        fileMenu->addAction("Save &Scene Collection...");
    connect(saveCollectionAction, &QAction::triggered,
            this, &MainWindow::onSaveSceneCollection);

    QAction* loadCollectionAction =
        fileMenu->addAction("Load S&cene Collection...");
    connect(loadCollectionAction, &QAction::triggered,
            this, &MainWindow::onLoadSceneCollection);

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
    createAudioMixerDock();
    createControlsDock();
    
    // Position docks
    addDockWidget(Qt::LeftDockWidgetArea, m_scenesDock);
    addDockWidget(Qt::LeftDockWidgetArea, m_sourcesDock);
    addDockWidget(Qt::BottomDockWidgetArea, m_audioMixerDock);
    addDockWidget(Qt::RightDockWidgetArea, m_controlsDock);
    
    // Add dock toggle actions to View menu
    QMenu* viewMenu = menuBar()->findChild<QMenu*>();
    if (viewMenu && viewMenu->title() == "&View") {
        viewMenu->addAction(m_scenesDock->toggleViewAction());
        viewMenu->addAction(m_sourcesDock->toggleViewAction());
        viewMenu->addAction(m_audioMixerDock->toggleViewAction());
        viewMenu->addAction(m_controlsDock->toggleViewAction());
    }
}

void MainWindow::createAudioMixerDock() {
    m_audioMixerDock = new AudioMixerDock(this);
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
    m_controlsDock->setFeatures(
        QDockWidget::DockWidgetMovable |
        QDockWidget::DockWidgetFloatable);

    auto* container = new QWidget();
    auto* layout = new QVBoxLayout(container);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(12);

    auto* outputGroup = new QGroupBox("Output");
    auto* outputLayout = new QVBoxLayout(outputGroup);
    auto* outputInfo = new QLabel(
        "Stream, video, audio and recording configuration are managed in "
        "Settings. Save reusable output configuration as a Profile.",
        outputGroup);
    outputInfo->setWordWrap(true);
    outputLayout->addWidget(outputInfo);
    layout->addWidget(outputGroup);

    auto* recordingGroup = new QGroupBox("Recording");
    auto* recordingLayout = new QVBoxLayout(recordingGroup);
    auto* recordButtons = new QHBoxLayout();

    m_recordBtn = new QPushButton("Start Recording");
    m_recordBtn->setObjectName("startRecordBtn");
    m_recordBtn->setMinimumHeight(40);

    m_pauseRecordBtn = new QPushButton("Pause");
    m_pauseRecordBtn->setEnabled(false);
    m_pauseRecordBtn->setMinimumHeight(40);

    m_recordDurationLabel = new QLabel("Recording: 00:00:00");
    m_recordDurationLabel->setMinimumWidth(125);
    m_recordDurationLabel->setAlignment(Qt::AlignCenter);

    recordButtons->addWidget(m_recordBtn, 2);
    recordButtons->addWidget(m_pauseRecordBtn, 1);
    recordButtons->addWidget(m_recordDurationLabel);
    recordingLayout->addLayout(recordButtons);
    layout->addWidget(recordingGroup);

    auto* transitionGroup = new QGroupBox("Scene Transition");
    auto* transitionLayout = new QVBoxLayout(transitionGroup);

    auto* transitionTypeLayout = new QHBoxLayout();
    transitionTypeLayout->addWidget(new QLabel("Type:"));
    m_transitionTypeCombo = new QComboBox();
    m_transitionTypeCombo->addItems({"Cut", "Fade", "Slide"});
    transitionTypeLayout->addWidget(m_transitionTypeCombo, 1);
    transitionLayout->addLayout(transitionTypeLayout);

    auto* transitionDurationLayout = new QHBoxLayout();
    transitionDurationLayout->addWidget(new QLabel("Duration:"));
    m_transitionDurationSpin = new QSpinBox();
    m_transitionDurationSpin->setRange(0, 10000);
    m_transitionDurationSpin->setSingleStep(50);
    m_transitionDurationSpin->setSuffix(" ms");
    m_transitionDurationSpin->setValue(
        SceneManager::instance().transitionDuration(SceneTransitionType::Fade));
    transitionDurationLayout->addWidget(m_transitionDurationSpin, 1);
    transitionLayout->addLayout(transitionDurationLayout);
    layout->addWidget(transitionGroup);

    auto* filterGroup = new QGroupBox("Video Filter");
    auto* filterLayout = new QHBoxLayout(filterGroup);

    m_filterCombo = new QComboBox();
    m_filterCombo->addItems(
        {"None", "Chroma Key", "Gaussian Blur", "Color Correction"});
    m_applyFilterBtn = new QPushButton("Apply");
    m_applyFilterBtn->setMinimumHeight(30);

    filterLayout->addWidget(m_filterCombo, 1);
    filterLayout->addWidget(m_applyFilterBtn);
    layout->addWidget(filterGroup);

    auto* actionsGroup = new QGroupBox("Actions");
    auto* actionsLayout = new QVBoxLayout(actionsGroup);

    m_startStreamBtn = new QPushButton("Start Streaming");
    m_startStreamBtn->setObjectName("startStreamBtn");
    m_startStreamBtn->setMinimumHeight(40);

    m_settingsBtn = new QPushButton("Settings");
    m_settingsBtn->setMinimumHeight(32);

    actionsLayout->addWidget(m_startStreamBtn);
    actionsLayout->addWidget(m_settingsBtn);
    layout->addWidget(actionsGroup);

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
    connect(m_recordBtn, &QPushButton::clicked, this, &MainWindow::onRecordClicked);
    connect(m_pauseRecordBtn, &QPushButton::clicked,
            this, &MainWindow::onPauseRecordingClicked);
    connect(m_applyFilterBtn, &QPushButton::clicked,
            this, &MainWindow::onApplyFilter);
    connect(m_transitionTypeCombo, &QComboBox::currentIndexChanged,
            this, &MainWindow::onTransitionTypeChanged);
    connect(m_transitionDurationSpin, &QSpinBox::valueChanged,
            this, &MainWindow::onTransitionDurationChanged);

    auto& hotkeys = GlobalHotkeyManager::instance();

    hotkeys.setCallback(GlobalHotkeyAction::StartStream, [this]() {
        if (!StreamManager::instance().isConnected()) {
            QMetaObject::invokeMethod(
                this, &MainWindow::onStartStreaming,
                Qt::QueuedConnection);
        }
    });

    hotkeys.setCallback(GlobalHotkeyAction::StopStream, [this]() {
        if (StreamManager::instance().isConnected()) {
            QMetaObject::invokeMethod(
                this, &MainWindow::onStopStreaming,
                Qt::QueuedConnection);
        }
    });

    hotkeys.setCallback(GlobalHotkeyAction::StartRecord, [this]() {
        if (!RecordingManager::instance().isRecording() &&
            !RecordingManager::instance().isPaused()) {
            QMetaObject::invokeMethod(
                this, &MainWindow::onRecordClicked,
                Qt::QueuedConnection);
        }
    });

    hotkeys.setCallback(GlobalHotkeyAction::StopRecord, [this]() {
        if (RecordingManager::instance().isRecording() ||
            RecordingManager::instance().isPaused()) {
            QMetaObject::invokeMethod(this, [this]() {
                onRecordClicked();
            }, Qt::QueuedConnection);
        }
    });

    hotkeys.setCallback(GlobalHotkeyAction::NextScene, [this]() {
        QMetaObject::invokeMethod(this, [this]() {
            const QList<Scene*> scenes = SceneManager::instance().scenes();
            if (scenes.size() < 2) return;
            Scene* active = SceneManager::instance().activeScene();
            const int index = std::max(0, scenes.indexOf(active));
            SceneManager::instance().setActiveScene(
                scenes.at((index + 1) % scenes.size()));
            refreshScenesList();
            refreshSourcesList();
        }, Qt::QueuedConnection);
    });

    hotkeys.setCallback(GlobalHotkeyAction::ToggleMicMute, [this]() {
        QMetaObject::invokeMethod(this, [this]() {
            if (m_micTrackId < 0) return;
            auto& mixer = AudioMixer::instance();
            mixer.setMuted(
                m_micTrackId,
                !mixer.isMuted(m_micTrackId));
        }, Qt::QueuedConnection);
    });

    connect(&RecordingManager::instance(), &RecordingManager::stateChanged,
            this, &MainWindow::updateRecordingState);
    connect(&RecordingManager::instance(), &RecordingManager::recordingError,
            this, [this](const QString& error) {
                m_statusLabel->setText(QString("Recording error: %1").arg(error));
                QMessageBox::warning(this, "Recording Error", error);
            });
    
    // Stream state changes
    connect(&StreamManager::instance(), &StreamManager::stateChanged,
            this, &MainWindow::updateStreamState);
}

void MainWindow::initializeManagers() {
    if (m_transitionTypeCombo) {
        m_transitionTypeCombo->setCurrentIndex(
            static_cast<int>(SceneManager::instance().transitionType()));
    }

    // Initialize capture manager
    CaptureManager::instance().initialize();
    
    // Initialize plugin manager
    PluginManager::instance().discoverPlugins();
    PluginManager::instance().loadAllPlugins();
    
    // Configure encoder (video + audio)
    EncoderSettings encSettings;
    encSettings.width = 1920;
    encSettings.height = 1080;
    encSettings.fpsNum = 60;
    encSettings.bitrate = 6000;
    encSettings.audioEnabled = true;
    encSettings.audioSampleRate = 48000;
    encSettings.audioChannels = 2;
    encSettings.audioBitrate = 160;
    EncoderManager::instance().configure(encSettings);

    // Initialize built-in GPU-capable filters
    m_chromaKeyFilter = std::make_unique<ChromaKeyFilter>();
    m_chromaKeyFilter->initialize();

    m_gaussianBlurFilter = std::make_unique<GaussianBlurFilter>();
    m_gaussianBlurFilter->initialize();

    m_colorCorrectionFilter = std::make_unique<ColorCorrectionFilter>();
    m_colorCorrectionFilter->initialize();

    // Initialize audio capture sources
    m_desktopAudio = std::make_shared<DesktopAudioSource>();
    m_desktopAudio->start();
    AudioMixer::instance().addTrack(m_desktopAudio);

    m_micAudio = std::make_shared<MicrophoneAudioSource>();
    m_micAudio->start();
    m_micTrackId = AudioMixer::instance().addTrack(m_micAudio);

    RecordingSettings recordingDefaults =
        RecordingManager::instance().settings();
    if (recordingDefaults.outputPath.isEmpty()) {
        const QString moviesDir =
            QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
        recordingDefaults.outputPath = QDir(
            moviesDir.isEmpty() ? QDir::homePath() : moviesDir)
            .filePath("WeaR-recording.mkv");
        RecordingManager::instance().configure(recordingDefaults);
    }

    if (m_audioMixerDock) {
        m_audioMixerDock->refreshTracks();
    }
    
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

void MainWindow::onSourceSelected(
    QListWidgetItem* current,
    QListWidgetItem* /*previous*/) {
    if (!current || !m_filterCombo) {
        return;
    }

    Scene* activeScene = SceneManager::instance().activeScene();
    SceneItem* item = activeScene
        ? activeScene->itemByName(current->text())
        : nullptr;

    if (!item || !item->filter()) {
        m_filterCombo->setCurrentIndex(0);
        return;
    }

    if (item->filter() == m_chromaKeyFilter.get()) {
        m_filterCombo->setCurrentIndex(1);
    } else if (item->filter() == m_gaussianBlurFilter.get()) {
        m_filterCombo->setCurrentIndex(2);
    } else if (item->filter() == m_colorCorrectionFilter.get()) {
        m_filterCombo->setCurrentIndex(3);
    } else {
        m_filterCombo->setCurrentIndex(0);
    }
}

void MainWindow::onTransitionTypeChanged(int index) {
    if (!m_transitionTypeCombo || !m_transitionDurationSpin ||
        index < 0 || index > 2) {
        return;
    }

    const auto type = static_cast<SceneTransitionType>(index);
    auto& manager = SceneManager::instance();
    manager.setTransitionType(type);

    const QSignalBlocker blocker(m_transitionDurationSpin);
    m_transitionDurationSpin->setValue(manager.transitionDuration(type));
}

void MainWindow::onTransitionDurationChanged(int durationMs) {
    if (!m_transitionTypeCombo) {
        return;
    }

    const int index = m_transitionTypeCombo->currentIndex();
    if (index < 0 || index > 2) {
        return;
    }

    SceneManager::instance().setTransitionDuration(
        static_cast<SceneTransitionType>(index),
        durationMs);
}

void MainWindow::onApplyFilter() {
    if (!m_filterCombo) {
        return;
    }

    QListWidgetItem* current = m_sourcesList->currentItem();
    Scene* activeScene = SceneManager::instance().activeScene();
    if (!current || !activeScene) {
        return;
    }

    SceneItem* item = activeScene->itemByName(current->text());
    if (!item) {
        return;
    }

    IFilter* filter = nullptr;
    switch (m_filterCombo->currentIndex()) {
        case 1:
            filter = m_chromaKeyFilter.get();
            break;
        case 2:
            filter = m_gaussianBlurFilter.get();
            break;
        case 3:
            filter = m_colorCorrectionFilter.get();
            break;
        default:
            filter = nullptr;
            break;
    }

    item->setFilter(filter);
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

void MainWindow::onRecordClicked() {
    auto& recorder = RecordingManager::instance();

    if (recorder.isRecording() || recorder.isPaused()) {
        recorder.stopRecording();
        SceneManager::instance().setRecordingOutputEnabled(false);
        return;
    }

    RecordingSettings settings = recorder.settings();
    QString path = settings.outputPath.trimmed();

    if (path.isEmpty()) {
        path = QFileDialog::getSaveFileName(
            this,
            "Choose recording output",
            QStandardPaths::writableLocation(QStandardPaths::MoviesLocation),
            "MKV Video (*.mkv);;MP4 Video (*.mp4);;FLV Video (*.flv);;All Files (*)");
        if (path.isEmpty()) return;
    }

    const QString extension =
        settings.format == RecordingFormat::MP4
            ? "mp4"
            : (settings.format == RecordingFormat::FLV ? "flv" : "mkv");

    QFileInfo info(path);
    if (info.suffix().compare(extension, Qt::CaseInsensitive) != 0) {
        const QString base = info.completeBaseName().isEmpty()
            ? "WeaR-recording"
            : info.completeBaseName();
        path = QDir(info.path()).filePath(base + "." + extension);
    }

    settings.outputPath = path;
    if (!recorder.configure(settings) ||
        !recorder.startRecording(path)) {
        const QString error = recorder.lastError();
        if (!error.isEmpty()) {
            QMessageBox::warning(this, "Recording Error", error);
        }
        return;
    }

    SceneManager::instance().setRecordingOutputEnabled(true);
}

void MainWindow::onPauseRecordingClicked() {
    auto& recorder = RecordingManager::instance();

    if (recorder.isPaused()) {
        recorder.resumeRecording();
    } else if (recorder.isRecording()) {
        recorder.pauseRecording();
    }
}

void MainWindow::onStartStreaming() {
    auto streamSettings = StreamManager::instance().settings();
    auto encoderSettings = EncoderManager::instance().settings();

    if (streamSettings.url.trimmed().isEmpty()) {
        QMessageBox::warning(
            this, "Missing URL",
            "Configure the stream URL in Settings before starting.");
        return;
    }

    encoderSettings.width = streamSettings.videoWidth;
    encoderSettings.height = streamSettings.videoHeight;
    encoderSettings.fpsNum = streamSettings.videoFpsNum;
    encoderSettings.fpsDen = streamSettings.videoFpsDen;
    encoderSettings.bitrate = streamSettings.videoBitrate;
    encoderSettings.audioEnabled = streamSettings.audioEnabled;
    encoderSettings.audioSampleRate = streamSettings.audioSampleRate;
    encoderSettings.audioChannels = streamSettings.audioChannels;
    encoderSettings.audioBitrate = streamSettings.audioBitrate;

    if (!EncoderManager::instance().configure(encoderSettings) ||
        !StreamManager::instance().configure(streamSettings)) {
        QMessageBox::critical(
            this, "Stream Error",
            "Failed to apply stream/encoder settings.");
        return;
    }

    if (!EncoderManager::instance().isRunning() &&
        !EncoderManager::instance().start()) {
        QMessageBox::critical(
            this, "Stream Error",
            "Failed to start the encoder.");
        return;
    }

    StreamManager::instance().setVideoCodecParameters(
        EncoderManager::instance().videoCodecParameters());
    StreamManager::instance().setAudioCodecParameters(
        EncoderManager::instance().audioCodecParameters());

    EncoderManager::instance().setPacketCallback([](const EncodedPacket& pkt) {
        StreamManager::instance().writePacket(
            pkt.data, pkt.size, pkt.pts, pkt.dts,
            pkt.isKeyframe, pkt.isAudio);
    });    hotkeys.initialize();



    SceneManager::instance().setEncoderOutputEnabled(true);

    if (StreamManager::instance().startStream()) {
        m_statusLabel->setText("Connecting...");
    } else {
        SceneManager::instance().setEncoderOutputEnabled(false);
        EncoderManager::instance().stop();
        QMessageBox::critical(
            this, "Stream Error",
            "Failed to start streaming.");
    }
}

void MainWindow::onStopStreaming() {
    SceneManager::instance().setEncoderOutputEnabled(false);
    StreamManager::instance().stopStream();
    EncoderManager::instance().stop();
    m_statusLabel->setText(
        RecordingManager::instance().isRecording() || RecordingManager::instance().isPaused()
            ? "Recording"
            : "Stopped");
}

void MainWindow::onSettingsClicked() {
    SettingsDialog dialog(m_micTrackId, this);
    dialog.exec();
}

bool MainWindow::applyProfile(
    const StreamSettings& stream,
    const EncoderSettings& encoder,
    const RecordingSettings& recording,
    const QSize& outputResolution,
    double targetFps) {

    if (StreamManager::instance().isConnected() ||
        RecordingManager::instance().isRecording() ||
        RecordingManager::instance().isPaused() ||
        EncoderManager::instance().isRunning()) {
        QMessageBox::warning(
            this, "Profile",
            "Stop streaming and recording before loading a profile.");
        return false;
    }

    if (!StreamManager::instance().configure(stream) ||
        !EncoderManager::instance().configure(encoder) ||
        !RecordingManager::instance().configure(recording)) {
        return false;
    }

    SceneManager::instance().setOutputResolution(outputResolution);
    SceneManager::instance().setTargetFps(targetFps);
    SceneManager::instance().setEncoderOutputEnabled(false);
    SceneManager::instance().setRecordingOutputEnabled(false);
    return true;
}

void MainWindow::onSaveProfile() {
    const QString path = QFileDialog::getSaveFileName(
        this, "Save Profile", QDir::homePath(),
        "WeaR Profile (*.json)");
    if (path.isEmpty()) return;

    QString error;
    if (!ProjectPersistence::saveProfile(
            path,
            StreamManager::instance().settings(),
            EncoderManager::instance().settings(),
            RecordingManager::instance().settings(),
            SceneManager::instance().outputResolution(),
            SceneManager::instance().targetFps(),
            &error)) {
        QMessageBox::warning(this, "Save Profile", error);
        return;
    }

    m_statusLabel->setText(QString("Profile saved: %1").arg(path));
}

void MainWindow::onLoadProfile() {
    const QString path = QFileDialog::getOpenFileName(
        this, "Load Profile", QDir::homePath(),
        "WeaR Profile (*.json)");
    if (path.isEmpty()) return;

    StreamSettings stream = StreamManager::instance().settings();
    EncoderSettings encoder = EncoderManager::instance().settings();
    RecordingSettings recording = RecordingManager::instance().settings();
    QSize outputResolution = SceneManager::instance().outputResolution();
    double targetFps = SceneManager::instance().targetFps();

    QString error;
    if (!ProjectPersistence::loadProfile(
            path, stream, encoder, recording,
            outputResolution, targetFps, &error)) {
        QMessageBox::warning(this, "Load Profile", error);
        return;
    }

    if (!applyProfile(
            stream, encoder, recording,
            outputResolution, targetFps)) {
        QMessageBox::warning(
            this, "Load Profile",
            "The profile settings could not be applied.");
        return;
    }

    m_statusLabel->setText(QString("Profile loaded: %1").arg(path));
}

void MainWindow::onSaveSceneCollection() {
    const QString path = QFileDialog::getSaveFileName(
        this, "Save Scene Collection", QDir::homePath(),
        "WeaR Scene Collection (*.json)");
    if (path.isEmpty()) return;

    QString error;
    if (!ProjectPersistence::saveSceneCollection(
            path, SceneManager::instance(), &error)) {
        QMessageBox::warning(this, "Save Scene Collection", error);
        return;
    }

    m_statusLabel->setText(
        QString("Scene collection saved: %1").arg(path));
}

void MainWindow::onLoadSceneCollection() {
    if (StreamManager::instance().isConnected() ||
        RecordingManager::instance().isRecording() ||
        RecordingManager::instance().isPaused()) {
        QMessageBox::warning(
            this, "Load Scene Collection",
            "Stop streaming and recording before loading a scene collection.");
        return;
    }

    const QString path = QFileDialog::getOpenFileName(
        this, "Load Scene Collection", QDir::homePath(),
        "WeaR Scene Collection (*.json)");
    if (path.isEmpty()) return;

    const auto sourceResolver = [](const QString& id) -> ISource* {
        if (id == CaptureManager::instance().info().id) {
            return &CaptureManager::instance();
        }
        return PluginManager::instance().createSource(id);
    };

    const auto filterResolver = [this](const QString& id) -> IFilter* {
        if (id == m_chromaKeyFilter->info().id) {
            return m_chromaKeyFilter.get();
        }
        if (id == m_gaussianBlurFilter->info().id) {
            return m_gaussianBlurFilter.get();
        }
        if (id == m_colorCorrectionFilter->info().id) {
            return m_colorCorrectionFilter.get();
        }
        return PluginManager::instance().createFilter(id);
    };

    QString error;
    if (!ProjectPersistence::loadSceneCollection(
            path,
            SceneManager::instance(),
            sourceResolver,
            filterResolver,
            &error)) {
        QMessageBox::warning(this, "Load Scene Collection", error);
        return;
    }

    refreshScenesList();
    refreshSourcesList();
    m_statusLabel->setText(
        QString("Scene collection loaded: %1").arg(path));
}

void MainWindow::onPreviewFrame(const QImage& frame) {
    m_previewWidget->updateFrame(frame);
}

void MainWindow::updateRecordingState() {
    const RecordingState state = RecordingManager::instance().state();

    switch (state) {
        case RecordingState::Stopped:
            m_recordBtn->setText("Start Recording");
            m_recordBtn->setObjectName("startRecordBtn");
            m_recordBtn->setEnabled(true);
            m_pauseRecordBtn->setText("Pause");
            m_pauseRecordBtn->setEnabled(false);
            break;

        case RecordingState::Recording:
            m_recordBtn->setText("Stop Recording");
            m_recordBtn->setObjectName("stopRecordBtn");
            m_recordBtn->setEnabled(true);
            m_pauseRecordBtn->setText("Pause");
            m_pauseRecordBtn->setEnabled(true);
            break;

        case RecordingState::Paused:
            m_recordBtn->setText("Stop Recording");
            m_recordBtn->setObjectName("stopRecordBtn");
            m_recordBtn->setEnabled(true);
            m_pauseRecordBtn->setText("Resume");
            m_pauseRecordBtn->setEnabled(true);
            break;

        case RecordingState::Error:
            m_recordBtn->setText("Start Recording");
            m_recordBtn->setObjectName("startRecordBtn");
            m_recordBtn->setEnabled(true);
            m_pauseRecordBtn->setText("Pause");
            m_pauseRecordBtn->setEnabled(false);
            break;
    }

    m_recordBtn->style()->unpolish(m_recordBtn);
    m_recordBtn->style()->polish(m_recordBtn);
}

void MainWindow::updateStatistics() {
    // Render stats
    RenderStatistics renderStats = SceneManager::instance().statistics();
    m_fpsLabel->setText(QString("FPS: %1").arg(renderStats.currentFps, 0, 'f', 1));
    m_compositingLabel->setText(
        QString("Comp: %1 CPU %2%")
            .arg(renderStats.compositingBackend)
            .arg(renderStats.compositingCpuUsagePercent, 0, 'f', 1));
    
    const RecordingState recordingState = RecordingManager::instance().state();
    if (recordingState == RecordingState::Recording ||
        recordingState == RecordingState::Paused) {
        const qint64 ms = RecordingManager::instance().durationMs();
        const int seconds = static_cast<int>((ms / 1000) % 60);
        const int minutes = static_cast<int>((ms / 60000) % 60);
        const int hours = static_cast<int>(ms / 3600000);
        m_recordDurationLabel->setText(QString("REC %1:%2:%3")
            .arg(hours, 2, 10, QChar('0'))
            .arg(minutes, 2, 10, QChar('0'))
            .arg(seconds, 2, 10, QChar('0')));
    } else {
        m_recordDurationLabel->setText("Recording: 00:00:00");
    }

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
