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
    
    // Recording settings and controls
    QGroupBox* recordingGroup = new QGroupBox("Recording");
    QVBoxLayout* recordingLayout = new QVBoxLayout(recordingGroup);

    QHBoxLayout* pathLayout = new QHBoxLayout();
    m_recordPathEdit = new QLineEdit();
    m_recordPathEdit->setPlaceholderText("Output file path");

    const QString moviesDir =
        QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    const QString defaultRecordPath =
        QDir(moviesDir.isEmpty() ? QDir::homePath() : moviesDir)
            .filePath("WeaR-recording.mkv");
    m_recordPathEdit->setText(defaultRecordPath);

    m_recordBrowseBtn = new QPushButton("Browse...");
    m_recordBrowseBtn->setFixedWidth(84);
    pathLayout->addWidget(m_recordPathEdit, 1);
    pathLayout->addWidget(m_recordBrowseBtn);
    recordingLayout->addLayout(pathLayout);

    QHBoxLayout* formatLayout = new QHBoxLayout();
    formatLayout->addWidget(new QLabel("Format:"));
    m_recordFormatCombo = new QComboBox();
    m_recordFormatCombo->addItem("MKV");
    m_recordFormatCombo->addItem("MP4");
    m_recordFormatCombo->addItem("FLV");
    formatLayout->addWidget(m_recordFormatCombo, 1);

    formatLayout->addWidget(new QLabel("Video bitrate:"));
    m_recordBitrateSpin = new QSpinBox();
    m_recordBitrateSpin->setRange(500, 100000);
    m_recordBitrateSpin->setSingleStep(500);
    m_recordBitrateSpin->setValue(12000);
    m_recordBitrateSpin->setSuffix(" kbps");
    formatLayout->addWidget(m_recordBitrateSpin);
    recordingLayout->addLayout(formatLayout);

    QHBoxLayout* recordButtonsLayout = new QHBoxLayout();
    m_recordBtn = new QPushButton("Start Recording");
    m_recordBtn->setObjectName("startRecordBtn");
    m_recordBtn->setMinimumHeight(40);

    m_pauseRecordBtn = new QPushButton("Pause");
    m_pauseRecordBtn->setEnabled(false);
    m_pauseRecordBtn->setMinimumHeight(40);

    m_recordDurationLabel = new QLabel("Recording: 00:00:00");
    m_recordDurationLabel->setMinimumWidth(125);
    m_recordDurationLabel->setAlignment(Qt::AlignCenter);

    recordButtonsLayout->addWidget(m_recordBtn, 2);
    recordButtonsLayout->addWidget(m_pauseRecordBtn, 1);
    recordButtonsLayout->addWidget(m_recordDurationLabel);
    recordingLayout->addLayout(recordButtonsLayout);

    layout->addWidget(recordingGroup);


    // Basic GPU filter controls
    QGroupBox* filterGroup = new QGroupBox("Video Filter");
    QHBoxLayout* filterLayout = new QHBoxLayout(filterGroup);

    m_filterCombo = new QComboBox();
    m_filterCombo->addItem("None");
    m_filterCombo->addItem("Chroma Key");
    m_filterCombo->addItem("Gaussian Blur");
    m_filterCombo->addItem("Color Correction");

    m_applyFilterBtn = new QPushButton("Apply");
    m_applyFilterBtn->setMinimumHeight(30);

    filterLayout->addWidget(m_filterCombo, 1);
    filterLayout->addWidget(m_applyFilterBtn);
    layout->addWidget(filterGroup);

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
    connect(m_recordBtn, &QPushButton::clicked, this, &MainWindow::onRecordClicked);
    connect(m_pauseRecordBtn, &QPushButton::clicked,
            this, &MainWindow::onPauseRecordingClicked);
    connect(m_recordBrowseBtn, &QPushButton::clicked,
            this, &MainWindow::onBrowseRecordingPath);
    connect(m_applyFilterBtn, &QPushButton::clicked,
            this, &MainWindow::onApplyFilter);

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
    AudioMixer::instance().addTrack(m_micAudio);

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

    RecordingSettings settings;
    settings.width = 1920;
    settings.height = 1080;
    settings.fpsNum = 60;
    settings.fpsDen = 1;
    settings.videoBitrate = m_recordBitrateSpin->value();
    settings.maxVideoBitrate = settings.videoBitrate + settings.videoBitrate / 3;
    settings.bufferSize = settings.videoBitrate * 2;
    settings.crf = 18;
    settings.qp = 18;
    settings.encoderType = EncoderType::Auto;
    settings.preset = EncoderPreset::Fast;
    settings.rateControl = RateControlMode::VBR;
    settings.keyframeInterval = 2;
    settings.bFrames = 2;
    settings.audioEnabled = true;
    settings.audioSampleRate = 48000;
    settings.audioChannels = 2;
    settings.audioBitrate = 192;

    switch (m_recordFormatCombo->currentIndex()) {
        case 1:
            settings.format = RecordingFormat::MP4;
            break;
        case 2:
            settings.format = RecordingFormat::FLV;
            break;
        default:
            settings.format = RecordingFormat::MKV;
            break;
    }

    QString path = m_recordPathEdit->text().trimmed();
    if (path.isEmpty()) {
        onBrowseRecordingPath();
        path = m_recordPathEdit->text().trimmed();
    }
    if (path.isEmpty()) {
        return;
    }

    const QString expectedExt =
        settings.format == RecordingFormat::MP4
            ? "mp4"
            : (settings.format == RecordingFormat::FLV ? "flv" : "mkv");

    QFileInfo info(path);
    if (info.suffix().compare(expectedExt, Qt::CaseInsensitive) != 0) {
        const QString directory = info.path();
        const QString base = info.completeBaseName().isEmpty()
            ? "WeaR-recording"
            : info.completeBaseName();
        path = QDir(directory).filePath(base + "." + expectedExt);
        m_recordPathEdit->setText(path);
    }

    if (!recorder.configure(settings)) {
        return;
    }

    if (!recorder.startRecording(path)) {
        return;
    }

    // Only the recording output is enabled. Streaming remains untouched.
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

void MainWindow::onBrowseRecordingPath() {
    const QString selected = QFileDialog::getSaveFileName(
        this,
        "Choose recording output",
        m_recordPathEdit ? m_recordPathEdit->text() : QString(),
        "MKV Video (*.mkv);;MP4 Video (*.mp4);;FLV Video (*.flv);;All Files (*)");

    if (selected.isEmpty()) {
        return;
    }

    m_recordPathEdit->setText(selected);

    const QString suffix = QFileInfo(selected).suffix().toLower();
    if (suffix == "mp4") {
        m_recordFormatCombo->setCurrentIndex(1);
    } else if (suffix == "flv") {
        m_recordFormatCombo->setCurrentIndex(2);
    } else {
        m_recordFormatCombo->setCurrentIndex(0);
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
    settings.audioEnabled = true;
    settings.audioSampleRate = 48000;
    settings.audioChannels = 2;
    settings.audioBitrate = 160;
    
    StreamManager::instance().configure(settings);
    
    // Start encoder first
    if (!EncoderManager::instance().isRunning()) {
        EncoderManager::instance().start();
    }
    
    // Provide codec parameters to stream muxer
    StreamManager::instance().setVideoCodecParameters(EncoderManager::instance().videoCodecParameters());
    StreamManager::instance().setAudioCodecParameters(EncoderManager::instance().audioCodecParameters());

    // Connect encoder to stream
    EncoderManager::instance().setPacketCallback([](const EncodedPacket& pkt) {
        StreamManager::instance().writePacket(pkt.data, pkt.size,
                                              pkt.pts, pkt.dts, pkt.isKeyframe, pkt.isAudio);
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
    StreamManager::instance().stopStream();
    EncoderManager::instance().stop();
    m_statusLabel->setText(
        RecordingManager::instance().isRecording() || RecordingManager::instance().isPaused()
            ? "Recording"
            : "Stopped");
}

void MainWindow::onSettingsClicked() {
    QMessageBox::information(this, "Settings", 
                             "Settings dialog coming soon!\n\n"
                             "Configure output resolution, bitrate, encoder, etc.");
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
