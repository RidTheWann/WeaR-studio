#pragma once
// ==============================================================================
// WeaR-studio Example Plugin - Color Source
// Demonstrates implementing ISource plugin interface
// ==============================================================================

#include <IPlugin.h>
#include <ISource.h>

#include <QObject>
#include <QtPlugin>
#include <QColor>
#include <QImage>
#include <QWidget>

namespace WeaR {

/**
 * @brief Color source plugin - generates solid color frames
 * 
 * This is an example plugin demonstrating how to implement the
 * ISource interface for WeaR-studio. It generates frames filled
 * with a solid color.
 */
class ColorSourcePlugin : public QObject, public ISource {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID WEAR_SOURCE_IID FILE "ExamplePlugin.json")
    Q_INTERFACES(WeaR::ISource)

public:
    explicit ColorSourcePlugin(QObject* parent = nullptr);
    ~ColorSourcePlugin() override;

    // =========================================================================
    // IPlugin Interface
    // =========================================================================
    [[nodiscard]] PluginInfo info() const override;
    [[nodiscard]] QString name() const override { return QStringLiteral("Color Source"); }
    [[nodiscard]] QString version() const override { return QStringLiteral("0.1"); }
    [[nodiscard]] PluginType type() const override { return PluginType::Source; }
    [[nodiscard]] PluginCapability capabilities() const override;
    
    bool initialize() override;
    void shutdown() override;
    [[nodiscard]] bool isActive() const override { return m_initialized; }
    
    QWidget* settingsWidget() override;
    [[nodiscard]] QString lastError() const override { return m_lastError; }

    // =========================================================================
    // ISource Interface
    // =========================================================================
    bool configure(const SourceConfig& config) override;
    [[nodiscard]] SourceConfig config() const override { return m_config; }
    
    bool start() override;
    void stop() override;
    [[nodiscard]] bool isRunning() const override { return m_running; }
    
    [[nodiscard]] VideoFrame captureVideoFrame() override;
    
    [[nodiscard]] QSize nativeResolution() const override;
    [[nodiscard]] double nativeFps() const override { return 60.0; }
    [[nodiscard]] QSize outputResolution() const override;
    [[nodiscard]] double outputFps() const override { return m_config.fps; }

    // =========================================================================
    // Color Source Specific API
    // =========================================================================
    
    /**
     * @brief Set the fill color
     */
    void setColor(const QColor& color);
    
    /**
     * @brief Get the current fill color
     */
    [[nodiscard]] QColor color() const { return m_color; }
    
    /**
     * @brief Set whether to animate color
     */
    void setAnimated(bool animated) { m_animated = animated; }
    
    /**
     * @brief Check if color is animated
     */
    [[nodiscard]] bool isAnimated() const { return m_animated; }

private:
    void generateFrame();
    
    bool m_initialized = false;
    bool m_running = false;
    QString m_lastError;
    
    SourceConfig m_config;
    QColor m_color{Qt::red};
    bool m_animated = false;
    
    QImage m_currentFrame;
    int64_t m_frameNumber = 0;
    float m_hue = 0.0f;
};

} // namespace WeaR
