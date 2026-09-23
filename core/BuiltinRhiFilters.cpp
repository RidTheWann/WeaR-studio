// ==============================================================================
// WeaR-studio built-in GPU-capable filters implementation
// ==============================================================================

#include "BuiltinRhiFilters.h"

#include <QImage>
#include <algorithm>
#include <cmath>

namespace WeaR {

namespace {

PluginCapability videoFilterCapabilities() {
    return PluginCapability::HasVideo |
           PluginCapability::HasSettings |
           PluginCapability::ThreadSafe;
}

float smoothStep(float edge0, float edge1, float x) {
    if (edge0 == edge1) {
        return x < edge0 ? 0.0f : 1.0f;
    }
    const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

QImage ensureRgba(const VideoFrame& input) {
    if (!input.softwareFrame.isNull()) {
        return input.softwareFrame.convertToFormat(QImage::Format_RGBA8888);
    }
    return {};
}

uint8_t clampByte(float value) {
    return static_cast<uint8_t>(std::lround(std::clamp(value, 0.0f, 255.0f)));
}

} // namespace

// =============================================================================
// ChromaKeyFilter
// =============================================================================

ChromaKeyFilter::ChromaKeyFilter() = default;

PluginInfo ChromaKeyFilter::info() const {
    return {
        QStringLiteral("wear.filter.chroma_key"),
        QStringLiteral("Chroma Key"),
        QStringLiteral("GPU green-screen/chroma key filter"),
        QStringLiteral("0.2.0"),
        QStringLiteral("WeaR-studio"),
        QStringLiteral(""),
        PluginType::Filter,
        videoFilterCapabilities()
    };
}

QString ChromaKeyFilter::name() const {
    return QStringLiteral("Chroma Key");
}

QString ChromaKeyFilter::version() const {
    return QStringLiteral("0.2.0");
}

PluginCapability ChromaKeyFilter::capabilities() const {
    return videoFilterCapabilities();
}

bool ChromaKeyFilter::initialize() {
    QMutexLocker lock(&m_mutex);
    m_active = true;
    return true;
}

void ChromaKeyFilter::shutdown() {
    QMutexLocker lock(&m_mutex);
    m_active = false;
}

bool ChromaKeyFilter::isActive() const {
    QMutexLocker lock(&m_mutex);
    return m_active;
}

QList<FilterParameter> ChromaKeyFilter::parameters() const {
    return {
        {"keyColor", "Key Color", "Color to remove", FilterParameter::Type::Color,
         QColor(0, 255, 0), {}, {}, {}},
        {"threshold", "Threshold", "Distance from key color where transparency begins",
         FilterParameter::Type::Double, 0.24, 0.0, 1.0, 0.01},
        {"softness", "Softness", "Soft edge around the chroma threshold",
         FilterParameter::Type::Double, 0.08, 0.0, 1.0, 0.01}
    };
}

QVariant ChromaKeyFilter::parameterValue(const QString& parameterId) const {
    QMutexLocker lock(&m_mutex);
    if (parameterId == "keyColor") return m_keyColor;
    if (parameterId == "threshold") return m_threshold;
    if (parameterId == "softness") return m_softness;
    return {};
}

bool ChromaKeyFilter::setParameter(const QString& parameterId, const QVariant& value) {
    QMutexLocker lock(&m_mutex);
    if (parameterId == "keyColor" && value.canConvert<QColor>()) {
        m_keyColor = value.value<QColor>();
        return true;
    }
    if (parameterId == "threshold") {
        m_threshold = std::clamp(value.toFloat(), 0.0f, 1.0f);
        return true;
    }
    if (parameterId == "softness") {
        m_softness = std::clamp(value.toFloat(), 0.0f, 1.0f);
        return true;
    }
    return false;
}

QMap<QString, QVariant> ChromaKeyFilter::allParameters() const {
    return {
        {"keyColor", parameterValue("keyColor")},
        {"threshold", parameterValue("threshold")},
        {"softness", parameterValue("softness")}
    };
}

void ChromaKeyFilter::setAllParameters(const QMap<QString, QVariant>& parameters) {
    for (auto it = parameters.cbegin(); it != parameters.cend(); ++it) {
        setParameter(it.key(), it.value());
    }
}

void ChromaKeyFilter::resetToDefaults() {
    setParameter("keyColor", QColor(0, 255, 0));
    setParameter("threshold", 0.24f);
    setParameter("softness", 0.08f);
}

VideoFrame ChromaKeyFilter::processVideo(const VideoFrame& input) {
    const QImage source = ensureRgba(input);
    if (source.isNull()) {
        return input;
    }

    QColor keyColor;
    float threshold = 0.24f;
    float softness = 0.08f;
    {
        QMutexLocker lock(&m_mutex);
        if (!m_active) {
            return input;
        }
        keyColor = m_keyColor;
        threshold = m_threshold;
        softness = m_softness;
    }

    QImage result = source;
    const float kr = keyColor.redF();
    const float kg = keyColor.greenF();
    const float kb = keyColor.blueF();

    for (int y = 0; y < result.height(); ++y) {
        auto* pixels = reinterpret_cast<QRgb*>(result.scanLine(y));
        for (int x = 0; x < result.width(); ++x) {
            const QRgb p = pixels[x];
            const float r = qRed(p) / 255.0f;
            const float g = qGreen(p) / 255.0f;
            const float b = qBlue(p) / 255.0f;

            const float dr = r - kr;
            const float dg = g - kg;
            const float db = b - kb;
            const float distance = std::sqrt(dr * dr + dg * dg + db * db);
            const float keepAlpha = smoothStep(threshold - softness,
                                               threshold + softness,
                                               distance);
            const uint8_t alpha = static_cast<uint8_t>(
                std::lround(qAlpha(p) * keepAlpha));

            pixels[x] = qPremultiply(qRgba(qRed(p), qGreen(p), qBlue(p), alpha));
        }
    }

    VideoFrame output = input;
    output.softwareFrame = result.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    output.isHardwareFrame = false;
    output.hardwareFrame = nullptr;
    return output;
}

void ChromaKeyFilter::setGPUEnabled(bool enable) {
    QMutexLocker lock(&m_mutex);
    m_gpuEnabled = enable;
}

bool ChromaKeyFilter::isGPUEnabled() const {
    QMutexLocker lock(&m_mutex);
    return m_gpuEnabled;
}

RhiFilterUniforms ChromaKeyFilter::rhiUniforms() const {
    QMutexLocker lock(&m_mutex);
    return {
        QVector4D(1.0f, m_threshold, m_softness, 0.0f),
        QVector4D(m_keyColor.redF(), m_keyColor.greenF(), m_keyColor.blueF(), 1.0f),
        QVector4D(0.0f, 0.0f, 0.0f, 0.0f),
        QVector4D(0.0f, 0.0f, 0.0f, 0.0f)
    };
}

// =============================================================================
// GaussianBlurFilter
// =============================================================================

GaussianBlurFilter::GaussianBlurFilter() = default;

PluginInfo GaussianBlurFilter::info() const {
    return {
        QStringLiteral("wear.filter.gaussian_blur"),
        QStringLiteral("Gaussian Blur"),
        QStringLiteral("GPU 3x3 Gaussian blur filter"),
        QStringLiteral("0.2.0"),
        QStringLiteral("WeaR-studio"),
        QStringLiteral(""),
        PluginType::Filter,
        videoFilterCapabilities()
    };
}

QString GaussianBlurFilter::name() const {
    return QStringLiteral("Gaussian Blur");
}

QString GaussianBlurFilter::version() const {
    return QStringLiteral("0.2.0");
}

PluginCapability GaussianBlurFilter::capabilities() const {
    return videoFilterCapabilities();
}

bool GaussianBlurFilter::initialize() {
    QMutexLocker lock(&m_mutex);
    m_active = true;
    return true;
}

void GaussianBlurFilter::shutdown() {
    QMutexLocker lock(&m_mutex);
    m_active = false;
}

bool GaussianBlurFilter::isActive() const {
    QMutexLocker lock(&m_mutex);
    return m_active;
}

QList<FilterParameter> GaussianBlurFilter::parameters() const {
    return {
        {"radius", "Radius", "Sample spacing for the Gaussian kernel",
         FilterParameter::Type::Double, 2.0, 1.0, 8.0, 1.0}
    };
}

QVariant GaussianBlurFilter::parameterValue(const QString& parameterId) const {
    QMutexLocker lock(&m_mutex);
    if (parameterId == "radius") return m_radius;
    return {};
}

bool GaussianBlurFilter::setParameter(const QString& parameterId, const QVariant& value) {
    QMutexLocker lock(&m_mutex);
    if (parameterId == "radius") {
        m_radius = std::clamp(value.toFloat(), 1.0f, 8.0f);
        return true;
    }
    return false;
}

QMap<QString, QVariant> GaussianBlurFilter::allParameters() const {
    return {{"radius", parameterValue("radius")}};
}

void GaussianBlurFilter::setAllParameters(const QMap<QString, QVariant>& parameters) {
    for (auto it = parameters.cbegin(); it != parameters.cend(); ++it) {
        setParameter(it.key(), it.value());
    }
}

void GaussianBlurFilter::resetToDefaults() {
    setParameter("radius", 2.0f);
}

VideoFrame GaussianBlurFilter::processVideo(const VideoFrame& input) {
    const QImage source = ensureRgba(input);
    if (source.isNull()) {
        return input;
    }

    float radius = 2.0f;
    {
        QMutexLocker lock(&m_mutex);
        if (!m_active) {
            return input;
        }
        radius = m_radius;
    }

    const int r = std::max(1, qRound(radius));
    const int weights[3][3] = {
        {1, 2, 1},
        {2, 4, 2},
        {1, 2, 1}
    };

    QImage result(source.size(), QImage::Format_RGBA8888);
    for (int y = 0; y < source.height(); ++y) {
        auto* dst = reinterpret_cast<QRgb*>(result.scanLine(y));
        for (int x = 0; x < source.width(); ++x) {
            int ar = 0;
            int ag = 0;
            int ab = 0;
            int aa = 0;
            int weightSum = 0;

            for (int ky = -1; ky <= 1; ++ky) {
                const int sy = std::clamp(y + ky * r, 0, source.height() - 1);
                const auto* srcRow = reinterpret_cast<const QRgb*>(source.constScanLine(sy));

                for (int kx = -1; kx <= 1; ++kx) {
                    const int sx = std::clamp(x + kx * r, 0, source.width() - 1);
                    const int w = weights[ky + 1][kx + 1];
                    const QRgb p = srcRow[sx];
                    ar += qRed(p) * w;
                    ag += qGreen(p) * w;
                    ab += qBlue(p) * w;
                    aa += qAlpha(p) * w;
                    weightSum += w;
                }
            }

            dst[x] = qRgba(
                ar / weightSum, ag / weightSum, ab / weightSum, aa / weightSum);
        }
    }

    VideoFrame output = input;
    output.softwareFrame = result.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    output.isHardwareFrame = false;
    output.hardwareFrame = nullptr;
    return output;
}

void GaussianBlurFilter::setGPUEnabled(bool enable) {
    QMutexLocker lock(&m_mutex);
    m_gpuEnabled = enable;
}

bool GaussianBlurFilter::isGPUEnabled() const {
    QMutexLocker lock(&m_mutex);
    return m_gpuEnabled;
}

RhiFilterUniforms GaussianBlurFilter::rhiUniforms() const {
    QMutexLocker lock(&m_mutex);
    return {
        QVector4D(2.0f, m_radius, 0.0f, 0.0f),
        QVector4D(0.0f, 0.0f, 0.0f, 0.0f),
        QVector4D(0.0f, 0.0f, 0.0f, 0.0f),
        QVector4D(0.0f, 0.0f, 0.0f, 0.0f)
    };
}

// =============================================================================
// ColorCorrectionFilter
// =============================================================================

ColorCorrectionFilter::ColorCorrectionFilter() = default;

PluginInfo ColorCorrectionFilter::info() const {
    return {
        QStringLiteral("wear.filter.color_correction"),
        QStringLiteral("Color Correction"),
        QStringLiteral("GPU brightness/contrast/saturation/gamma correction"),
        QStringLiteral("0.2.0"),
        QStringLiteral("WeaR-studio"),
        QStringLiteral(""),
        PluginType::Filter,
        videoFilterCapabilities()
    };
}

QString ColorCorrectionFilter::name() const {
    return QStringLiteral("Color Correction");
}

QString ColorCorrectionFilter::version() const {
    return QStringLiteral("0.2.0");
}

PluginCapability ColorCorrectionFilter::capabilities() const {
    return videoFilterCapabilities();
}

bool ColorCorrectionFilter::initialize() {
    QMutexLocker lock(&m_mutex);
    m_active = true;
    return true;
}

void ColorCorrectionFilter::shutdown() {
    QMutexLocker lock(&m_mutex);
    m_active = false;
}

bool ColorCorrectionFilter::isActive() const {
    QMutexLocker lock(&m_mutex);
    return m_active;
}

QList<FilterParameter> ColorCorrectionFilter::parameters() const {
    return {
        {"brightness", "Brightness", "Additive brightness adjustment",
         FilterParameter::Type::Double, 0.0, -1.0, 1.0, 0.01},
        {"contrast", "Contrast", "Contrast multiplier",
         FilterParameter::Type::Double, 1.0, 0.0, 3.0, 0.01},
        {"saturation", "Saturation", "Saturation multiplier",
         FilterParameter::Type::Double, 1.0, 0.0, 3.0, 0.01},
        {"gamma", "Gamma", "Gamma correction",
         FilterParameter::Type::Double, 1.0, 0.1, 4.0, 0.01}
    };
}

QVariant ColorCorrectionFilter::parameterValue(const QString& parameterId) const {
    QMutexLocker lock(&m_mutex);
    if (parameterId == "brightness") return m_brightness;
    if (parameterId == "contrast") return m_contrast;
    if (parameterId == "saturation") return m_saturation;
    if (parameterId == "gamma") return m_gamma;
    return {};
}

bool ColorCorrectionFilter::setParameter(const QString& parameterId, const QVariant& value) {
    QMutexLocker lock(&m_mutex);
    if (parameterId == "brightness") {
        m_brightness = std::clamp(value.toFloat(), -1.0f, 1.0f);
        return true;
    }
    if (parameterId == "contrast") {
        m_contrast = std::clamp(value.toFloat(), 0.0f, 3.0f);
        return true;
    }
    if (parameterId == "saturation") {
        m_saturation = std::clamp(value.toFloat(), 0.0f, 3.0f);
        return true;
    }
    if (parameterId == "gamma") {
        m_gamma = std::clamp(value.toFloat(), 0.1f, 4.0f);
        return true;
    }
    return false;
}

QMap<QString, QVariant> ColorCorrectionFilter::allParameters() const {
    return {
        {"brightness", parameterValue("brightness")},
        {"contrast", parameterValue("contrast")},
        {"saturation", parameterValue("saturation")},
        {"gamma", parameterValue("gamma")}
    };
}

void ColorCorrectionFilter::setAllParameters(const QMap<QString, QVariant>& parameters) {
    for (auto it = parameters.cbegin(); it != parameters.cend(); ++it) {
        setParameter(it.key(), it.value());
    }
}

void ColorCorrectionFilter::resetToDefaults() {
    setParameter("brightness", 0.0f);
    setParameter("contrast", 1.0f);
    setParameter("saturation", 1.0f);
    setParameter("gamma", 1.0f);
}

VideoFrame ColorCorrectionFilter::processVideo(const VideoFrame& input) {
    const QImage source = ensureRgba(input);
    if (source.isNull()) {
        return input;
    }

    float brightness = 0.0f;
    float contrast = 1.0f;
    float saturation = 1.0f;
    float gamma = 1.0f;
    {
        QMutexLocker lock(&m_mutex);
        if (!m_active) {
            return input;
        }
        brightness = m_brightness;
        contrast = m_contrast;
        saturation = m_saturation;
        gamma = m_gamma;
    }

    QImage result = source;
    const float gammaInv = 1.0f / std::max(0.1f, gamma);

    for (int y = 0; y < result.height(); ++y) {
        auto* pixels = reinterpret_cast<QRgb*>(result.scanLine(y));
        for (int x = 0; x < result.width(); ++x) {
            const QRgb p = pixels[x];

            float r = qRed(p) / 255.0f + brightness;
            float g = qGreen(p) / 255.0f + brightness;
            float b = qBlue(p) / 255.0f + brightness;

            r = (r - 0.5f) * contrast + 0.5f;
            g = (g - 0.5f) * contrast + 0.5f;
            b = (b - 0.5f) * contrast + 0.5f;

            const float luma = 0.2126f * r + 0.7152f * g + 0.0722f * b;
            r = luma + (r - luma) * saturation;
            g = luma + (g - luma) * saturation;
            b = luma + (b - luma) * saturation;

            r = std::pow(std::clamp(r, 0.0f, 1.0f), gammaInv);
            g = std::pow(std::clamp(g, 0.0f, 1.0f), gammaInv);
            b = std::pow(std::clamp(b, 0.0f, 1.0f), gammaInv);

            pixels[x] = qPremultiply(qRgba(
                clampByte(r * 255.0f),
                clampByte(g * 255.0f),
                clampByte(b * 255.0f),
                qAlpha(p)));
        }
    }

    VideoFrame output = input;
    output.softwareFrame = result.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    output.isHardwareFrame = false;
    output.hardwareFrame = nullptr;
    return output;
}

void ColorCorrectionFilter::setGPUEnabled(bool enable) {
    QMutexLocker lock(&m_mutex);
    m_gpuEnabled = enable;
}

bool ColorCorrectionFilter::isGPUEnabled() const {
    QMutexLocker lock(&m_mutex);
    return m_gpuEnabled;
}

RhiFilterUniforms ColorCorrectionFilter::rhiUniforms() const {
    QMutexLocker lock(&m_mutex);
    return {
        QVector4D(3.0f, m_brightness, m_contrast, m_saturation),
        QVector4D(m_gamma, 0.0f, 0.0f, 0.0f),
        QVector4D(0.0f, 0.0f, 0.0f, 0.0f),
        QVector4D(0.0f, 0.0f, 0.0f, 0.0f)
    };
}

} // namespace WeaR
