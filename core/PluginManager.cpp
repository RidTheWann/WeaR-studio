// ==============================================================================
// WeaR-studio PluginManager Implementation
// Dynamic plugin loading system using Qt Plugin Loader
// ==============================================================================

#include "PluginManager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QDebug>

namespace WeaR {

// ==============================================================================
// PluginManager Singleton
// ==============================================================================
PluginManager& PluginManager::instance() {
    static PluginManager instance;
    return instance;
}

PluginManager::PluginManager(QObject* parent)
    : QObject(parent)
{
    // Default plugins directory relative to executable
    QString appDir = QCoreApplication::applicationDirPath();
    m_pluginsDir = appDir + "/plugins";
    
    qDebug() << "PluginManager initialized, plugins directory:" << m_pluginsDir;
}

PluginManager::~PluginManager() {
    unloadAllPlugins();
}

// ==============================================================================
// Plugin Discovery
// ==============================================================================
void PluginManager::setPluginsDirectory(const QString& path) {
    QMutexLocker lock(&m_mutex);
    m_pluginsDir = path;
}

int PluginManager::discoverPlugins() {
    QMutexLocker lock(&m_mutex);
    
    QDir pluginsDir(m_pluginsDir);
    if (!pluginsDir.exists()) {
        qWarning() << "Plugins directory does not exist:" << m_pluginsDir;
        return 0;
    }
    
    int discovered = 0;
    
    // Get list of plugin files
#ifdef Q_OS_WIN
    QStringList filters = {"*.dll"};
#else
    QStringList filters = {"*.so", "*.dylib"};
#endif
    
    pluginsDir.setNameFilters(filters);
    QFileInfoList files = pluginsDir.entryInfoList(QDir::Files);
    
    qDebug() << "Scanning for plugins in:" << m_pluginsDir;
    qDebug() << "Found" << files.size() << "potential plugin files";
    
    for (const QFileInfo& fileInfo : files) {
        QString path = fileInfo.absoluteFilePath();
        
        // Check if already discovered
        bool alreadyDiscovered = false;
        for (const auto& entry : m_plugins) {
            if (entry.path == path) {
                alreadyDiscovered = true;
                break;
            }
        }
        
        if (alreadyDiscovered) continue;
        
        // Try to load and register the plugin
        QPluginLoader* loader = new QPluginLoader(path);
        
        // Get metadata without fully loading
        QJsonObject metadata = loader->metaData();
        if (metadata.isEmpty()) {
            qDebug() << "Not a valid Qt plugin:" << path;
            delete loader;
            continue;
        }
        
        // Register the plugin
        if (registerPlugin(loader, path)) {
            discovered++;
        } else {
            delete loader;
        }
    }
    
    qDebug() << "Discovered" << discovered << "plugins";
    return discovered;
}

bool PluginManager::registerPlugin(QPluginLoader* loader, const QString& path) {
    // Load the plugin to get its interface
    QObject* instance = loader->instance();
    if (!instance) {
        qWarning() << "Failed to load plugin:" << path 
                   << "-" << loader->errorString();
        return false;
    }
    
    // Try to cast to IPlugin
    IPlugin* plugin = qobject_cast<IPlugin*>(instance);
    if (!plugin) {
        qWarning() << "Plugin does not implement IPlugin:" << path;
        loader->unload();
        return false;
    }
    
    // Get plugin info
    PluginInfo info = plugin->info();
    
    if (info.id.isEmpty()) {
        qWarning() << "Plugin has no ID:" << path;
        loader->unload();
        return false;
    }
    
    // Check for duplicate
    if (m_plugins.contains(info.id)) {
        qWarning() << "Duplicate plugin ID:" << info.id;
        loader->unload();
        return false;
    }
    
    // Create plugin entry
    PluginEntry entry;
    entry.id = info.id;
    entry.name = info.name;
    entry.path = path;
    entry.type = info.type;
    entry.capabilities = info.capabilities;
    entry.loader = loader;
    entry.instance = plugin;
    entry.isLoaded = true;
    
    // Categorize and store
    categorizePlugin(entry);
    m_plugins.insert(info.id, entry);
    
    // Initialize the plugin
    if (!plugin->initialize()) {
        qWarning() << "Plugin initialization failed:" << info.id;
        // Still keep it registered but mark as not loaded
        entry.isLoaded = false;
    }
    
    qDebug() << "Registered plugin:" << info.id << "(" << info.name << ")";
    emit pluginDiscovered(info.id, info.name);
    emit pluginLoaded(info.id);
    
    return true;
}

void PluginManager::categorizePlugin(PluginEntry& entry) {
    if (!entry.instance) return;
    
    switch (entry.type) {
        case PluginType::Source: {
            ISource* source = dynamic_cast<ISource*>(entry.instance);
            if (source) {
                m_sources.append(source);
            }
            break;
        }
        case PluginType::Filter: {
            IFilter* filter = dynamic_cast<IFilter*>(entry.instance);
            if (filter) {
                m_filters.append(filter);
            }
            break;
        }
        default:
            break;
    }
}

QList<PluginEntry> PluginManager::discoveredPlugins() const {
    QMutexLocker lock(&m_mutex);
    return m_plugins.values();
}

bool PluginManager::hasPlugin(const QString& id) const {
    QMutexLocker lock(&m_mutex);
    return m_plugins.contains(id);
}

// ==============================================================================
// Plugin Loading
// ==============================================================================
bool PluginManager::loadPlugin(const QString& id) {
    QMutexLocker lock(&m_mutex);
    
    if (!m_plugins.contains(id)) {
        qWarning() << "Plugin not found:" << id;
        return false;
    }
    
    PluginEntry& entry = m_plugins[id];
    
    if (entry.isLoaded) {
        return true;  // Already loaded
    }
    
    if (!entry.loader) {
        return false;
    }
    
    // Load the plugin
    if (!entry.loader->load()) {
        QString error = entry.loader->errorString();
        qWarning() << "Failed to load plugin:" << id << "-" << error;
        emit pluginLoadError(id, error);
        return false;
    }
    
    QObject* instance = entry.loader->instance();
    if (!instance) {
        emit pluginLoadError(id, "Failed to get plugin instance");
        return false;
    }
    
    entry.instance = qobject_cast<IPlugin*>(instance);
    
    if (entry.instance) {
        entry.instance->initialize();
        entry.isLoaded = true;
        categorizePlugin(entry);
        emit pluginLoaded(id);
        return true;
    }
    
    return false;
}

bool PluginManager::loadPluginFromPath(const QString& path) {
    QMutexLocker lock(&m_mutex);
    
    QPluginLoader* loader = new QPluginLoader(path);
    
    if (registerPlugin(loader, path)) {
        return true;
    }
    
    delete loader;
    return false;
}

int PluginManager::loadAllPlugins() {
    // First discover if not done
    if (m_plugins.isEmpty()) {
        discoverPlugins();
    }
    
    QMutexLocker lock(&m_mutex);
    
    int loaded = 0;
    for (const QString& id : m_plugins.keys()) {
        if (!m_plugins[id].isLoaded) {
            lock.unlock();
            if (loadPlugin(id)) {
                loaded++;
            }
            lock.relock();
        } else {
            loaded++;
        }
    }
    
    return loaded;
}

bool PluginManager::unloadPlugin(const QString& id) {
    QMutexLocker lock(&m_mutex);
    
    if (!m_plugins.contains(id)) {
        return false;
    }
    
    PluginEntry& entry = m_plugins[id];
    
    if (!entry.isLoaded) {
        return true;
    }
    
    // Remove from categorized lists
    if (entry.type == PluginType::Source) {
        ISource* source = dynamic_cast<ISource*>(entry.instance);
        if (source) {
            m_sources.removeOne(source);
        }
    } else if (entry.type == PluginType::Filter) {
        IFilter* filter = dynamic_cast<IFilter*>(entry.instance);
        if (filter) {
            m_filters.removeOne(filter);
        }
    }
    
    // Shutdown plugin
    if (entry.instance) {
        entry.instance->shutdown();
    }
    
    // Unload
    if (entry.loader) {
        entry.loader->unload();
    }
    
    entry.instance = nullptr;
    entry.isLoaded = false;
    
    emit pluginUnloaded(id);
    
    return true;
}

void PluginManager::unloadAllPlugins() {
    QMutexLocker lock(&m_mutex);
    
    for (const QString& id : m_plugins.keys()) {
        lock.unlock();
        unloadPlugin(id);
        lock.relock();
    }
    
    m_sources.clear();
    m_filters.clear();
}

bool PluginManager::isPluginLoaded(const QString& id) const {
    QMutexLocker lock(&m_mutex);
    
    if (!m_plugins.contains(id)) {
        return false;
    }
    
    return m_plugins[id].isLoaded;
}

// ==============================================================================
// Plugin Access by Type
// ==============================================================================
QList<ISource*> PluginManager::availableSources() const {
    QMutexLocker lock(&m_mutex);
    return m_sources;
}

QList<IFilter*> PluginManager::availableFilters() const {
    QMutexLocker lock(&m_mutex);
    return m_filters;
}

QList<IPlugin*> PluginManager::allPlugins() const {
    QMutexLocker lock(&m_mutex);
    
    QList<IPlugin*> plugins;
    for (const auto& entry : m_plugins) {
        if (entry.instance && entry.isLoaded) {
            plugins.append(entry.instance);
        }
    }
    
    return plugins;
}

IPlugin* PluginManager::plugin(const QString& id) const {
    QMutexLocker lock(&m_mutex);
    
    if (m_plugins.contains(id) && m_plugins[id].isLoaded) {
        return m_plugins[id].instance;
    }
    
    return nullptr;
}

ISource* PluginManager::source(const QString& id) const {
    IPlugin* p = plugin(id);
    if (p && p->type() == PluginType::Source) {
        return dynamic_cast<ISource*>(p);
    }
    return nullptr;
}

IFilter* PluginManager::filter(const QString& id) const {
    IPlugin* p = plugin(id);
    if (p && p->type() == PluginType::Filter) {
        return dynamic_cast<IFilter*>(p);
    }
    return nullptr;
}

// ==============================================================================
// Plugin Instance Creation (Factory)
// ==============================================================================
ISource* PluginManager::createSource(const QString& id) {
    QMutexLocker lock(&m_mutex);
    
    if (!m_plugins.contains(id)) {
        qWarning() << "Source plugin not found:" << id;
        return nullptr;
    }
    
    const PluginEntry& entry = m_plugins[id];
    
    if (!entry.isLoaded || !entry.instance) {
        qWarning() << "Source plugin not loaded:" << id;
        return nullptr;
    }
    
    if (entry.type != PluginType::Source) {
        qWarning() << "Plugin is not a source:" << id;
        return nullptr;
    }
    
    // Return the singleton instance
    // Note: For multiple instances, the scene can use the same source
    // with different SceneItems
    return dynamic_cast<ISource*>(entry.instance);
}

IFilter* PluginManager::createFilter(const QString& id) {
    QMutexLocker lock(&m_mutex);
    
    if (!m_plugins.contains(id)) {
        qWarning() << "Filter plugin not found:" << id;
        return nullptr;
    }
    
    const PluginEntry& entry = m_plugins[id];
    
    if (!entry.isLoaded || !entry.instance) {
        qWarning() << "Filter plugin not loaded:" << id;
        return nullptr;
    }
    
    if (entry.type != PluginType::Filter) {
        qWarning() << "Plugin is not a filter:" << id;
        return nullptr;
    }
    
    // Return the singleton instance
    return dynamic_cast<IFilter*>(entry.instance);
}

// ==============================================================================
// Plugin Information
// ==============================================================================
int PluginManager::loadedPluginCount() const {
    QMutexLocker lock(&m_mutex);
    
    int count = 0;
    for (const auto& entry : m_plugins) {
        if (entry.isLoaded) count++;
    }
    
    return count;
}

PluginInfo PluginManager::pluginInfo(const QString& id) const {
    QMutexLocker lock(&m_mutex);
    
    if (m_plugins.contains(id) && m_plugins[id].instance) {
        return m_plugins[id].instance->info();
    }
    
    return PluginInfo();
}

} // namespace WeaR
