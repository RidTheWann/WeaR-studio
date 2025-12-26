#pragma once
// ==============================================================================
// WeaR-studio PluginManager
// Dynamic plugin loading system using Qt Plugin Loader
// ==============================================================================

#include "IPlugin.h"
#include "ISource.h"
#include "IFilter.h"

#include <QObject>
#include <QMutex>
#include <QDir>
#include <QPluginLoader>
#include <QMap>
#include <QList>

#include <memory>
#include <functional>

namespace WeaR {

/**
 * @brief Plugin metadata for registration
 */
struct PluginEntry {
    QString id;                     ///< Unique plugin identifier
    QString name;                   ///< Display name
    QString path;                   ///< DLL path
    PluginType type;                ///< Plugin type
    PluginCapability capabilities;  ///< Plugin capabilities
    QPluginLoader* loader = nullptr; ///< Plugin loader instance
    IPlugin* instance = nullptr;    ///< Plugin instance (singleton)
    bool isLoaded = false;          ///< Whether plugin is currently loaded
    bool supportsFactory = false;   ///< Whether plugin can create multiple instances
};

/**
 * @brief Dynamic plugin loading and management system
 * 
 * PluginManager handles:
 * - Discovering plugins in the ./plugins directory
 * - Loading/unloading plugins dynamically
 * - Categorizing plugins by type (Source, Filter, etc.)
 * - Creating plugin instances via factory pattern
 * 
 * Thread-safe Singleton pattern for application-wide access.
 * 
 * Usage:
 * @code
 *   auto& plugins = PluginManager::instance();
 *   plugins.discoverPlugins();
 *   plugins.loadAllPlugins();
 *   
 *   // Get available sources
 *   auto sources = plugins.availableSources();
 *   
 *   // Create a source instance
 *   ISource* colorSource = plugins.createSource("wear.source.color");
 * @endcode
 */
class PluginManager : public QObject {
    Q_OBJECT

public:
    /**
     * @brief Get singleton instance
     * @return Reference to the PluginManager instance
     */
    static PluginManager& instance();

    // Prevent copying
    PluginManager(const PluginManager&) = delete;
    PluginManager& operator=(const PluginManager&) = delete;

    ~PluginManager() override;

    // =========================================================================
    // Plugin Discovery
    // =========================================================================
    
    /**
     * @brief Set the plugins directory
     * @param path Path to plugins directory
     */
    void setPluginsDirectory(const QString& path);
    
    /**
     * @brief Get the plugins directory
     */
    [[nodiscard]] QString pluginsDirectory() const { return m_pluginsDir; }
    
    /**
     * @brief Discover plugins in the plugins directory
     * @return Number of plugins discovered
     */
    int discoverPlugins();
    
    /**
     * @brief Get list of discovered plugin entries
     */
    [[nodiscard]] QList<PluginEntry> discoveredPlugins() const;
    
    /**
     * @brief Check if a plugin is discovered
     */
    [[nodiscard]] bool hasPlugin(const QString& id) const;

    // =========================================================================
    // Plugin Loading
    // =========================================================================
    
    /**
     * @brief Load a specific plugin by ID
     * @param id Plugin identifier
     * @return true if loaded successfully
     */
    bool loadPlugin(const QString& id);
    
    /**
     * @brief Load a plugin from a file path
     * @param path Path to the plugin DLL
     * @return true if loaded successfully
     */
    bool loadPluginFromPath(const QString& path);
    
    /**
     * @brief Load all discovered plugins
     * @return Number of plugins loaded
     */
    int loadAllPlugins();
    
    /**
     * @brief Unload a specific plugin
     * @param id Plugin identifier
     * @return true if unloaded
     */
    bool unloadPlugin(const QString& id);
    
    /**
     * @brief Unload all plugins
     */
    void unloadAllPlugins();
    
    /**
     * @brief Check if a plugin is loaded
     */
    [[nodiscard]] bool isPluginLoaded(const QString& id) const;

    // =========================================================================
    // Plugin Access by Type
    // =========================================================================
    
    /**
     * @brief Get all loaded source plugins
     */
    [[nodiscard]] QList<ISource*> availableSources() const;
    
    /**
     * @brief Get all loaded filter plugins
     */
    [[nodiscard]] QList<IFilter*> availableFilters() const;
    
    /**
     * @brief Get all loaded plugins
     */
    [[nodiscard]] QList<IPlugin*> allPlugins() const;
    
    /**
     * @brief Get plugin by ID
     * @param id Plugin identifier
     * @return Plugin instance, nullptr if not found/loaded
     */
    [[nodiscard]] IPlugin* plugin(const QString& id) const;
    
    /**
     * @brief Get source plugin by ID
     */
    [[nodiscard]] ISource* source(const QString& id) const;
    
    /**
     * @brief Get filter plugin by ID
     */
    [[nodiscard]] IFilter* filter(const QString& id) const;

    // =========================================================================
    // Plugin Instance Creation (Factory)
    // =========================================================================
    
    /**
     * @brief Create a new source instance
     * 
     * Returns the singleton instance (plugins are singletons).
     * For multiple instances, each source maintains internal state.
     * 
     * @param id Source plugin identifier
     * @return Source instance, nullptr if failed
     */
    [[nodiscard]] ISource* createSource(const QString& id);
    
    /**
     * @brief Create a new filter instance
     * @param id Filter plugin identifier
     * @return Filter instance, nullptr if failed
     */
    [[nodiscard]] IFilter* createFilter(const QString& id);

    // =========================================================================
    // Plugin Information
    // =========================================================================
    
    /**
     * @brief Get number of loaded plugins
     */
    [[nodiscard]] int loadedPluginCount() const;
    
    /**
     * @brief Get plugin info by ID
     */
    [[nodiscard]] PluginInfo pluginInfo(const QString& id) const;

signals:
    /**
     * @brief Emitted when a plugin is discovered
     */
    void pluginDiscovered(const QString& id, const QString& name);
    
    /**
     * @brief Emitted when a plugin is loaded
     */
    void pluginLoaded(const QString& id);
    
    /**
     * @brief Emitted when a plugin is unloaded
     */
    void pluginUnloaded(const QString& id);
    
    /**
     * @brief Emitted when plugin loading fails
     */
    void pluginLoadError(const QString& id, const QString& error);

private:
    // Private constructor for singleton
    explicit PluginManager(QObject* parent = nullptr);
    
    // Internal helpers
    bool registerPlugin(QPluginLoader* loader, const QString& path);
    void categorizePlugin(PluginEntry& entry);
    
    // Plugin storage
    QMap<QString, PluginEntry> m_plugins;
    
    // Categorized plugin lists (for fast access)
    QList<ISource*> m_sources;
    QList<IFilter*> m_filters;
    
    // Settings
    QString m_pluginsDir;
    
    // Thread safety
    mutable QMutex m_mutex;
};

} // namespace WeaR
