// ==============================================================================
// WeaR-studio Qt RHI compositor implementation
// ==============================================================================

#include "RhiCompositor.h"

#include "IRhiFilter.h"
#include "IFilter.h"
#include "Scene.h"
#include "SceneItem.h"

#include <QDebug>
#include <QFile>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QVector>
#if QT_CONFIG(vulkan) && __has_include(<vulkan/vulkan.h>)
#include <QVulkanInstance>
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <vector>

#include <rhi/qrhi.h>

namespace WeaR {

namespace {

constexpr const char* kVertexShader = ":/wear/rhi/rhi_composite.vert.qsb";
constexpr const char* kFragmentShader = ":/wear/rhi/rhi_composite.frag.qsb";

struct Vertex {
    float x;
    float y;
    float u;
    float v;
};

static_assert(sizeof(Vertex) == sizeof(float) * 4);

struct ItemResources {
    QRhiTexture* texture = nullptr;
    QRhiBuffer* vertexBuffer = nullptr;
    QRhiBuffer* uniformBuffer = nullptr;
    QRhiShaderResourceBindings* srb = nullptr;
    QSize textureSize;
};

template <typename T>
void destroyRhiResource(T*& resource) {
    if (!resource) {
        return;
    }
    resource->destroy();
    delete resource;
    resource = nullptr;
}

QRhi::Implementation implementationForName(const QString& name) {
    const QString value = name.trimmed().toLower();
    if (value == "d3d11") return QRhi::D3D11;
    if (value == "vulkan") return QRhi::Vulkan;
    if (value == "metal") return QRhi::Metal;
    if (value == "opengl" || value == "gles2") return QRhi::OpenGLES2;
    return QRhi::Null;
}

QStringList candidateBackends(const QString& requested) {
    const QString value = requested.trimmed().toLower();

    if (!value.isEmpty() && value != "auto") {
        return {value};
    }

#ifdef Q_OS_WIN
    return {"d3d11", "vulkan", "opengl"};
#elif defined(Q_OS_MACOS) || defined(Q_OS_IOS)
    return {"metal", "vulkan", "opengl"};
#else
    return {"vulkan", "opengl"};
#endif
}

QShader loadQsbShader(const char* resourcePath) {
    QFile file(QString::fromLatin1(resourcePath));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QShader::fromSerialized(file.readAll());
}

float radians(double degrees) {
    return static_cast<float>(degrees * 3.14159265358979323846 / 180.0);
}

QPointF transformPoint(const ItemTransform& transform, const QPointF& local) {
    const QPointF center(
        transform.size.width() * transform.anchor.x(),
        transform.size.height() * transform.anchor.y());

    QPointF p = local - center;
    const double sx = transform.scale.x() * (transform.flipH ? -1.0 : 1.0);
    const double sy = transform.scale.y() * (transform.flipV ? -1.0 : 1.0);
    p.setX(p.x() * sx);
    p.setY(p.y() * sy);

    if (transform.rotation != 0.0) {
        const float a = radians(transform.rotation);
        const double c = std::cos(a);
        const double s = std::sin(a);
        p = QPointF(
            p.x() * c - p.y() * s,
            p.x() * s + p.y() * c);
    }

    p += center;
    p += transform.position;
    return p;
}

std::array<QPointF, 4> itemCorners(const ItemTransform& transform) {
    return {
        transformPoint(transform, QPointF(0.0, 0.0)),
        transformPoint(transform, QPointF(transform.size.width(), 0.0)),
        transformPoint(transform, QPointF(0.0, transform.size.height())),
        transformPoint(transform, QPointF(transform.size.width(), transform.size.height()))
    };
}

Vertex toVertex(const QPointF& p, const QSize& outputSize, float u, float v) {
    const float x = static_cast<float>(
        (p.x() / static_cast<double>(outputSize.width())) * 2.0 - 1.0);
    const float y = static_cast<float>(
        1.0 - (p.y() / static_cast<double>(outputSize.height())) * 2.0);
    return {x, y, u, v};
}

} // namespace

class RhiCompositor::Impl {
public:
    ~Impl() {
        reset();
    }

    bool initialize() {
        if (m_rhi) {
            return true;
        }

        m_lastError.clear();

        const QString requested =
            qEnvironmentVariable("WEAR_RHI_BACKEND", QStringLiteral("auto"));
        const QStringList candidates = candidateBackends(requested);

        for (const QString& candidate : candidates) {
            if (tryCreateBackend(implementationForName(candidate), candidate)) {
                return true;
            }
        }

        if (m_lastError.isEmpty()) {
            m_lastError = QStringLiteral("No usable Qt RHI backend was available.");
        }
        qWarning() << "RHI compositor initialization failed:" << m_lastError;
        return false;
    }

    bool compose(const Scene& scene, const QSize& outputSize, QImage& output) {
        if (!outputSize.isValid() || outputSize.isEmpty()) {
            m_lastError = QStringLiteral("Invalid RHI compositor output size.");
            return false;
        }

        if (!initialize()) {
            return false;
        }

        if (!prepareRenderTarget(outputSize)) {
            reset();
            return false;
        }

        const QList<SceneItem*> items = scene.items();
        std::vector<SceneItem*> visibleItems;
        visibleItems.reserve(static_cast<std::size_t>(items.size()));

        for (SceneItem* item : items) {
            if (!item || !item->isVisible()) {
                continue;
            }

            // Keep blend modes with exact legacy QPainter semantics on the
            // safety path until equivalent RHI blend operators are implemented.
            if (item->blendMode() != BlendMode::Normal) {
                m_lastError = QStringLiteral(
                    "RHI compositor uses QPainter fallback for non-normal blend mode.");
                return false;
            }

            visibleItems.push_back(item);
        }

        if (visibleItems.empty()) {
            output = QImage(outputSize, QImage::Format_ARGB32_Premultiplied);
            output.fill(Qt::black);
            return true;
        }

        if (!preparePipeline()) {
            return false;
        }

        while (m_itemResources.size() < visibleItems.size()) {
            m_itemResources.push_back(ItemResources{});
        }

        for (std::size_t i = visibleItems.size(); i < m_itemResources.size(); ++i) {
            destroyItemResources(m_itemResources[i]);
        }
        m_itemResources.resize(visibleItems.size());

        std::vector<QImage> uploadImages(visibleItems.size());
        std::vector<std::array<Vertex, 4>> vertices(visibleItems.size());
        std::vector<RhiFilterUniforms> uniforms(visibleItems.size());

        // Prepare all CPU-side work before beginOffscreenFrame(). This keeps
        // every failure path outside an in-flight QRhi frame.
        for (std::size_t i = 0; i < visibleItems.size(); ++i) {
            SceneItem* item = visibleItems[i];
            ItemResources& resources = m_itemResources[i];

            QImage frame = item->currentFrame();
            if (frame.isNull()) {
                m_lastError = QStringLiteral(
                    "Scene item did not provide a software frame.");
                return false;
            }

            IRhiFilter* gpuFilter = nullptr;
            IFilter* filter = item->filter();

            if (filter && filter->isActive()) {
                gpuFilter = dynamic_cast<IRhiFilter*>(filter);
                if (!filter->supportsGPU() || !filter->isGPUEnabled() || !gpuFilter) {
                    VideoFrame input;
                    input.softwareFrame = frame;
                    input.timestamp = 0;
                    const VideoFrame filtered = filter->processVideo(input);
                    if (filtered.softwareFrame.isNull()) {
                        m_lastError = QStringLiteral(
                            "Filter could not produce a software frame for RHI upload.");
                        return false;
                    }
                    frame = filtered.softwareFrame;
                    gpuFilter = nullptr;
                }
            }

            const QImage uploadImage = frame.convertToFormat(QImage::Format_RGBA8888);
            if (uploadImage.isNull()) {
                m_lastError = QStringLiteral(
                    "Failed to convert scene item frame to RGBA8.");
                return false;
            }

            uploadImages[i] = uploadImage;

            if (resources.textureSize != uploadImage.size()) {
                destroyRhiResource(resources.texture);
                resources.texture = m_rhi->newTexture(
                    QRhiTexture::RGBA8,
                    uploadImage.size(),
                    1);
                if (!resources.texture || !resources.texture->create()) {
                    m_lastError = QStringLiteral(
                        "Failed to create an RHI source texture.");
                    return false;
                }
                resources.textureSize = uploadImage.size();
                destroyRhiResource(resources.srb);
            }

            if (!resources.srb) {
                if (!resources.texture) {
                    m_lastError = QStringLiteral(
                        "RHI source texture is unavailable.");
                    return false;
                }

                resources.srb = m_rhi->newShaderResourceBindings();
                if (!resources.srb) {
                    m_lastError = QStringLiteral(
                        "Failed to allocate RHI shader bindings.");
                    return false;
                }

                resources.srb->setBindings({
                    QRhiShaderResourceBinding::uniformBuffer(
                        0,
                        QRhiShaderResourceBinding::FragmentStage,
                        resources.uniformBuffer),
                    QRhiShaderResourceBinding::sampledTexture(
                        1,
                        QRhiShaderResourceBinding::FragmentStage,
                        resources.texture,
                        m_sampler)
                });

                if (!resources.srb->create()) {
                    m_lastError = QStringLiteral(
                        "Failed to create RHI shader bindings.");
                    return false;
                }
            }

            const ItemTransform transform = item->transform();
            const auto corners = itemCorners(transform);
            vertices[i] = {
                toVertex(corners[0], outputSize, 0.0f, 0.0f),
                toVertex(corners[1], outputSize, 1.0f, 0.0f),
                toVertex(corners[2], outputSize, 0.0f, 1.0f),
                toVertex(corners[3], outputSize, 1.0f, 1.0f)
            };

            uniforms[i] = {};
            uniforms[i].filter3.setX(static_cast<float>(
                std::clamp(transform.opacity, 0.0, 1.0)));
            uniforms[i].filter3.setY(
                uploadImage.width() > 0
                    ? 1.0f / static_cast<float>(uploadImage.width())
                    : 0.0f);
            uniforms[i].filter3.setZ(
                uploadImage.height() > 0
                    ? 1.0f / static_cast<float>(uploadImage.height())
                    : 0.0f);

            if (gpuFilter) {
                uniforms[i] = gpuFilter->rhiUniforms();
                uniforms[i].filter3.setX(static_cast<float>(
                    std::clamp(transform.opacity, 0.0, 1.0)));
                uniforms[i].filter3.setY(
                    uploadImage.width() > 0
                        ? 1.0f / static_cast<float>(uploadImage.width())
                        : 0.0f);
                uniforms[i].filter3.setZ(
                    uploadImage.height() > 0
                        ? 1.0f / static_cast<float>(uploadImage.height())
                        : 0.0f);
            }
        }

        QRhiResourceUpdateBatch* updates = m_rhi->nextResourceUpdateBatch();
        QRhiResourceUpdateBatch* readbackBatch = m_rhi->nextResourceUpdateBatch();
        if (!updates || !readbackBatch) {
            if (updates) {
                updates->release();
            }
            if (readbackBatch) {
                readbackBatch->release();
            }
            m_lastError = QStringLiteral(
                "QRhi resource update batch allocation failed.");
            return false;
        }

        for (std::size_t i = 0; i < visibleItems.size(); ++i) {
            ItemResources& resources = m_itemResources[i];
            updates->uploadTexture(resources.texture, uploadImages[i]);
            updates->updateDynamicBuffer(
                resources.vertexBuffer,
                0,
                static_cast<quint32>(sizeof(vertices[i])),
                vertices[i].data());
            updates->updateDynamicBuffer(
                resources.uniformBuffer,
                0,
                static_cast<quint32>(sizeof(RhiFilterUniforms)),
                &uniforms[i]);
        }

        QRhiReadbackResult readback;
        readbackBatch->readBackTexture(
            QRhiReadbackDescription(m_outputTexture),
            &readback);

        QRhiCommandBuffer* cb = nullptr;
        const auto beginResult = m_rhi->beginOffscreenFrame(&cb);
        if (beginResult != QRhi::FrameOpSuccess || !cb) {
            updates->release();
            readbackBatch->release();
            m_lastError = QStringLiteral("QRhi beginOffscreenFrame() failed.");
            return false;
        }

        cb->beginPass(
            m_renderTarget,
            scene.backgroundColor(),
            {1.0f, 0},
            updates);

        cb->setGraphicsPipeline(m_pipeline);
        cb->setViewport({
            0.0f,
            0.0f,
            static_cast<float>(outputSize.width()),
            static_cast<float>(outputSize.height()),
            0.0f,
            1.0f
        });

        for (ItemResources& resources : m_itemResources) {
            const QRhiCommandBuffer::VertexInput vertexInput(
                resources.vertexBuffer,
                0);
            cb->setShaderResources(resources.srb);
            cb->setVertexInput(0, 1, &vertexInput);
            cb->draw(4);
        }

        cb->endPass(readbackBatch);

        const auto endResult = m_rhi->endOffscreenFrame();
        if (endResult != QRhi::FrameOpSuccess) {
            m_lastError = QStringLiteral(
                "QRhi endOffscreenFrame() failed.");
            reset();
            return false;
        }

        if (m_rhi->isDeviceLost()) {
            m_lastError = QStringLiteral(
                "Qt RHI reported a lost graphics device.");
            reset();
            return false;
        }

        if (readback.data.isEmpty() ||
            readback.pixelSize != outputSize) {
            m_lastError = QStringLiteral(
                "RHI texture readback returned no complete frame.");
            reset();
            return false;
        }

        QImage wrapped(
            reinterpret_cast<const uchar*>(readback.data.constData()),
            readback.pixelSize.width(),
            readback.pixelSize.height(),
            QImage::Format_RGBA8888);

        if (m_rhi->isYUpInFramebuffer()) {
            output = wrapped.mirrored(false, true)
                         .convertToFormat(QImage::Format_ARGB32_Premultiplied);
        } else {
            output = wrapped.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        }

        return !output.isNull();
    }

    void reset() {
        for (ItemResources& resources : m_itemResources) {
            destroyItemResources(resources);
        }
        m_itemResources.clear();

        destroyRhiResource(m_pipeline);
        destroyRhiResource(m_sampler);
        destroyRhiResource(m_renderTarget);
        destroyRhiResource(m_renderPassDescriptor);
        destroyRhiResource(m_outputTexture);

        if (m_rhi) {
            delete m_rhi;
            m_rhi = nullptr;
        }

        // OpenGL fallback surfaces and Vulkan instances must outlive their RHI
        // only, so release them after QRhi is gone.
        m_fallbackSurface.reset();
#if QT_CONFIG(vulkan) && __has_include(<vulkan/vulkan.h>)
        m_vulkanInstance.reset();
#endif

        m_backendName.clear();
        m_outputSize = {};
    }

    bool isInitialized() const {
        return m_rhi != nullptr;
    }

    QString backendName() const {
        return m_backendName;
    }

    QString lastError() const {
        return m_lastError;
    }

private:
    bool tryCreateBackend(QRhi::Implementation impl, const QString& friendlyName) {
        reset();

        if (impl == QRhi::Null) {
            return false;
        }

        QRhi* candidate = nullptr;

        switch (impl) {
        case QRhi::D3D11: {
#ifdef Q_OS_WIN
            QRhiD3D11InitParams params;
            candidate = QRhi::create(QRhi::D3D11, &params);
#else
            return false;
#endif
            break;
        }

        case QRhi::Vulkan: {
#if QT_CONFIG(vulkan) && __has_include(<vulkan/vulkan.h>)
            m_vulkanInstance = std::make_unique<QVulkanInstance>();
            m_vulkanInstance->setExtensions(
                QRhiVulkanInitParams::preferredInstanceExtensions());
            if (!m_vulkanInstance->create()) {
                m_lastError = QStringLiteral("QVulkanInstance creation failed.");
                m_vulkanInstance.reset();
                return false;
            }

            QRhiVulkanInitParams params;
            params.inst = m_vulkanInstance.get();
            candidate = QRhi::create(QRhi::Vulkan, &params);
#else
            m_lastError = QStringLiteral("Qt Vulkan support is not available.");
            return false;
#endif
            break;
        }

        case QRhi::Metal: {
#if QT_CONFIG(metal)
            QRhiMetalInitParams params;
            candidate = QRhi::create(QRhi::Metal, &params);
#else
            m_lastError = QStringLiteral("Qt Metal support is not available.");
            return false;
#endif
            break;
        }

        case QRhi::OpenGLES2: {
            m_fallbackSurface.reset(QRhiGles2InitParams::newFallbackSurface());
            if (!m_fallbackSurface) {
                m_lastError = QStringLiteral("OpenGL fallback surface creation failed.");
                return false;
            }

            QRhiGles2InitParams params;
            params.fallbackSurface = m_fallbackSurface.get();
            candidate = QRhi::create(QRhi::OpenGLES2, &params);
            break;
        }

        default:
            break;
        }

        if (!candidate) {
            m_lastError = QStringLiteral(
                "QRhi could not initialize backend %1.").arg(friendlyName);
            return false;
        }

        m_rhi = candidate;
        m_backendName = QString::fromUtf8(m_rhi->backendName());
        m_vertexShader = loadQsbShader(kVertexShader);
        m_fragmentShader = loadQsbShader(kFragmentShader);

        if (!m_vertexShader.isValid() || !m_fragmentShader.isValid()) {
            m_lastError = QStringLiteral(
                "Embedded RHI shader package is missing or invalid.");
            reset();
            return false;
        }

        qInfo() << "RHI compositor initialized using" << m_backendName;
        return true;
    }

    bool prepareRenderTarget(const QSize& size) {
        if (m_outputSize == size && m_renderTarget && m_pipeline) {
            return true;
        }

        destroyRhiResource(m_pipeline);
        destroyRhiResource(m_renderTarget);
        destroyRhiResource(m_renderPassDescriptor);
        destroyRhiResource(m_outputTexture);
        m_outputSize = size;

        m_outputTexture = m_rhi->newTexture(
            QRhiTexture::RGBA8,
            size,
            1,
            QRhiTexture::RenderTarget | QRhiTexture::UsedAsTransferSource);
        if (!m_outputTexture || !m_outputTexture->create()) {
            m_lastError = QStringLiteral("Failed to create RHI output texture.");
            return false;
        }

        QRhiTextureRenderTargetDescription desc({m_outputTexture});
        m_renderTarget = m_rhi->newTextureRenderTarget(desc);
        if (!m_renderTarget) {
            m_lastError = QStringLiteral("Failed to allocate RHI render target.");
            return false;
        }

        m_renderPassDescriptor = m_renderTarget->newCompatibleRenderPassDescriptor();
        if (!m_renderPassDescriptor) {
            m_lastError = QStringLiteral("Failed to allocate RHI render pass descriptor.");
            return false;
        }

        m_renderTarget->setRenderPassDescriptor(m_renderPassDescriptor);
        if (!m_renderTarget->create()) {
            m_lastError = QStringLiteral("Failed to create RHI render target.");
            return false;
        }

        // Rebuild item bindings because the pipeline's render-pass descriptor
        // changed when the render target changed.
        for (ItemResources& resources : m_itemResources) {
            destroyRhiResource(resources.srb);
        }

        return true;
    }

    bool preparePipeline() {
        if (m_pipeline) {
            return true;
        }

        destroyRhiResource(m_sampler);
        m_sampler = m_rhi->newSampler(
            QRhiSampler::Linear,
            QRhiSampler::Linear,
            QRhiSampler::None,
            QRhiSampler::ClampToEdge,
            QRhiSampler::ClampToEdge);
        if (!m_sampler || !m_sampler->create()) {
            m_lastError = QStringLiteral("Failed to create RHI sampler.");
            return false;
        }

        // We need a prototype SRB for the layout used by the shared pipeline.
        ItemResources prototype;
        if (!prepareItemResources(prototype, true)) {
            destroyItemResources(prototype);
            return false;
        }

        m_pipeline = m_rhi->newGraphicsPipeline();
        if (!m_pipeline) {
            destroyItemResources(prototype);
            m_lastError = QStringLiteral("Failed to allocate RHI graphics pipeline.");
            return false;
        }

        m_pipeline->setShaderStages({
            {QRhiShaderStage::Vertex, m_vertexShader},
            {QRhiShaderStage::Fragment, m_fragmentShader}
        });

        QRhiVertexInputLayout inputLayout;
        inputLayout.setBindings({
            QRhiVertexInputBinding(sizeof(Vertex))
        });
        inputLayout.setAttributes({
            QRhiVertexInputAttribute(
                0, 0, QRhiVertexInputAttribute::Float2, 0),
            QRhiVertexInputAttribute(
                0, 1, QRhiVertexInputAttribute::Float2, sizeof(float) * 2)
        });
        m_pipeline->setVertexInputLayout(inputLayout);
        m_pipeline->setTopology(QRhiGraphicsPipeline::TriangleStrip);
        m_pipeline->setShaderResourceBindings(prototype.srb);
        m_pipeline->setRenderPassDescriptor(m_renderPassDescriptor);

        QRhiGraphicsPipeline::TargetBlend blend;
        blend.enable = true;
        blend.srcColor = QRhiGraphicsPipeline::SrcAlpha;
        blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
        blend.srcAlpha = QRhiGraphicsPipeline::One;
        blend.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;
        m_pipeline->setTargetBlends({blend});

        if (!m_pipeline->create()) {
            destroyItemResources(prototype);
            destroyRhiResource(m_pipeline);
            m_lastError = QStringLiteral("Failed to create RHI graphics pipeline.");
            return false;
        }

        // The pipeline only retains the SRB layout. The actual draw calls use
        // per-item SRBs with the same layout.
        destroyItemResources(prototype);
        return true;
    }

    bool prepareItemResources(ItemResources& resources, bool buildPrototype = false) {
        if (!resources.vertexBuffer) {
            resources.vertexBuffer = m_rhi->newBuffer(
                QRhiBuffer::Dynamic,
                QRhiBuffer::VertexBuffer,
                sizeof(Vertex) * 4);
            if (!resources.vertexBuffer || !resources.vertexBuffer->create()) {
                m_lastError = QStringLiteral("Failed to create RHI vertex buffer.");
                return false;
            }
        }

        if (!resources.uniformBuffer) {
            const quint32 size = static_cast<quint32>(
                m_rhi->ubufAligned(sizeof(RhiFilterUniforms)));
            resources.uniformBuffer = m_rhi->newBuffer(
                QRhiBuffer::Dynamic,
                QRhiBuffer::UniformBuffer,
                size);
            if (!resources.uniformBuffer || !resources.uniformBuffer->create()) {
                m_lastError = QStringLiteral("Failed to create RHI uniform buffer.");
                return false;
            }
        }

        if (buildPrototype) {
            resources.texture = m_rhi->newTexture(
                QRhiTexture::RGBA8,
                QSize(1, 1),
                1);
            if (!resources.texture || !resources.texture->create()) {
                m_lastError = QStringLiteral("Failed to create RHI prototype texture.");
                return false;
            }
            resources.textureSize = QSize(1, 1);
        }

        if (!resources.srb && resources.texture) {
            resources.srb = m_rhi->newShaderResourceBindings();
            if (!resources.srb) {
                m_lastError = QStringLiteral("Failed to allocate prototype shader bindings.");
                return false;
            }
            resources.srb->setBindings({
                QRhiShaderResourceBinding::uniformBuffer(
                    0,
                    QRhiShaderResourceBinding::VertexStage |
                        QRhiShaderResourceBinding::FragmentStage,
                    resources.uniformBuffer),
                QRhiShaderResourceBinding::sampledTexture(
                    1,
                    QRhiShaderResourceBinding::FragmentStage,
                    resources.texture,
                    m_sampler)
            });
            if (!resources.srb->create()) {
                m_lastError = QStringLiteral("Failed to create prototype shader bindings.");
                return false;
            }
        }

        return true;
    }

    void destroyItemResources(ItemResources& resources) {
        destroyRhiResource(resources.srb);
        destroyRhiResource(resources.uniformBuffer);
        destroyRhiResource(resources.vertexBuffer);
        destroyRhiResource(resources.texture);
        resources.textureSize = {};
    }

    QRhi* m_rhi = nullptr;
    QString m_backendName;
    QString m_lastError;

    QShader m_vertexShader;
    QShader m_fragmentShader;

    std::unique_ptr<QOffscreenSurface> m_fallbackSurface;
#if QT_CONFIG(vulkan) && __has_include(<vulkan/vulkan.h>)
    std::unique_ptr<QVulkanInstance> m_vulkanInstance;
#endif

    QRhiTexture* m_outputTexture = nullptr;
    QRhiTextureRenderTarget* m_renderTarget = nullptr;
    QRhiRenderPassDescriptor* m_renderPassDescriptor = nullptr;
    QRhiSampler* m_sampler = nullptr;
    QRhiGraphicsPipeline* m_pipeline = nullptr;

    QSize m_outputSize;
    std::vector<ItemResources> m_itemResources;
};

RhiCompositor::RhiCompositor()
    : m_impl(std::make_unique<Impl>()) {}

RhiCompositor::~RhiCompositor() = default;

bool RhiCompositor::initialize() {
    return m_impl->initialize();
}

bool RhiCompositor::compose(
    const Scene& scene,
    const QSize& outputSize,
    QImage& output) {
    return m_impl->compose(scene, outputSize, output);
}

void RhiCompositor::reset() {
    m_impl->reset();
}

bool RhiCompositor::isInitialized() const {
    return m_impl->isInitialized();
}

QString RhiCompositor::backendName() const {
    return m_impl->backendName();
}

QString RhiCompositor::lastError() const {
    return m_impl->lastError();
}

} // namespace WeaR
