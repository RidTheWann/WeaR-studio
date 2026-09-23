#pragma once
// ==============================================================================
// WeaR-studio built-in GPU-capable filters
// ==============================================================================

#include "IFilter.h"
#include "IRhiFilter.h"

#include <QColor>
#include <QMutex>
#include <QVariant>

namespace WeaR {

class ChromaKeyFilter final : public IFilter, public IRhiFilter {
public:
    ChromaKeyFilter();

    PluginInfo info() const override;
    QString name() const override;
    QString version() const override;
    PluginCapability capabilities() const override;

    bool initialize() override;
    void shutdown() override;
    bool isActive() const override;

    QList<FilterParameter> parameters() const override;
    QVariant parameterValue(const QString& parameterId) const override;
    bool setParameter(const QString& parameterId, const QVariant& value) override;
    QMap<QString, QVariant> allParameters() const override;
    void setAllParameters(const QMap<QString, QVariant>& parameters) override;
    void resetToDefaults() override;

    VideoFrame processVideo(const VideoFrame& input) override;

    bool supportsGPU() const override { return true; }
    void setGPUEnabled(bool enable) override;
    bool isGPUEnabled() const override;

    RhiFilterKind rhiFilterKind() const override { return RhiFilterKind::ChromaKey; }
    RhiFilterUniforms rhiUniforms() const override;

private:
    mutable QMutex m_mutex;
    QColor m_keyColor{0, 255, 0};
    float m_threshold = 0.24f;
    float m_softness = 0.08f;
    bool m_active = true;
    bool m_gpuEnabled = true;
};

class GaussianBlurFilter final : public IFilter, public IRhiFilter {
public:
    GaussianBlurFilter();

    PluginInfo info() const override;
    QString name() const override;
    QString version() const override;
    PluginCapability capabilities() const override;

    bool initialize() override;
    void shutdown() override;
    bool isActive() const override;

    QList<FilterParameter> parameters() const override;
    QVariant parameterValue(const QString& parameterId) const override;
    bool setParameter(const QString& parameterId, const QVariant& value) override;
    QMap<QString, QVariant> allParameters() const override;
    void setAllParameters(const QMap<QString, QVariant>& parameters) override;
    void resetToDefaults() override;

    VideoFrame processVideo(const VideoFrame& input) override;

    bool supportsGPU() const override { return true; }
    void setGPUEnabled(bool enable) override;
    bool isGPUEnabled() const override;

    RhiFilterKind rhiFilterKind() const override { return RhiFilterKind::GaussianBlur; }
    RhiFilterUniforms rhiUniforms() const override;

private:
    mutable QMutex m_mutex;
    float m_radius = 2.0f;
    bool m_active = true;
    bool m_gpuEnabled = true;
};

class ColorCorrectionFilter final : public IFilter, public IRhiFilter {
public:
    ColorCorrectionFilter();

    PluginInfo info() const override;
    QString name() const override;
    QString version() const override;
    PluginCapability capabilities() const override;

    bool initialize() override;
    void shutdown() override;
    bool isActive() const override;

    QList<FilterParameter> parameters() const override;
    QVariant parameterValue(const QString& parameterId) const override;
    bool setParameter(const QString& parameterId, const QVariant& value) override;
    QMap<QString, QVariant> allParameters() const override;
    void setAllParameters(const QMap<QString, QVariant>& parameters) override;
    void resetToDefaults() override;

    VideoFrame processVideo(const VideoFrame& input) override;

    bool supportsGPU() const override { return true; }
    void setGPUEnabled(bool enable) override;
    bool isGPUEnabled() const override;

    RhiFilterKind rhiFilterKind() const override { return RhiFilterKind::ColorCorrection; }
    RhiFilterUniforms rhiUniforms() const override;

private:
    mutable QMutex m_mutex;
    float m_brightness = 0.0f;
    float m_contrast = 1.0f;
    float m_saturation = 1.0f;
    float m_gamma = 1.0f;
    bool m_active = true;
    bool m_gpuEnabled = true;
};

} // namespace WeaR
