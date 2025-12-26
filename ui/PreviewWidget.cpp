// ==============================================================================
// WeaR-studio Preview Widget Implementation
// ==============================================================================

#include "PreviewWidget.h"

#include <QPainter>
#include <QResizeEvent>

namespace WeaR {

PreviewWidget::PreviewWidget(QWidget* parent)
    : QWidget(parent)
{
    // Optimize for video rendering
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAttribute(Qt::WA_NoSystemBackground);
    
    // Set minimum size
    setMinimumSize(320, 180);
    
    // Set widget background
    setAutoFillBackground(false);
}

PreviewWidget::~PreviewWidget() = default;

double PreviewWidget::aspectRatio() const {
    QMutexLocker lock(&m_mutex);
    if (m_frame.isNull() || m_frame.height() == 0) {
        return 16.0 / 9.0;  // Default 16:9
    }
    return static_cast<double>(m_frame.width()) / m_frame.height();
}

QImage PreviewWidget::currentFrame() const {
    QMutexLocker lock(&m_mutex);
    return m_frame;
}

QSize PreviewWidget::minimumSizeHint() const {
    return QSize(320, 180);
}

QSize PreviewWidget::sizeHint() const {
    return QSize(640, 360);
}

void PreviewWidget::updateFrame(const QImage& frame) {
    {
        QMutexLocker lock(&m_mutex);
        m_frame = frame;
        m_needsScaling = true;
    }
    
    // Request repaint on the UI thread
    update();
}

void PreviewWidget::clear() {
    {
        QMutexLocker lock(&m_mutex);
        m_frame = QImage();
        m_scaledFrame = QImage();
    }
    update();
}

void PreviewWidget::paintEvent(QPaintEvent* /*event*/) {
    QPainter painter(this);
    
    // Fill background with black
    painter.fillRect(rect(), Qt::black);
    
    QMutexLocker lock(&m_mutex);
    
    if (m_frame.isNull()) {
        // Draw placeholder text
        painter.setPen(QColor(100, 100, 100));
        QFont font("Segoe UI", 14);
        painter.setFont(font);
        painter.drawText(rect(), Qt::AlignCenter, "No Preview");
        return;
    }
    
    // Calculate target rect if needed
    if (m_needsScaling) {
        recalculateTargetRect();
        m_needsScaling = false;
    }
    
    // Draw the frame
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.drawImage(m_targetRect, m_frame);
}

void PreviewWidget::resizeEvent(QResizeEvent* /*event*/) {
    m_needsScaling = true;
}

void PreviewWidget::recalculateTargetRect() {
    if (m_frame.isNull()) {
        m_targetRect = rect();
        return;
    }
    
    if (m_keepAspectRatio) {
        // Calculate centered rect maintaining aspect ratio
        double frameAspect = static_cast<double>(m_frame.width()) / m_frame.height();
        double widgetAspect = static_cast<double>(width()) / height();
        
        int targetWidth, targetHeight;
        
        if (widgetAspect > frameAspect) {
            // Widget is wider - fit height
            targetHeight = height();
            targetWidth = static_cast<int>(targetHeight * frameAspect);
        } else {
            // Widget is taller - fit width
            targetWidth = width();
            targetHeight = static_cast<int>(targetWidth / frameAspect);
        }
        
        int x = (width() - targetWidth) / 2;
        int y = (height() - targetHeight) / 2;
        
        m_targetRect = QRect(x, y, targetWidth, targetHeight);
    } else {
        // Stretch to fill
        m_targetRect = rect();
    }
}

} // namespace WeaR
