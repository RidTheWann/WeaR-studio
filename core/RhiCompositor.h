#pragma once
// ==============================================================================
// WeaR-studio Qt RHI compositor
// Cross-backend GPU composition with QPainter safety fallback
// ==============================================================================

#include <QImage>
#include <QSize>
#include <QString>

#include <memory>

namespace WeaR {

class Scene;

/**
 * @brief Offscreen Qt RHI scene compositor.
 *
 * QRhi is a Qt 6.6+ limited-compatibility API and is used here behind a small
 * adapter so SceneManager can retain the existing QPainter path as a safety
 * net. The compositor can target D3D11, Vulkan, Metal, or OpenGL depending on
 * platform and runtime availability.
 */
class RhiCompositor {
public:
    RhiCompositor();
    ~RhiCompositor();

    RhiCompositor(const RhiCompositor&) = delete;
    RhiCompositor& operator=(const RhiCompositor&) = delete;

    /**
     * @brief Try to initialize the requested backend.
     *
     * WEAR_RHI_BACKEND may be auto, d3d11, vulkan, metal, or opengl.
     */
    bool initialize();

    /**
     * @brief Compose one scene to an RGBA QImage.
     *
     * Returns false when initialization, resource creation, rendering, or
     * device submission fails. The caller should then use QPainter.
     */
    bool compose(const Scene& scene, const QSize& outputSize, QImage& output);

    /**
     * @brief Release all graphics resources and force a fresh initialization.
     */
    void reset();

    [[nodiscard]] bool isInitialized() const;
    [[nodiscard]] QString backendName() const;
    [[nodiscard]] QString lastError() const;

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace WeaR
