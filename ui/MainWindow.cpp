#include "MainWindow.h"
#include <QMenuBar>
#include <QToolBar>
#include <QAction>
#include <QDockWidget>
#include <QPushButton>
#include <QWidget>
#include <QVBoxLayout>

MainWindow::MainWindow() {
    setWindowTitle("WeaR Studio");
    resize(1200, 800);

    // Menu Bar
    QMenuBar* menuBar = new QMenuBar(this);
    setMenuBar(menuBar);
    menuBar->addMenu("File");
    menuBar->addMenu("Edit");
    menuBar->addMenu("View");
    menuBar->addMenu("Settings");
    menuBar->addMenu("Help");

    // Toolbar
    QToolBar* toolBar = new QToolBar(this);
    addToolBar(toolBar);
    QAction* startStream = new QAction("Start Streaming", this);
    QAction* startRecord = new QAction("Start Recording", this);
    toolBar->addAction(startStream);
    toolBar->addAction(startRecord);

    // Central Widget
    QWidget* central = new QWidget(this);
    QVBoxLayout* layout = new QVBoxLayout(central);
    layout->addWidget(new QPushButton("Start Streaming"));
    layout->addWidget(new QPushButton("Start Recording"));
    setCentralWidget(central);

    // Example dockable panel
    QDockWidget* dock = new QDockWidget("Sources", this);
    dock->setWidget(new QWidget());
    addDockWidget(Qt::LeftDockWidgetArea, dock);
}
