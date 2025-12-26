#pragma once
// ==============================================================================
// WeaR-studio Preview Widget
// Hardware-accelerated video preview display
// ==============================================================================

#include <QWidget>
#include <QImage>
#include <QMutex>

namespace WeaR {

/**
 * @brief Video preview display widget
 * 
 * Displays video frames from the SceneManager. Optimized for
 * minimal overhead using opaque paint events.
 */
class PreviewWidget : public QWidget {
    Q_OBJECT

public:
    explicit PreviewWidget(QWidget* parent = nullptr);
    ~PreviewWidget() override;

    /**
     * @brief Get the aspect ratio of current content
     */
    [[nodiscard]] double aspectRatio() const;
    
    /**
     * @brief Set whether to maintain aspect ratio
     */
    void setKeepAspectRatio(bool keep) { m_keepAspectRatio = keep; update(); }
    
    /**
     * @brief Get aspect ratio setting
     */
    [[nodiscard]] bool keepAspectRatio() const { return m_keepAspectRatio; }
    
    /**
     * @brief Get the current frame
     */
    [[nodiscard]] QImage currentFrame() const;
    
    /**
     * @brief Get recommended minimum size
     */
    [[nodiscard]] QSize minimumSizeHint() const override;
    
    /**
     * @brief Get recommended size
     */
    [[nodiscard]] QSize sizeHint() const override;

public slots:
    /**
     * @brief Update the displayed frame
     * @param frame New frame to display
     */
    void updateFrame(const QImage& frame);
    
    /**
     * @brief Clear the display (show black)
     */
    void clear();

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    QImage m_frame;
    QImage m_scaledFrame;
    QRect m_targetRect;
    mutable QMutex m_mutex;
    
    bool m_keepAspectRatio = true;
    bool m_needsScaling = true;
    
    void recalculateTargetRect();
};

} // namespace WeaR
