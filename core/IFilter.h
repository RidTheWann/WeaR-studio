#pragma once
// ==============================================================================
// WeaR-studio Filter Interface
// IFilter.h - Interface for video/audio filter plugins
// ==============================================================================

#include "IPlugin.h"
#include "ISource.h"
#include <QImage>
#include <QVariant>
#include <QMap>

namespace WeaR {

/**
 * @brief Filter parameter definition
 * Describes a configurable parameter for the filter
 */
struct FilterParameter {
    enum class Type {
        Boolean,    ///< Checkbox
        Integer,    ///< Slider or spinbox with integer values
        Double,     ///< Slider or spinbox with floating point values
        String,     ///< Text input
        Color,      ///< Color picker (QColor)
        FilePath,   ///< File selection dialog
        Enum,       ///< Dropdown selection
        Point,      ///< 2D point (QPointF)
        Rect,       ///< Rectangle (QRectF)
    };

    QString id;             ///< Parameter identifier
    QString name;           ///< Display name
    QString description;    ///< Tooltip/description
    Type type;              ///< Parameter type
    QVariant defaultValue;  ///< Default value
    QVariant minValue;      ///< Minimum value (for numeric types)
    QVariant maxValue;      ///< Maximum value (for numeric types)
    QVariant step;          ///< Step size (for numeric types)
    QStringList enumValues; ///< Possible values (for Enum type)
};

/**
 * @brief Filter interface for video/audio processing plugins
 * 
 * Filters transform input frames into output frames. Examples include:
 * - Color correction (brightness, contrast, saturation)
 * - Chroma key (green screen removal)
 * - Blur/sharpen effects
 * - Crop/scale transformations
 * - Audio filters (noise suppression, EQ)
 * 
 * Filters can be chained together in a processing pipeline.
 */
class IFilter : public IPlugin {
public:
    ~IFilter() override = default;

    /**
     * @brief Get plugin type (always Filter)
     */
    [[nodiscard]] PluginType type() const override { return PluginType::Filter; }

    /**
     * @brief Get list of configurable parameters
     * @return List of FilterParameter definitions
     */
    [[nodiscard]] virtual QList<FilterParameter> parameters() const = 0;

    /**
     * @brief Get current parameter value
     * @param parameterId Parameter identifier
     * @return Current value, invalid QVariant if not found
     */
    [[nodiscard]] virtual QVariant parameterValue(const QString& parameterId) const = 0;

    /**
     * @brief Set parameter value
     * @param parameterId Parameter identifier
     * @param value New value
     * @return true if value was accepted
     */
    virtual bool setParameter(const QString& parameterId, const QVariant& value) = 0;

    /**
     * @brief Get all parameter values as a map
     * @return Map of parameter IDs to values
     */
    [[nodiscard]] virtual QMap<QString, QVariant> allParameters() const = 0;

    /**
     * @brief Set multiple parameters at once
     * @param parameters Map of parameter IDs to values
     */
    virtual void setAllParameters(const QMap<QString, QVariant>& parameters) = 0;

    /**
     * @brief Reset all parameters to default values
     */
    virtual void resetToDefaults() = 0;

    /**
     * @brief Process a video frame
     * 
     * Apply the filter effect to the input frame and return the result.
     * This method must be thread-safe if ThreadSafe capability is set.
     * 
     * @param input Input video frame
     * @return Processed video frame
     */
    [[nodiscard]] virtual VideoFrame processVideo(const VideoFrame& input) = 0;

    /**
     * @brief Process audio samples
     * 
     * Apply the filter effect to audio samples.
     * Only valid if HasAudio capability is set.
     * 
     * @param input Input audio frame
     * @return Processed audio frame
     */
    [[nodiscard]] virtual AudioFrame processAudio(const AudioFrame& input) {
        return input; // Default: pass-through
    }

    /**
     * @brief Check if this filter supports GPU acceleration
     * @return true if GPU processing is available
     */
    [[nodiscard]] virtual bool supportsGPU() const { return false; }

    /**
     * @brief Enable/disable GPU acceleration
     * @param enable true to use GPU, false for CPU
     */
    virtual void setGPUEnabled(bool enable) { (void)enable; }

    /**
     * @brief Check if GPU is currently enabled
     * @return true if using GPU processing
     */
    [[nodiscard]] virtual bool isGPUEnabled() const { return false; }

    /**
     * @brief Get processing time statistics
     * @return Average processing time in milliseconds
     */
    [[nodiscard]] virtual double averageProcessingTimeMs() const { return 0.0; }
};

} // namespace WeaR

// Qt Plugin interface declaration for filters
#define WEAR_FILTER_IID "com.wear-studio.filter/1.0"
Q_DECLARE_INTERFACE(WeaR::IFilter, WEAR_FILTER_IID)
