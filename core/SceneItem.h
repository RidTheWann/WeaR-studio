#pragma once
// ==============================================================================
// WeaR-studio SceneItem
// Individual source item with transform properties for scene composition
// ==============================================================================

#include "ISource.h"

#include <QObject>
#include <QString>
#include <QUuid>
#include <QRectF>
#include <QPointF>
#include <QSizeF>
#include <QTransform>
#include <QImage>

#include <memory>

namespace WeaR {

/**
 * @brief Blend mode for scene item compositing
 */
enum class BlendMode {
    Normal,         ///< Standard alpha blending
    Multiply,       ///< Multiply blend
    Screen,         ///< Screen blend
    Overlay,        ///< Overlay blend
    Additive        ///< Additive blend
};

/**
 * @brief Scene item transform properties
 */
struct ItemTransform {
    QPointF position{0, 0};     ///< Position (top-left corner)
    QSizeF size{0, 0};          ///< Display size
    double rotation = 0.0;      ///< Rotation in degrees
    QPointF scale{1.0, 1.0};    ///< Scale factors
    QPointF anchor{0.5, 0.5};   ///< Anchor point (0-1, relative to size)
    double opacity = 1.0;       ///< Opacity (0-1)
    bool flipH = false;         ///< Horizontal flip
    bool flipV = false;         ///< Vertical flip
    
    /**
     * @brief Get the bounding rectangle
     */
    [[nodiscard]] QRectF boundingRect() const {
        return QRectF(position, size);
    }
    
    /**
     * @brief Get Qt transformation matrix
     */
    [[nodiscard]] QTransform toQTransform() const {
        QTransform t;
        
        // Move to position
        t.translate(position.x(), position.y());
        
        // Move to anchor point
        double anchorX = size.width() * anchor.x();
        double anchorY = size.height() * anchor.y();
        t.translate(anchorX, anchorY);
        
        // Apply rotation
        if (rotation != 0.0) {
            t.rotate(rotation);
        }
        
        // Apply scale
        double sx = scale.x() * (flipH ? -1 : 1);
        double sy = scale.y() * (flipV ? -1 : 1);
        if (sx != 1.0 || sy != 1.0) {
            t.scale(sx, sy);
        }
        
        // Move back from anchor point
        t.translate(-anchorX, -anchorY);
        
        return t;
    }
};

/**
 * @brief A compositable item within a scene
 * 
 * SceneItem wraps an ISource and adds transform/rendering properties.
 * It represents a single layer in the scene composition.
 */
class SceneItem : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString name READ name WRITE setName NOTIFY nameChanged)
    Q_PROPERTY(bool visible READ isVisible WRITE setVisible NOTIFY visibilityChanged)
    Q_PROPERTY(bool locked READ isLocked WRITE setLocked NOTIFY lockedChanged)

public:
    /**
     * @brief Create a scene item with a source
     * @param source Source providing the video content
     * @param parent Parent object
     */
    explicit SceneItem(ISource* source = nullptr, QObject* parent = nullptr);
    
    /**
     * @brief Create a scene item with name
     * @param name Item name
     * @param source Source providing the video content
     * @param parent Parent object
     */
    SceneItem(const QString& name, ISource* source, QObject* parent = nullptr);
    
    ~SceneItem() override;

    // =========================================================================
    // Identification
    // =========================================================================
    
    /**
     * @brief Get unique item ID
     */
    [[nodiscard]] QUuid id() const { return m_id; }
    
    /**
     * @brief Get item display name
     */
    [[nodiscard]] QString name() const { return m_name; }
    
    /**
     * @brief Set item display name
     */
    void setName(const QString& name);

    // =========================================================================
    // Source
    // =========================================================================
    
    /**
     * @brief Get the associated source
     */
    [[nodiscard]] ISource* source() const { return m_source; }
    
    /**
     * @brief Set the source
     * @param source New source (takes ownership)
     */
    void setSource(ISource* source);
    
    /**
     * @brief Check if item has a valid source
     */
    [[nodiscard]] bool hasSource() const { return m_source != nullptr; }

    // =========================================================================
    // Transform
    // =========================================================================
    
    /**
     * @brief Get transform properties
     */
    [[nodiscard]] ItemTransform transform() const { return m_transform; }
    
    /**
     * @brief Set transform properties
     */
    void setTransform(const ItemTransform& transform);
    
    /**
     * @brief Get position
     */
    [[nodiscard]] QPointF position() const { return m_transform.position; }
    
    /**
     * @brief Set position
     */
    void setPosition(const QPointF& pos);
    void setPosition(double x, double y) { setPosition(QPointF(x, y)); }
    
    /**
     * @brief Get size
     */
    [[nodiscard]] QSizeF size() const { return m_transform.size; }
    
    /**
     * @brief Set size
     */
    void setSize(const QSizeF& size);
    void setSize(double w, double h) { setSize(QSizeF(w, h)); }
    
    /**
     * @brief Get rotation (degrees)
     */
    [[nodiscard]] double rotation() const { return m_transform.rotation; }
    
    /**
     * @brief Set rotation (degrees)
     */
    void setRotation(double degrees);
    
    /**
     * @brief Get opacity (0-1)
     */
    [[nodiscard]] double opacity() const { return m_transform.opacity; }
    
    /**
     * @brief Set opacity (0-1)
     */
    void setOpacity(double opacity);

    // =========================================================================
    // State
    // =========================================================================
    
    /**
     * @brief Check if item is visible
     */
    [[nodiscard]] bool isVisible() const { return m_visible; }
    
    /**
     * @brief Set visibility
     */
    void setVisible(bool visible);
    
    /**
     * @brief Check if item is locked (cannot be moved/edited)
     */
    [[nodiscard]] bool isLocked() const { return m_locked; }
    
    /**
     * @brief Set locked state
     */
    void setLocked(bool locked);

    // =========================================================================
    // Rendering
    // =========================================================================
    
    /**
     * @brief Get blend mode
     */
    [[nodiscard]] BlendMode blendMode() const { return m_blendMode; }
    
    /**
     * @brief Set blend mode
     */
    void setBlendMode(BlendMode mode) { m_blendMode = mode; }
    
    /**
     * @brief Get the current frame from the source
     * @return Current video frame as QImage
     */
    [[nodiscard]] QImage currentFrame() const;
    
    /**
     * @brief Render this item to a painter
     * @param painter QPainter to render to
     */
    void render(QPainter* painter) const;

signals:
    void nameChanged(const QString& name);
    void transformChanged();
    void visibilityChanged(bool visible);
    void lockedChanged(bool locked);
    void sourceChanged();

private:
    QUuid m_id;
    QString m_name;
    ISource* m_source = nullptr;
    bool m_ownsSource = false;
    
    ItemTransform m_transform;
    BlendMode m_blendMode = BlendMode::Normal;
    
    bool m_visible = true;
    bool m_locked = false;
};

} // namespace WeaR
