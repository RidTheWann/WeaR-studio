// ==============================================================================
// WeaR-studio SceneItem Implementation
// ==============================================================================

#include "SceneItem.h"

#include <QPainter>
#include <QDebug>

namespace WeaR {

SceneItem::SceneItem(ISource* source, QObject* parent)
    : QObject(parent)
    , m_id(QUuid::createUuid())
    , m_name(QStringLiteral("New Item"))
    , m_source(source)
{
    if (m_source) {
        // Set default size from source resolution
        QSize srcSize = m_source->nativeResolution();
        if (srcSize.isValid()) {
            m_transform.size = QSizeF(srcSize);
        }
    }
}

SceneItem::SceneItem(const QString& name, ISource* source, QObject* parent)
    : QObject(parent)
    , m_id(QUuid::createUuid())
    , m_name(name)
    , m_source(source)
{
    if (m_source) {
        QSize srcSize = m_source->nativeResolution();
        if (srcSize.isValid()) {
            m_transform.size = QSizeF(srcSize);
        }
    }
}

SceneItem::~SceneItem() {
    if (m_ownsSource && m_source) {
        delete m_source;
    }
}

void SceneItem::setName(const QString& name) {
    if (m_name != name) {
        m_name = name;
        emit nameChanged(name);
    }
}

void SceneItem::setSource(ISource* source) {
    if (m_source != source) {
        if (m_ownsSource && m_source) {
            delete m_source;
        }
        m_source = source;
        m_ownsSource = true;
        
        // Update size from new source
        if (m_source) {
            QSize srcSize = m_source->nativeResolution();
            if (srcSize.isValid() && m_transform.size.isEmpty()) {
                m_transform.size = QSizeF(srcSize);
            }
        }
        
        emit sourceChanged();
    }
}

void SceneItem::setTransform(const ItemTransform& transform) {
    m_transform = transform;
    emit transformChanged();
}

void SceneItem::setPosition(const QPointF& pos) {
    if (m_transform.position != pos) {
        m_transform.position = pos;
        emit transformChanged();
    }
}

void SceneItem::setSize(const QSizeF& size) {
    if (m_transform.size != size) {
        m_transform.size = size;
        emit transformChanged();
    }
}

void SceneItem::setRotation(double degrees) {
    if (m_transform.rotation != degrees) {
        m_transform.rotation = degrees;
        emit transformChanged();
    }
}

void SceneItem::setOpacity(double opacity) {
    opacity = qBound(0.0, opacity, 1.0);
    if (m_transform.opacity != opacity) {
        m_transform.opacity = opacity;
        emit transformChanged();
    }
}

void SceneItem::setVisible(bool visible) {
    if (m_visible != visible) {
        m_visible = visible;
        emit visibilityChanged(visible);
    }
}

void SceneItem::setLocked(bool locked) {
    if (m_locked != locked) {
        m_locked = locked;
        emit lockedChanged(locked);
    }
}

QImage SceneItem::currentFrame() const {
    if (!m_source || !m_visible) {
        return QImage();
    }
    
    // Get frame from source
    VideoFrame frame = m_source->captureVideoFrame();
    
    if (frame.isHardwareFrame) {
        // For hardware frames, we'd need to convert - not implemented yet
        // Return empty for now, or use the software fallback
        return frame.softwareFrame;
    }
    
    return frame.softwareFrame;
}

void SceneItem::render(QPainter* painter) const {
    if (!painter || !m_visible) return;
    
    QImage frame = currentFrame();
    if (frame.isNull()) return;
    
    painter->save();
    
    // Apply transform
    QTransform t = m_transform.toQTransform();
    painter->setTransform(t, true);
    
    // Apply opacity
    if (m_transform.opacity < 1.0) {
        painter->setOpacity(m_transform.opacity);
    }
    
    // Apply blend mode (simplified - full implementation would use composition modes)
    switch (m_blendMode) {
        case BlendMode::Multiply:
            painter->setCompositionMode(QPainter::CompositionMode_Multiply);
            break;
        case BlendMode::Screen:
            painter->setCompositionMode(QPainter::CompositionMode_Screen);
            break;
        case BlendMode::Overlay:
            painter->setCompositionMode(QPainter::CompositionMode_Overlay);
            break;
        case BlendMode::Additive:
            painter->setCompositionMode(QPainter::CompositionMode_Plus);
            break;
        default:
            painter->setCompositionMode(QPainter::CompositionMode_SourceOver);
            break;
    }
    
    // Scale frame to target size if different
    QRectF targetRect(0, 0, m_transform.size.width(), m_transform.size.height());
    
    if (frame.size() != m_transform.size.toSize()) {
        // Draw scaled
        painter->drawImage(targetRect, frame);
    } else {
        // Draw at native size
        painter->drawImage(0, 0, frame);
    }
    
    painter->restore();
}

} // namespace WeaR
