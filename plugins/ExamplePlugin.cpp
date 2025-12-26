// ==============================================================================
// WeaR-studio Example Plugin - Color Source Implementation
// ==============================================================================

#include "ExamplePlugin.h"

#include <QPainter>
#include <QDateTime>
#include <QVBoxLayout>
#include <QLabel>
#include <QColorDialog>
#include <QPushButton>
#include <QCheckBox>

namespace WeaR {

ColorSourcePlugin::ColorSourcePlugin(QObject* parent)
    : QObject(parent)
{
    // Default configuration
    m_config.resolution = QSize(1920, 1080);
    m_config.fps = 60.0;
}

ColorSourcePlugin::~ColorSourcePlugin() {
    shutdown();
}

// ==============================================================================
// IPlugin Interface
// ==============================================================================
PluginInfo ColorSourcePlugin::info() const {
    return PluginInfo{
        .id = QStringLiteral("wear.source.color"),
        .name = QStringLiteral("Color Source"),
        .description = QStringLiteral("Generates solid color or animated color frames"),
        .version = QStringLiteral("0.1"),
        .author = QStringLiteral("WeaR-studio"),
        .website = QStringLiteral("https://github.com/wear-studio"),
        .type = PluginType::Source,
        .capabilities = capabilities()
    };
}

PluginCapability ColorSourcePlugin::capabilities() const {
    return PluginCapability::HasVideo 
         | PluginCapability::HasSettings
         | PluginCapability::HasPreview
         | PluginCapability::ThreadSafe;
}

bool ColorSourcePlugin::initialize() {
    if (m_initialized) return true;
    
    // Pre-generate initial frame
    generateFrame();
    
    m_initialized = true;
    return true;
}

void ColorSourcePlugin::shutdown() {
    stop();
    m_initialized = false;
}

QWidget* ColorSourcePlugin::settingsWidget() {
    // Create a simple settings widget
    QWidget* widget = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout(widget);
    
    // Color picker button
    QPushButton* colorButton = new QPushButton("Choose Color");
    colorButton->setStyleSheet(
        QString("background-color: %1").arg(m_color.name())
    );
    
    QObject::connect(colorButton, &QPushButton::clicked, [this, colorButton]() {
        QColor newColor = QColorDialog::getColor(m_color, nullptr, "Select Color");
        if (newColor.isValid()) {
            setColor(newColor);
            colorButton->setStyleSheet(
                QString("background-color: %1").arg(newColor.name())
            );
        }
    });
    
    // Animated checkbox
    QCheckBox* animatedCheck = new QCheckBox("Animate Color (Rainbow)");
    animatedCheck->setChecked(m_animated);
    
    QObject::connect(animatedCheck, &QCheckBox::toggled, [this](bool checked) {
        setAnimated(checked);
    });
    
    layout->addWidget(new QLabel("Color Source Settings"));
    layout->addWidget(colorButton);
    layout->addWidget(animatedCheck);
    layout->addStretch();
    
    return widget;
}

// ==============================================================================
// ISource Interface
// ==============================================================================
bool ColorSourcePlugin::configure(const SourceConfig& config) {
    m_config = config;
    
    // Regenerate frame with new resolution
    generateFrame();
    
    return true;
}

bool ColorSourcePlugin::start() {
    if (m_running) return true;
    
    if (!m_initialized) {
        if (!initialize()) {
            return false;
        }
    }
    
    m_running = true;
    m_frameNumber = 0;
    
    return true;
}

void ColorSourcePlugin::stop() {
    m_running = false;
}

VideoFrame ColorSourcePlugin::captureVideoFrame() {
    VideoFrame frame;
    
    if (!m_running && !m_initialized) {
        return frame;
    }
    
    // Generate new frame if animated
    if (m_animated) {
        generateFrame();
    }
    
    frame.softwareFrame = m_currentFrame;
    frame.isHardwareFrame = false;
    frame.timestamp = QDateTime::currentMSecsSinceEpoch() * 1000;
    frame.frameNumber = m_frameNumber++;
    
    return frame;
}

QSize ColorSourcePlugin::nativeResolution() const {
    return m_config.resolution;
}

QSize ColorSourcePlugin::outputResolution() const {
    return m_config.resolution;
}

// ==============================================================================
// Color Source Specific API
// ==============================================================================
void ColorSourcePlugin::setColor(const QColor& color) {
    if (m_color != color) {
        m_color = color;
        generateFrame();
    }
}

void ColorSourcePlugin::generateFrame() {
    QSize size = m_config.resolution;
    if (!size.isValid()) {
        size = QSize(1920, 1080);
    }
    
    // Create or resize frame
    if (m_currentFrame.size() != size) {
        m_currentFrame = QImage(size, QImage::Format_ARGB32_Premultiplied);
    }
    
    QColor fillColor = m_color;
    
    // If animated, cycle through hues
    if (m_animated) {
        m_hue += 1.0f;
        if (m_hue >= 360.0f) {
            m_hue = 0.0f;
        }
        fillColor = QColor::fromHslF(m_hue / 360.0f, 0.8f, 0.5f);
    }
    
    // Fill with color
    m_currentFrame.fill(fillColor);
    
    // Optionally draw some visual elements
    QPainter painter(&m_currentFrame);
    painter.setRenderHint(QPainter::Antialiasing);
    
    // Draw a simple pattern/logo
    painter.setPen(Qt::NoPen);
    
    // Center circle
    QColor centerColor = fillColor.lighter(150);
    painter.setBrush(centerColor);
    
    int circleSize = qMin(size.width(), size.height()) / 4;
    QPoint center(size.width() / 2, size.height() / 2);
    painter.drawEllipse(center, circleSize, circleSize);
    
    // Text overlay
    painter.setPen(Qt::white);
    QFont font("Arial", 24, QFont::Bold);
    painter.setFont(font);
    painter.drawText(
        m_currentFrame.rect(),
        Qt::AlignCenter,
        "Color Source"
    );
    
    painter.end();
}

} // namespace WeaR
