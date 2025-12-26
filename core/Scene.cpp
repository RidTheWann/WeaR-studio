// ==============================================================================
// WeaR-studio Scene Implementation
// ==============================================================================

#include "Scene.h"

#include <QPainter>
#include <QDebug>
#include <algorithm>

namespace WeaR {

Scene::Scene(QObject* parent)
    : QObject(parent)
    , m_id(QUuid::createUuid())
    , m_name(QStringLiteral("Scene"))
{
}

Scene::Scene(const QString& name, QObject* parent)
    : QObject(parent)
    , m_id(QUuid::createUuid())
    , m_name(name)
{
}

Scene::~Scene() {
    clear();
}

void Scene::setName(const QString& name) {
    if (m_name != name) {
        m_name = name;
        emit nameChanged(name);
    }
}

void Scene::setResolution(const QSize& size) {
    if (m_resolution != size) {
        m_resolution = size;
        emit resolutionChanged(size);
        emit sceneChanged();
    }
}

void Scene::setBackgroundColor(const QColor& color) {
    if (m_backgroundColor != color) {
        m_backgroundColor = color;
        emit sceneChanged();
    }
}

int Scene::itemCount() const {
    QMutexLocker lock(&m_mutex);
    return m_items.size();
}

QList<SceneItem*> Scene::items() const {
    QMutexLocker lock(&m_mutex);
    return m_items;
}

SceneItem* Scene::itemAt(int index) const {
    QMutexLocker lock(&m_mutex);
    if (index >= 0 && index < m_items.size()) {
        return m_items.at(index);
    }
    return nullptr;
}

SceneItem* Scene::itemById(const QUuid& id) const {
    QMutexLocker lock(&m_mutex);
    for (SceneItem* item : m_items) {
        if (item->id() == id) {
            return item;
        }
    }
    return nullptr;
}

SceneItem* Scene::itemByName(const QString& name) const {
    QMutexLocker lock(&m_mutex);
    for (SceneItem* item : m_items) {
        if (item->name() == name) {
            return item;
        }
    }
    return nullptr;
}

int Scene::addItem(SceneItem* item) {
    if (!item) return -1;
    
    QMutexLocker lock(&m_mutex);
    
    // Check if already added
    if (m_items.contains(item)) {
        return m_items.indexOf(item);
    }
    
    // Take ownership
    item->setParent(this);
    
    // Connect signals
    connect(item, &SceneItem::transformChanged, this, &Scene::sceneChanged);
    connect(item, &SceneItem::visibilityChanged, this, &Scene::sceneChanged);
    connect(item, &SceneItem::sourceChanged, this, &Scene::sceneChanged);
    
    m_items.append(item);
    int index = m_items.size() - 1;
    
    lock.unlock();
    
    emit itemAdded(item);
    emit sceneChanged();
    
    qDebug() << "Item added to scene:" << item->name() << "at index" << index;
    
    return index;
}

SceneItem* Scene::addItem(const QString& name, ISource* source) {
    SceneItem* item = new SceneItem(name, source, this);
    addItem(item);
    return item;
}

bool Scene::removeItem(SceneItem* item) {
    if (!item) return false;
    
    QMutexLocker lock(&m_mutex);
    
    int index = m_items.indexOf(item);
    if (index < 0) return false;
    
    QUuid id = item->id();
    m_items.removeAt(index);
    
    lock.unlock();
    
    emit itemRemoved(id);
    emit sceneChanged();
    
    item->deleteLater();
    
    qDebug() << "Item removed from scene:" << item->name();
    
    return true;
}

bool Scene::removeItem(const QUuid& id) {
    return removeItem(itemById(id));
}

bool Scene::removeItemAt(int index) {
    return removeItem(itemAt(index));
}

void Scene::clear() {
    QMutexLocker lock(&m_mutex);
    
    for (SceneItem* item : m_items) {
        emit itemRemoved(item->id());
        item->deleteLater();
    }
    m_items.clear();
    
    lock.unlock();
    
    emit sceneChanged();
}

bool Scene::moveItem(int from, int to) {
    QMutexLocker lock(&m_mutex);
    
    if (from < 0 || from >= m_items.size() ||
        to < 0 || to >= m_items.size() ||
        from == to) {
        return false;
    }
    
    m_items.move(from, to);
    
    lock.unlock();
    
    emit itemsReordered();
    emit sceneChanged();
    
    return true;
}

void Scene::bringToFront(SceneItem* item) {
    QMutexLocker lock(&m_mutex);
    
    int index = m_items.indexOf(item);
    if (index >= 0 && index < m_items.size() - 1) {
        m_items.move(index, m_items.size() - 1);
        
        lock.unlock();
        
        emit itemsReordered();
        emit sceneChanged();
    }
}

void Scene::sendToBack(SceneItem* item) {
    QMutexLocker lock(&m_mutex);
    
    int index = m_items.indexOf(item);
    if (index > 0) {
        m_items.move(index, 0);
        
        lock.unlock();
        
        emit itemsReordered();
        emit sceneChanged();
    }
}

QImage Scene::render() const {
    // Create output image with premultiplied alpha for better composition
    QImage output(m_resolution, QImage::Format_ARGB32_Premultiplied);
    output.fill(m_backgroundColor);
    
    QPainter painter(&output);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    
    render(&painter);
    
    painter.end();
    
    return output;
}

void Scene::render(QPainter* painter) const {
    if (!painter) return;
    
    QMutexLocker lock(&m_mutex);
    
    // Render items in order (bottom to top)
    for (const SceneItem* item : m_items) {
        if (item->isVisible()) {
            item->render(painter);
        }
    }
}

} // namespace WeaR
