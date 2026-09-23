#pragma once

#include "EncoderManager.h"
#include "RecordingManager.h"
#include "StreamManager.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

#include <functional>

namespace WeaR {

class Scene;
class SceneItem;
class SceneManager;
class ISource;
class IFilter;

struct ProfileData {
    StreamSettings stream;
    EncoderSettings encoder;
    RecordingSettings recording;
    QSize outputResolution{1920, 1080};
    double targetFps = 60.0;
    bool encoderOutputEnabled = true;
    bool recordingOutputEnabled = false;
    QList<GlobalHotkeyBinding> hotkeys;
    QMap<QString, QVariant> audioTrackSettings;
};

class ProjectPersistence final {
public:
    using SourceResolver = std::function<ISource*(const QString& pluginId)>;
    using FilterResolver = std::function<IFilter*(const QString& filterId)>;

    static bool saveProfile(
        const QString& path,
        const StreamSettings& stream,
        const EncoderSettings& encoder,
        const RecordingSettings& recording,
        const QSize& outputResolution,
        double targetFps,
        bool encoderOutputEnabled,
        bool recordingOutputEnabled,
        QString* error = nullptr);

    static bool loadProfile(
        const QString& path,
        StreamSettings& stream,
        EncoderSettings& encoder,
        RecordingSettings& recording,
        QSize& outputResolution,
        double& targetFps,
        bool& encoderOutputEnabled,
        bool& recordingOutputEnabled,
        QString* error = nullptr);

    static bool saveSceneCollection(
        const QString& path,
        const SceneManager& sceneManager,
        QString* error = nullptr);

    static bool loadSceneCollection(
        const QString& path,
        SceneManager& sceneManager,
        const SourceResolver& sourceResolver,
        const FilterResolver& filterResolver,
        QString* error = nullptr);

private:
    static QJsonObject sourceConfigToJson(
        const ISource& source,
        const SourceConfig& config);

    static bool sourceConfigFromJson(
        const QJsonObject& object,
        SourceConfig& config,
        QString* error);

    static QJsonObject sceneToJson(const Scene& scene);
    static QJsonObject itemToJson(const SceneItem& item);
    static QJsonObject filterToJson(const IFilter& filter);

    static bool jsonToVariant(
        const QJsonValue& value,
        QVariant& output,
        QString* error);

    static QJsonValue variantToJson(const QVariant& value);

    static QJsonObject pointToJson(const QPointF& point);
    static QJsonObject sizeFToJson(const QSizeF& size);
    static QJsonObject transformToJson(const ItemTransform& transform);
    static bool transformFromJson(
        const QJsonObject& object,
        ItemTransform& transform,
        QString* error);

    static QString colorToString(const QColor& color);
    static QColor colorFromJson(
        const QJsonValue& value,
        bool* ok = nullptr);

    static QString formatError(const QString& context, const QString& detail);

    static QJsonObject profileToJson(
        const StreamSettings& stream,
        const EncoderSettings& encoder,
        const RecordingSettings& recording,
        const QSize& outputResolution,
        double targetFps,
        bool encoderOutputEnabled,
        bool recordingOutputEnabled);

    static bool profileFromJson(
        const QJsonObject& root,
        StreamSettings& stream,
        EncoderSettings& encoder,
        RecordingSettings& recording,
        QSize& outputResolution,
        double& targetFps,
        bool& encoderOutputEnabled,
        bool& recordingOutputEnabled,
        QString* error);
};

} // namespace WeaR
