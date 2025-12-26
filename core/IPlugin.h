#pragma once
// ==============================================================================
// WeaR-studio Plugin Interface
// IPlugin.h - Base interface for all plugins
// ==============================================================================

#include <QString>
#include <QObject>
#include <QtPlugin>

namespace WeaR {

/**
 * @brief Plugin type enumeration
 * Defines the category of functionality a plugin provides
 */
enum class PluginType {
    Source,      ///< Provides video/audio input (capture devices, media files)
    Filter,      ///< Processes video/audio frames (color correction, effects)
    Transition,  ///< Handles scene transitions (fade, wipe, etc.)
    Output,      ///< Handles output destinations (streaming, recording)
    Service,     ///< Background services (analytics, chat integration)
    Unknown      ///< Unspecified plugin type
};

/**
 * @brief Plugin capability flags
 * Bitwise flags indicating what features a plugin supports
 */
enum class PluginCapability : uint32_t {
    None              = 0,
    HasVideo          = 1 << 0,   ///< Plugin provides/processes video
    HasAudio          = 1 << 1,   ///< Plugin provides/processes audio
    HasSettings       = 1 << 2,   ///< Plugin has configurable settings
    HasPreview        = 1 << 3,   ///< Plugin can show preview
    SupportsAsync     = 1 << 4,   ///< Plugin operations are asynchronous
    RequiresGPU       = 1 << 5,   ///< Plugin requires GPU acceleration
    ThreadSafe        = 1 << 6,   ///< Plugin is thread-safe
};

// Enable bitwise operations on PluginCapability
inline PluginCapability operator|(PluginCapability a, PluginCapability b) {
    return static_cast<PluginCapability>(
        static_cast<uint32_t>(a) | static_cast<uint32_t>(b)
    );
}

inline PluginCapability operator&(PluginCapability a, PluginCapability b) {
    return static_cast<PluginCapability>(
        static_cast<uint32_t>(a) & static_cast<uint32_t>(b)
    );
}

inline bool hasCapability(PluginCapability caps, PluginCapability flag) {
    return (static_cast<uint32_t>(caps) & static_cast<uint32_t>(flag)) != 0;
}

/**
 * @brief Plugin metadata structure
 * Contains descriptive information about a plugin
 */
struct PluginInfo {
    QString id;           ///< Unique identifier (e.g., "wear.source.webcam")
    QString name;         ///< Display name (e.g., "Webcam Capture")
    QString description;  ///< Brief description
    QString version = "0.1";
    QString author;       ///< Author name
    QString website;      ///< Support/documentation URL
    PluginType type;      ///< Plugin category
    PluginCapability capabilities;  ///< Feature flags
};

/**
 * @brief Base plugin interface
 * 
 * All WeaR-studio plugins must implement this interface.
 * The plugin lifecycle is:
 * 1. Plugin is loaded via PluginManager
 * 2. initialize() is called
 * 3. Plugin is used by the application
 * 4. shutdown() is called before unloading
 * 5. Plugin is unloaded
 * 
 * @note Plugins must be thread-safe if they set the ThreadSafe capability
 */
class IPlugin {
public:
    virtual ~IPlugin() = default;

    /**
     * @brief Get plugin metadata
     * @return PluginInfo structure with plugin details
     */
    [[nodiscard]] virtual PluginInfo info() const = 0;

    /**
     * @brief Get plugin display name
     * @return Human-readable plugin name
     */
    [[nodiscard]] virtual QString name() const = 0;

    /**
     * @brief Get plugin version string
     * @return Version in semantic versioning format
     */
    [[nodiscard]] virtual QString version() const = 0;

    /**
     * @brief Get plugin type
     * @return PluginType enumeration value
     */
    [[nodiscard]] virtual PluginType type() const = 0;

    /**
     * @brief Get plugin capabilities
     * @return Combined capability flags
     */
    [[nodiscard]] virtual PluginCapability capabilities() const = 0;

    /**
     * @brief Initialize the plugin
     * 
     * Called once when the plugin is loaded. Perform any setup,
     * resource allocation, or device enumeration here.
     * 
     * @return true if initialization succeeded, false otherwise
     */
    virtual bool initialize() = 0;

    /**
     * @brief Shutdown the plugin
     * 
     * Called before the plugin is unloaded. Release all resources,
     * stop any threads, and perform cleanup.
     */
    virtual void shutdown() = 0;

    /**
     * @brief Check if plugin is currently active
     * @return true if the plugin is initialized and running
     */
    [[nodiscard]] virtual bool isActive() const = 0;

    /**
     * @brief Get the settings widget for this plugin
     * @return QWidget pointer for settings UI, nullptr if none
     */
    [[nodiscard]] virtual QWidget* settingsWidget() { return nullptr; }

    /**
     * @brief Get last error message
     * @return Error description string, empty if no error
     */
    [[nodiscard]] virtual QString lastError() const { return QString(); }
};

} // namespace WeaR

// Qt Plugin interface declaration
#define WEAR_PLUGIN_IID "com.wear-studio.plugin/1.0"
Q_DECLARE_INTERFACE(WeaR::IPlugin, WEAR_PLUGIN_IID)
