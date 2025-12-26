#pragma once
// ==============================================================================
// WeaR-studio Scene
// Container for scene items (layers) for video composition
// ==============================================================================

#include "SceneItem.h"

#include <QObject>
#include <QString>
#include <QUuid>
#include <QList>
#include <QSize>
#include <QImage>
#include <QColor>
#include <QMutex>

#include <memory>

namespace WeaR {

/**
 * @brief A scene containing compositable items
 * 
 * Scene acts as a container for SceneItems (layers). Items are
 * rendered in order, with later items appearing on top.
 */
class Scene : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString name READ name WRITE setName NOTIFY nameChanged)

public:
    /**
     * @brief Create an empty scene
     * @param parent Parent object
     */
    explicit Scene(QObject* parent = nullptr);
    
    /**
     * @brief Create a named scene
     * @param name Scene name
     * @param parent Parent object
     */
    explicit Scene(const QString& name, QObject* parent = nullptr);
    
    ~Scene() override;

    // =========================================================================
    // Identification
    // =========================================================================
    
    /**
     * @brief Get unique scene ID
     */
    [[nodiscard]] QUuid id() const { return m_id; }
    
    /**
     * @brief Get scene display name
     */
    [[nodiscard]] QString name() const { return m_name; }
    
    /**
     * @brief Set scene display name
     */
    void setName(const QString& name);

    // =========================================================================
    // Canvas Settings
    // =========================================================================
    
    /**
     * @brief Get canvas resolution
     */
    [[nodiscard]] QSize resolution() const { return m_resolution; }
    
    /**
     * @brief Set canvas resolution
     */
    void setResolution(const QSize& size);
    void setResolution(int width, int height) { setResolution(QSize(width, height)); }
    
    /**
     * @brief Get background color
     */
    [[nodiscard]] QColor backgroundColor() const { return m_backgroundColor; }
    
    /**
     * @brief Set background color
     */
    void setBackgroundColor(const QColor& color);

    // =========================================================================
    // Item Management
    // =========================================================================
    
    /**
     * @brief Get number of items
     */
    [[nodiscard]] int itemCount() const;
    
    /**
     * @brief Get all items
     */
    [[nodiscard]] QList<SceneItem*> items() const;
    
    /**
     * @brief Get item by index
     * @param index Item index (0 = bottom layer)
     */
    [[nodiscard]] SceneItem* itemAt(int index) const;
    
    /**
     * @brief Get item by ID
     * @param id Item UUID
     */
    [[nodiscard]] SceneItem* itemById(const QUuid& id) const;
    
    /**
     * @brief Get item by name
     * @param name Item name
     */
    [[nodiscard]] SceneItem* itemByName(const QString& name) const;
    
    /**
     * @brief Add an item to the scene
     * @param item Item to add (scene takes ownership)
     * @return Index of added item
     */
    int addItem(SceneItem* item);
    
    /**
     * @brief Create and add a new item with source
     * @param name Item name
     * @param source Source for the item
     * @return Pointer to created item
     */
    SceneItem* addItem(const QString& name, ISource* source);
    
    /**
     * @brief Remove an item from the scene
     * @param item Item to remove
     * @return true if removed
     */
    bool removeItem(SceneItem* item);
    
    /**
     * @brief Remove item by ID
     * @param id Item UUID
     * @return true if removed
     */
    bool removeItem(const QUuid& id);
    
    /**
     * @brief Remove item at index
     * @param index Item index
     * @return true if removed
     */
    bool removeItemAt(int index);
    
    /**
     * @brief Remove all items
     */
    void clear();
    
    /**
     * @brief Move item to a new index (change layer order)
     * @param from Current index
     * @param to Target index
     * @return true if moved
     */
    bool moveItem(int from, int to);
    
    /**
     * @brief Bring item to front (top layer)
     * @param item Item to move
     */
    void bringToFront(SceneItem* item);
    
    /**
     * @brief Send item to back (bottom layer)
     * @param item Item to move
     */
    void sendToBack(SceneItem* item);

    // =========================================================================
    // Rendering
    // =========================================================================
    
    /**
     * @brief Render the scene to an image
     * @return Rendered scene as QImage
     */
    [[nodiscard]] QImage render() const;
    
    /**
     * @brief Render the scene to a painter
     * @param painter Target painter
     */
    void render(QPainter* painter) const;

signals:
    void nameChanged(const QString& name);
    void resolutionChanged(const QSize& size);
    void itemAdded(SceneItem* item);
    void itemRemoved(const QUuid& id);
    void itemsReordered();
    void sceneChanged();

private:
    QUuid m_id;
    QString m_name;
    QSize m_resolution{1920, 1080};
    QColor m_backgroundColor{Qt::black};
    
    QList<SceneItem*> m_items;
    mutable QMutex m_mutex;
};

} // namespace WeaR
