#include "ProjectPersistence.h"

#include "CaptureManager.h"
#include "IFilter.h"
#include "ISource.h"
#include "Scene.h"
#include "SceneItem.h"
#include "SceneManager.h"

#include <QColor>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QPointF>
#include <QSaveFile>
#include <QSet>
#include <QJsonValue>

#include <algorithm>
#include <cmath>

namespace WeaR {

namespace {

constexpr int kSchemaVersion = 1;
constexpr const char* kProfileSchema = "wear.profile";
constexpr const char* kSceneCollectionSchema = "wear.scene_collection";

QString encoderTypeName(EncoderType value) {
    return QString::number(static_cast<int>(value));
}

EncoderType encoderTypeFromJson(const QJsonValue& value) {
    return static_cast<EncoderType>(value.toInt(static_cast<int>(EncoderType::Auto)));
}

QString encoderPresetName(EncoderPreset value) {
    return QString::number(static_cast<int>(value));
}

EncoderPreset encoderPresetFromJson(const QJsonValue& value) {
    return static_cast<EncoderPreset>(value.toInt(static_cast<int>(EncoderPreset::Fast)));
}

QString rateControlName(RateControlMode value) {
    return QString::number(static_cast<int>(value));
}

RateControlMode rateControlFromJson(const QJsonValue& value) {
    return static_cast<RateControlMode>(value.toInt(static_cast<int>(RateControlMode::CBR)));
}

QString streamServiceName(StreamService value) {
    return QString::number(static_cast<int>(value));
}

StreamService streamServiceFromJson(const QJsonValue& value) {
    return static_cast<StreamService>(value.toInt(static_cast<int>(StreamService::Custom)));
}

QString recordingFormatName(RecordingFormat value) {
    return QString::number(static_cast<int>(value));
}

RecordingFormat recordingFormatFromJson(const QJsonValue& value) {
    return static_cast<RecordingFormat>(value.toInt(static_cast<int>(RecordingFormat::MKV)));
}

QJsonObject sourceConfigObject(const SourceConfig& config) {
    QJsonObject object;
    object.insert("resolution", QJsonObject{
        {"width", config.resolution.width()},
        {"height", config.resolution.height()}
    });
    object.insert("fps", config.fps);
    object.insert("useHardwareAcceleration", config.useHardwareAcceleration);
    object.insert("captureRegion", QJsonObject{
        {"x", config.captureRegion.x()},
        {"y", config.captureRegion.y()},
        {"width", config.captureRegion.width()},
        {"height", config.captureRegion.height()}
    });
    object.insert("deviceId", config.deviceId);
    return object;
}

bool readSourceConfigObject(
    const QJsonObject& object,
    SourceConfig& config,
    QString* error) {
    const QJsonObject resolution = object.value("resolution").toObject();
    const QJsonObject captureRegion = object.value("captureRegion").toObject();

    config.resolution = QSize(
        resolution.value("width").toInt(config.resolution.width()),
        resolution.value("height").toInt(config.resolution.height()));
    config.fps = object.value("fps").toDouble(config.fps);
    config.useHardwareAcceleration =
        object.value("useHardwareAcceleration")
            .toBool(config.useHardwareAcceleration);

    config.captureRegion = QRect(
        captureRegion.value("x").toInt(),
        captureRegion.value("y").toInt(),
        captureRegion.value("width").toInt(),
        captureRegion.value("height").toInt());

    config.deviceId = object.value("deviceId").toString();

    if (!config.resolution.isValid() || config.fps <= 0.0) {
        if (error) {
            *error = "Source configuration has invalid resolution/FPS.";
        }
        return false;
    }

    return true;
}

QJsonObject variantMapToJson(const QMap<QString, QVariant>& values) {
    QJsonObject object;
    for (auto it = values.cbegin(); it != values.cend(); ++it) {
        object.insert(it.key(), ProjectPersistence::variantToJson(it.value()));
    }
    return object;
}

bool variantMapFromJson(
    const QJsonObject& object,
    QMap<QString, QVariant>& values,
    QString* error) {
    for (auto it = object.cbegin(); it != object.cend(); ++it) {
        QVariant value;
        if (!ProjectPersistence::jsonToVariant(it.value(), value, error)) {
            return false;
        }
        values.insert(it.key(), value);
    }
    return true;
}

} // namespace

QString ProjectPersistence::formatError(
    const QString& context,
    const QString& detail) {
    return QStringLiteral("%1: %2").arg(context, detail);
}

QString ProjectPersistence::colorToString(const QColor& color) {
    return color.name(QColor::HexArgb);
}

QColor ProjectPersistence::colorFromJson(
    const QJsonValue& value,
    bool* ok) {
    const QColor color(value.toString());
    const bool valid = color.isValid();
    if (ok) *ok = valid;
    return color;
}

QJsonValue ProjectPersistence::variantToJson(const QVariant& value) {
    if (!value.isValid() || value.isNull()) {
        return QJsonValue();
    }

    if (value.canConvert<QColor>()) {
        const QColor color = value.value<QColor>();
        if (color.isValid()) {
            QJsonObject object;
            object.insert("__type", "color");
            object.insert("value", colorToString(color));
            return object;
        }
    }

    if (value.canConvert<QPointF>()) {
        const QPointF point = value.toPointF();
        QJsonObject object;
        object.insert("__type", "pointF");
        object.insert("x", point.x());
        object.insert("y", point.y());
        return object;
    }

    if (value.canConvert<QSizeF>()) {
        const QSizeF size = value.toSizeF();
        QJsonObject object;
        object.insert("__type", "sizeF");
        object.insert("width", size.width());
        object.insert("height", size.height());
        return object;
    }

    return QJsonValue::fromVariant(value);
}

bool ProjectPersistence::jsonToVariant(
    const QJsonValue& value,
    QVariant& output,
    QString* error) {
    if (value.isObject()) {
        const QJsonObject object = value.toObject();
        const QString type = object.value("__type").toString();

        if (type == "color") {
            bool ok = false;
            const QColor color = colorFromJson(object.value("value"), &ok);
            if (!ok) {
                if (error) *error = "Invalid QColor value.";
                return false;
            }
            output = color;
            return true;
        }

        if (type == "pointF") {
            output = QPointF(
                object.value("x").toDouble(),
                object.value("y").toDouble());
            return true;
        }

        if (type == "sizeF") {
            output = QSizeF(
                object.value("width").toDouble(),
                object.value("height").toDouble());
            return true;
        }
    }

    output = value.toVariant();
    return true;
}

QJsonObject ProjectPersistence::profileToJson(
    const StreamSettings& stream,
    const EncoderSettings& encoder,
    const RecordingSettings& recording,
    const QSize& outputResolution,
    double targetFps) {
    QJsonObject streamObject{
        {"url", stream.url},
        {"streamKey", stream.streamKey},
        {"service", static_cast<int>(stream.service)},
        {"connectTimeout", stream.connectTimeout},
        {"reconnectDelay", stream.reconnectDelay},
        {"maxReconnectAttempts", stream.maxReconnectAttempts},
        {"sendBufferSize", stream.sendBufferSize},
        {"videoWidth", stream.videoWidth},
        {"videoHeight", stream.videoHeight},
        {"videoFpsNum", stream.videoFpsNum},
        {"videoFpsDen", stream.videoFpsDen},
        {"videoBitrate", stream.videoBitrate},
        {"audioEnabled", stream.audioEnabled},
        {"audioSampleRate", stream.audioSampleRate},
        {"audioChannels", stream.audioChannels},
        {"audioBitrate", stream.audioBitrate}
    };

    QJsonObject encoderObject{
        {"width", encoder.width},
        {"height", encoder.height},
        {"fpsNum", encoder.fpsNum},
        {"fpsDen", encoder.fpsDen},
        {"bitrate", encoder.bitrate},
        {"maxBitrate", encoder.maxBitrate},
        {"bufferSize", encoder.bufferSize},
        {"crf", encoder.crf},
        {"qp", encoder.qp},
        {"encoderType", encoderTypeName(encoder.encoderType)},
        {"preset", encoderPresetName(encoder.preset)},
        {"rateControl", rateControlName(encoder.rateControl)},
        {"keyframeInterval", encoder.keyframeInterval},
        {"bFrames", encoder.bFrames},
        {"profile", encoder.profile},
        {"level", encoder.level},
        {"nvencLowLatency", encoder.nvencLowLatency},
        {"nvencZeroLatency", encoder.nvencZeroLatency},
        {"threads", encoder.threads},
        {"audioEnabled", encoder.audioEnabled},
        {"audioSampleRate", encoder.audioSampleRate},
        {"audioChannels", encoder.audioChannels},
        {"audioBitrate", encoder.audioBitrate}
    };

    QJsonObject recordingObject{
        {"outputPath", recording.outputPath},
        {"format", recordingFormatName(recording.format)},
        {"width", recording.width},
        {"height", recording.height},
        {"fpsNum", recording.fpsNum},
        {"fpsDen", recording.fpsDen},
        {"videoBitrate", recording.videoBitrate},
        {"maxVideoBitrate", recording.maxVideoBitrate},
        {"bufferSize", recording.bufferSize},
        {"crf", recording.crf},
        {"qp", recording.qp},
        {"encoderType", encoderTypeName(recording.encoderType)},
        {"preset", encoderPresetName(recording.preset)},
        {"rateControl", rateControlName(recording.rateControl)},
        {"keyframeInterval", recording.keyframeInterval},
        {"bFrames", recording.bFrames},
        {"threads", recording.threads},
        {"audioEnabled", recording.audioEnabled},
        {"audioSampleRate", recording.audioSampleRate},
        {"audioChannels", recording.audioChannels},
        {"audioBitrate", recording.audioBitrate}
    };

    QJsonObject root{
        {"schema", kProfileSchema},
        {"version", kSchemaVersion},
        {"output", QJsonObject{
            {"width", outputResolution.width()},
            {"height", outputResolution.height()},
            {"fps", targetFps}
        }},
        {"stream", streamObject},
        {"encoder", encoderObject},
        {"recording", recordingObject}
    };

    return root;
}

bool ProjectPersistence::profileFromJson(
    const QJsonObject& root,
    StreamSettings& stream,
    EncoderSettings& encoder,
    RecordingSettings& recording,
    QSize& outputResolution,
    double& targetFps,
    QString* error) {
    if (root.value("schema").toString() != QString::fromUtf8(kProfileSchema)) {
        if (error) *error = "Unsupported profile schema.";
        return false;
    }

    if (root.value("version").toInt(-1) != kSchemaVersion) {
        if (error) *error = "Unsupported profile version.";
        return false;
    }

    const QJsonObject output = root.value("output").toObject();
    outputResolution = QSize(
        output.value("width").toInt(outputResolution.width()),
        output.value("height").toInt(outputResolution.height()));
    targetFps = output.value("fps").toDouble(targetFps);
    if (!outputResolution.isValid() || targetFps <= 0.0) {
        if (error) *error = "Profile output resolution/FPS is invalid.";
        return false;
    }

    const QJsonObject streamObject = root.value("stream").toObject();
    stream.url = streamObject.value("url").toString();
    stream.streamKey = streamObject.value("streamKey").toString();
    stream.service = streamServiceFromJson(streamObject.value("service"));
    stream.connectTimeout = streamObject.value("connectTimeout").toInt(stream.connectTimeout);
    stream.reconnectDelay = streamObject.value("reconnectDelay").toInt(stream.reconnectDelay);
    stream.maxReconnectAttempts = streamObject.value("maxReconnectAttempts").toInt(stream.maxReconnectAttempts);
    stream.sendBufferSize = streamObject.value("sendBufferSize").toInt(stream.sendBufferSize);
    stream.videoWidth = streamObject.value("videoWidth").toInt(stream.videoWidth);
    stream.videoHeight = streamObject.value("videoHeight").toInt(stream.videoHeight);
    stream.videoFpsNum = streamObject.value("videoFpsNum").toInt(stream.videoFpsNum);
    stream.videoFpsDen = streamObject.value("videoFpsDen").toInt(stream.videoFpsDen);
    stream.videoBitrate = streamObject.value("videoBitrate").toInt(stream.videoBitrate);
    stream.audioEnabled = streamObject.value("audioEnabled").toBool(stream.audioEnabled);
    stream.audioSampleRate = streamObject.value("audioSampleRate").toInt(stream.audioSampleRate);
    stream.audioChannels = streamObject.value("audioChannels").toInt(stream.audioChannels);
    stream.audioBitrate = streamObject.value("audioBitrate").toInt(stream.audioBitrate);

    const QJsonObject encoderObject = root.value("encoder").toObject();
    encoder.width = encoderObject.value("width").toInt(encoder.width);
    encoder.height = encoderObject.value("height").toInt(encoder.height);
    encoder.fpsNum = encoderObject.value("fpsNum").toInt(encoder.fpsNum);
    encoder.fpsDen = encoderObject.value("fpsDen").toInt(encoder.fpsDen);
    encoder.bitrate = encoderObject.value("bitrate").toInt(encoder.bitrate);
    encoder.maxBitrate = encoderObject.value("maxBitrate").toInt(encoder.maxBitrate);
    encoder.bufferSize = encoderObject.value("bufferSize").toInt(encoder.bufferSize);
    encoder.crf = encoderObject.value("crf").toInt(encoder.crf);
    encoder.qp = encoderObject.value("qp").toInt(encoder.qp);
    encoder.encoderType = encoderTypeFromJson(encoderObject.value("encoderType"));
    encoder.preset = encoderPresetFromJson(encoderObject.value("preset"));
    encoder.rateControl = rateControlFromJson(encoderObject.value("rateControl"));
    encoder.keyframeInterval = encoderObject.value("keyframeInterval").toInt(encoder.keyframeInterval);
    encoder.bFrames = encoderObject.value("bFrames").toInt(encoder.bFrames);
    encoder.profile = encoderObject.value("profile").toString(encoder.profile);
    encoder.level = encoderObject.value("level").toString(encoder.level);
    encoder.nvencLowLatency = encoderObject.value("nvencLowLatency").toBool(encoder.nvencLowLatency);
    encoder.nvencZeroLatency = encoderObject.value("nvencZeroLatency").toBool(encoder.nvencZeroLatency);
    encoder.threads = encoderObject.value("threads").toInt(encoder.threads);
    encoder.audioEnabled = encoderObject.value("audioEnabled").toBool(encoder.audioEnabled);
    encoder.audioSampleRate = encoderObject.value("audioSampleRate").toInt(encoder.audioSampleRate);
    encoder.audioChannels = encoderObject.value("audioChannels").toInt(encoder.audioChannels);
    encoder.audioBitrate = encoderObject.value("audioBitrate").toInt(encoder.audioBitrate);

    const QJsonObject recordingObject = root.value("recording").toObject();
    recording.outputPath = recordingObject.value("outputPath").toString();
    recording.format = recordingFormatFromJson(recordingObject.value("format"));
    recording.width = recordingObject.value("width").toInt(recording.width);
    recording.height = recordingObject.value("height").toInt(recording.height);
    recording.fpsNum = recordingObject.value("fpsNum").toInt(recording.fpsNum);
    recording.fpsDen = recordingObject.value("fpsDen").toInt(recording.fpsDen);
    recording.videoBitrate = recordingObject.value("videoBitrate").toInt(recording.videoBitrate);
    recording.maxVideoBitrate = recordingObject.value("maxVideoBitrate").toInt(recording.maxVideoBitrate);
    recording.bufferSize = recordingObject.value("bufferSize").toInt(recording.bufferSize);
    recording.crf = recordingObject.value("crf").toInt(recording.crf);
    recording.qp = recordingObject.value("qp").toInt(recording.qp);
    recording.encoderType = encoderTypeFromJson(recordingObject.value("encoderType"));
    recording.preset = encoderPresetFromJson(recordingObject.value("preset"));
    recording.rateControl = rateControlFromJson(recordingObject.value("rateControl"));
    recording.keyframeInterval = recordingObject.value("keyframeInterval").toInt(recording.keyframeInterval);
    recording.bFrames = recordingObject.value("bFrames").toInt(recording.bFrames);
    recording.threads = recordingObject.value("threads").toInt(recording.threads);
    recording.audioEnabled = recordingObject.value("audioEnabled").toBool(recording.audioEnabled);
    recording.audioSampleRate = recordingObject.value("audioSampleRate").toInt(recording.audioSampleRate);
    recording.audioChannels = recordingObject.value("audioChannels").toInt(recording.audioChannels);
    recording.audioBitrate = recordingObject.value("audioBitrate").toInt(recording.audioBitrate);

    return true;
}

bool ProjectPersistence::saveProfile(
    const QString& path,
    const StreamSettings& stream,
    const EncoderSettings& encoder,
    const RecordingSettings& recording,
    const QSize& outputResolution,
    double targetFps,
    QString* error) {
    const QJsonDocument document(profileToJson(
        stream, encoder, recording, outputResolution, targetFps));

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error) *error = formatError("Save profile", file.errorString());
        return false;
    }

    file.write(document.toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        if (error) *error = formatError("Save profile", file.errorString());
        return false;
    }

    return true;
}

bool ProjectPersistence::loadProfile(
    const QString& path,
    StreamSettings& stream,
    EncoderSettings& encoder,
    RecordingSettings& recording,
    QSize& outputResolution,
    double& targetFps,
    QString* error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error) *error = formatError("Load profile", file.errorString());
        return false;
    }

    QJsonParseError parseError{};
    const QJsonDocument document =
        QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError ||
        !document.isObject()) {
        if (error) {
            *error = formatError(
                "Load profile", parseError.errorString());
        }
        return false;
    }

    StreamSettings nextStream = stream;
    EncoderSettings nextEncoder = encoder;
    RecordingSettings nextRecording = recording;
    QSize nextResolution = outputResolution;
    double nextFps = targetFps;
    if (!profileFromJson(
            document.object(),
            nextStream, nextEncoder, nextRecording,
            nextResolution, nextFps,
            error)) {
        return false;
    }

    stream = nextStream;
    encoder = nextEncoder;
    recording = nextRecording;
    outputResolution = nextResolution;
    targetFps = nextFps;
    return true;
}

QJsonObject ProjectPersistence::pointToJson(const QPointF& point) {
    return QJsonObject{{"x", point.x()}, {"y", point.y()}};
}

QJsonObject ProjectPersistence::sizeFToJson(const QSizeF& size) {
    return QJsonObject{{"width", size.width()}, {"height", size.height()}};
}

QJsonObject ProjectPersistence::transformToJson(
    const ItemTransform& transform) {
    return QJsonObject{
        {"position", pointToJson(transform.position)},
        {"size", sizeFToJson(transform.size)},
        {"rotation", transform.rotation},
        {"scale", pointToJson(transform.scale)},
        {"anchor", pointToJson(transform.anchor)},
        {"opacity", transform.opacity},
        {"flipH", transform.flipH},
        {"flipV", transform.flipV}
    };
}

bool ProjectPersistence::transformFromJson(
    const QJsonObject& object,
    ItemTransform& transform,
    QString* error) {
    const auto readPoint = [&object](
        const char* key, QPointF& point) -> bool {
        if (!object.value(key).isObject()) return false;
        const QJsonObject value = object.value(key).toObject();
        point = QPointF(
            value.value("x").toDouble(point.x()),
            value.value("y").toDouble(point.y()));
        return true;
    };

    const auto readSize = [&object](
        const char* key, QSizeF& size) -> bool {
        if (!object.value(key).isObject()) return false;
        const QJsonObject value = object.value(key).toObject();
        size = QSizeF(
            value.value("width").toDouble(size.width()),
            value.value("height").toDouble(size.height()));
        return true;
    };

    if (!readPoint("position", transform.position) ||
        !readSize("size", transform.size) ||
        !readPoint("scale", transform.scale) ||
        !readPoint("anchor", transform.anchor)) {
        if (error) *error = "Scene item transform is malformed.";
        return false;
    }

    transform.rotation = object.value("rotation").toDouble(transform.rotation);
    transform.opacity = std::clamp(
        object.value("opacity").toDouble(transform.opacity), 0.0, 1.0);
    transform.flipH = object.value("flipH").toBool(transform.flipH);
    transform.flipV = object.value("flipV").toBool(transform.flipV);
    return true;
}

QJsonObject ProjectPersistence::filterToJson(const IFilter& filter) {
    return QJsonObject{
        {"pluginId", filter.info().id},
        {"parameters", variantMapToJson(filter.allParameters())}
    };
}

QJsonObject ProjectPersistence::sourceConfigToJson(
    const ISource& source,
    const SourceConfig& config) {
    QJsonObject object{
        {"pluginId", source.info().id},
        {"config", sourceConfigObject(config)}
    };

    if (const auto* capture = dynamic_cast<const CaptureManager*>(&source)) {
        const CaptureTarget target = capture->currentTarget();
        object.insert("captureTargetId", target.id);
        object.insert("showCursor", capture->showCursor());
        object.insert("showBorder", capture->showBorder());
    }

    return object;
}

bool ProjectPersistence::sourceConfigFromJson(
    const QJsonObject& object,
    SourceConfig& config,
    QString* error) {
    return readSourceConfigObject(object.value("config").toObject(), config, error);
}

QJsonObject ProjectPersistence::itemToJson(const SceneItem& item) {
    QJsonObject object{
        {"id", item.id().toString(QUuid::WithoutBraces)},
        {"name", item.name()},
        {"visible", item.isVisible()},
        {"locked", item.isLocked()},
        {"blendMode", static_cast<int>(item.blendMode())},
        {"transform", transformToJson(item.transform())}
    };

    if (item.source()) {
        object.insert("source", sourceConfigToJson(
            *item.source(), item.source()->config()));
    }

    if (item.filter()) {
        object.insert("filter", filterToJson(*item.filter()));
    }

    return object;
}

QJsonObject ProjectPersistence::sceneToJson(const Scene& scene) {
    QJsonArray items;
    for (SceneItem* item : scene.items()) {
        if (item) items.append(itemToJson(*item));
    }

    return QJsonObject{
        {"id", scene.id().toString(QUuid::WithoutBraces)},
        {"name", scene.name()},
        {"resolution", QJsonObject{
            {"width", scene.resolution().width()},
            {"height", scene.resolution().height()}
        }},
        {"backgroundColor", colorToString(scene.backgroundColor())},
        {"items", items}
    };
}

bool ProjectPersistence::saveSceneCollection(
    const QString& path,
    const SceneManager& sceneManager,
    QString* error) {
    QJsonArray scenes;
    for (Scene* scene : sceneManager.scenes()) {
        if (scene) scenes.append(sceneToJson(*scene));
    }

    if (scenes.isEmpty()) {
        if (error) *error = "Scene collection is empty.";
        return false;
    }

    const Scene* activeScene = sceneManager.activeScene();
    const QString activeId = activeScene
        ? activeScene->id().toString(QUuid::WithoutBraces)
        : QString();

    const QJsonObject root{
        {"schema", kSceneCollectionSchema},
        {"version", kSchemaVersion},
        {"activeSceneId", activeId},
        {"scenes", scenes}
    };

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error) *error = formatError("Save scene collection", file.errorString());
        return false;
    }

    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        if (error) *error = formatError("Save scene collection", file.errorString());
        return false;
    }

    return true;
}

bool ProjectPersistence::loadSceneCollection(
    const QString& path,
    SceneManager& sceneManager,
    const SourceResolver& sourceResolver,
    const FilterResolver& filterResolver,
    QString* error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error) *error = formatError("Load scene collection", file.errorString());
        return false;
    }

    QJsonParseError parseError{};
    const QJsonDocument document =
        QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError ||
        !document.isObject()) {
        if (error) {
            *error = formatError(
                "Load scene collection", parseError.errorString());
        }
        return false;
    }

    const QJsonObject root = document.object();
    if (root.value("schema").toString() != QString::fromUtf8(kSceneCollectionSchema) ||
        root.value("version").toInt(-1) != kSchemaVersion) {
        if (error) {
            *error = "Unsupported scene collection schema/version.";
        }
        return false;
    }

    const QJsonArray scenesJson = root.value("scenes").toArray();
    if (scenesJson.isEmpty()) {
        if (error) *error = "Scene collection contains no scenes.";
        return false;
    }

    struct ValidatedItem {
        QUuid id;
        QString name;
        bool visible = true;
        bool locked = false;
        BlendMode blendMode = BlendMode::Normal;
        ItemTransform transform;
        ISource* source = nullptr;
        QJsonObject sourceObject;
        IFilter* filter = nullptr;
        QJsonObject filterObject;
    };

    struct ValidatedScene {
        QUuid id;
        QString name;
        QSize resolution;
        QColor background;
        QList<ValidatedItem> items;
    };

    QList<ValidatedScene> validatedScenes;
    QMap<QString, QJsonObject> sourceFingerprints;
    QMap<QString, QJsonObject> filterFingerprints;
    QSet<QUuid> sceneIds;
    QSet<QUuid> itemIds;

    // Validate all source/filter identities and data before mutating the
    // current scene graph.
    for (const QJsonValue& sceneValue : scenesJson) {
        const QJsonObject sceneObject = sceneValue.toObject();
        const QUuid sceneId(sceneObject.value("id").toString());
        if (sceneId.isNull() || sceneIds.contains(sceneId)) {
            if (error) {
                *error = "Scene collection contains a missing or duplicate scene UUID.";
            }
            return false;
        }
        sceneIds.insert(sceneId);

        ValidatedScene validated;
        validated.id = sceneId;
        validated.name = sceneObject.value("name").toString();
        const QJsonObject resolution = sceneObject.value("resolution").toObject();
        validated.resolution = QSize(
            resolution.value("width").toInt(),
            resolution.value("height").toInt());

        bool colorOk = false;
        validated.background =
            colorFromJson(sceneObject.value("backgroundColor"), &colorOk);
        if (validated.name.isEmpty() ||
            !validated.resolution.isValid() ||
            !colorOk) {
            if (error) *error = "Scene collection contains malformed scene data.";
            return false;
        }

        const QJsonArray items = sceneObject.value("items").toArray();
        for (const QJsonValue& itemValue : items) {
            const QJsonObject itemObject = itemValue.toObject();

            ValidatedItem item;
            item.id = QUuid(itemObject.value("id").toString());
            item.name = itemObject.value("name").toString();
            item.visible = itemObject.value("visible").toBool(true);
            item.locked = itemObject.value("locked").toBool(false);
            item.blendMode = static_cast<BlendMode>(
                itemObject.value("blendMode").toInt(0));

            if (item.id.isNull() || itemIds.contains(item.id) ||
                item.name.isEmpty() ||
                item.blendMode < BlendMode::Normal ||
                item.blendMode > BlendMode::Additive) {
                if (error) {
                    *error = "Scene collection contains a missing or duplicate item UUID.";
                }
                return false;
            }
            itemIds.insert(item.id);
            if (!transformFromJson(
                    itemObject.value("transform").toObject(),
                    item.transform,
                    error)) {
                return false;
            }

            const QJsonObject sourceObject = itemObject.value("source").toObject();
            const QString sourceId = sourceObject.value("pluginId").toString();
            if (sourceId.isEmpty() || !sourceResolver) {
                if (error) *error = "Scene item is missing a source plugin ID.";
                return false;
            }

            item.source = sourceResolver(sourceId);
            if (!item.source) {
                if (error) {
                    *error = QStringLiteral(
                        "Source plugin is unavailable: %1").arg(sourceId);
                }
                return false;
            }

            SourceConfig sourceConfig = item.source->config();
            if (!sourceConfigFromJson(sourceObject, sourceConfig, error)) {
                return false;
            }

            if (sourceFingerprints.contains(sourceId)) {
                if (sourceFingerprints.value(sourceId) != sourceObject) {
                    if (error) {
                        *error = QStringLiteral(
                            "Source plugin '%1' is shared by multiple items with conflicting configuration.")
                            .arg(sourceId);
                    }
                    return false;
                }
            } else {
                sourceFingerprints.insert(sourceId, sourceObject);
            }

            if (const auto* capture = dynamic_cast<const CaptureManager*>(item.source)) {
                Q_UNUSED(capture);
            }

            item.sourceObject = sourceObject;

            if (itemObject.contains("filter")) {
                item.filterObject = itemObject.value("filter").toObject();
                const QString filterId =
                    item.filterObject.value("pluginId").toString();
                if (filterId.isEmpty()) {
                    if (error) *error = "Scene item filter is missing a plugin ID.";
                    return false;
                }

                item.filter = filterResolver ? filterResolver(filterId) : nullptr;
                if (!item.filter) {
                    if (error) {
                        *error = QStringLiteral(
                            "Filter is unavailable: %1").arg(filterId);
                    }
                    return false;
                }

                if (filterFingerprints.contains(filterId)) {
                    if (filterFingerprints.value(filterId) != item.filterObject) {
                        if (error) {
                            *error = QStringLiteral(
                                "Filter plugin '%1' is shared by multiple items with conflicting parameters.")
                                .arg(filterId);
                        }
                        return false;
                    }
                } else {
                    filterFingerprints.insert(filterId, item.filterObject);
                }
            }

            validated.items.append(item);
        }

        validatedScenes.append(validated);
    }

    // Mutate only after the entire collection is validated.
    QList<Scene*> existing = sceneManager.scenes();
    for (int index = existing.size() - 1; index >= 1; --index) {
        sceneManager.removeScene(existing.at(index));
    }

    QList<Scene*> current = sceneManager.scenes();
    Scene* first = current.isEmpty()
        ? sceneManager.createScene()
        : current.first();

    first->clear();
    first->setId(validatedScenes.first().id);
    first->setName(validatedScenes.first().name);
    first->setResolution(validatedScenes.first().resolution);
    first->setBackgroundColor(validatedScenes.first().background);

    const auto populate = [](
        Scene* scene,
        const ValidatedScene& validated,
        QString* loadError) -> bool {
        for (const ValidatedItem& validatedItem : validated.items) {
            SourceConfig sourceConfig = validatedItem.source->config();
            if (!ProjectPersistence::sourceConfigFromJson(
                    validatedItem.sourceObject, sourceConfig, loadError)) {
                return false;
            }

            if (auto* capture = dynamic_cast<CaptureManager*>(validatedItem.source)) {
                const QString targetId =
                    validatedItem.sourceObject.value("captureTargetId").toString();

                if (!targetId.isEmpty()) {
                    bool foundTarget = false;
                    for (const CaptureTarget& target :
                         capture->enumerateTargets()) {
                        if (target.id == targetId) {
                            if (!capture->setTarget(target)) {
                                if (loadError) {
                                    *loadError = QStringLiteral(
                                        "Failed to restore capture target '%1'.")
                                        .arg(targetId);
                                }
                                return false;
                            }
                            foundTarget = true;
                            break;
                        }
                    }
                    if (!foundTarget) {
                        if (loadError) {
                            *loadError = QStringLiteral(
                                "Capture target is unavailable: %1")
                                .arg(targetId);
                        }
                        return false;
                    }
                }

                if (validatedItem.sourceObject.contains("showCursor")) {
                    capture->setShowCursor(
                        validatedItem.sourceObject.value("showCursor").toBool());
                }
                if (validatedItem.sourceObject.contains("showBorder")) {
                    capture->setShowBorder(
                        validatedItem.sourceObject.value("showBorder").toBool());
                }
            }

            if (!validatedItem.source->configure(sourceConfig)) {
                if (loadError) {
                    *loadError = QStringLiteral(
                        "Failed to configure source: %1")
                        .arg(validatedItem.source->name());
                }
                return false;
            }
            if (!validatedItem.source->isRunning() &&
                !validatedItem.source->start()) {
                if (loadError) {
                    *loadError = QStringLiteral(
                        "Failed to start source: %1 (%2)")
                        .arg(validatedItem.source->name(),
                             validatedItem.source->lastError());
                }
                return false;
            }

            auto* sceneItem = new SceneItem(
                validatedItem.name,
                validatedItem.source,
                scene);
            sceneItem->setId(validatedItem.id);
            sceneItem->setTransform(validatedItem.transform);
            sceneItem->setVisible(validatedItem.visible);
            sceneItem->setLocked(validatedItem.locked);
            sceneItem->setBlendMode(validatedItem.blendMode);

            if (validatedItem.filter) {
                sceneItem->setFilter(validatedItem.filter);
                QMap<QString, QVariant> params;
                QString parameterError;
                if (!variantMapFromJson(
                        validatedItem.filterObject.value("parameters").toObject(),
                        params,
                        &parameterError)) {
                    if (loadError) *loadError = parameterError;
                    return false;
                }
                validatedItem.filter->setAllParameters(params);
            }

            scene->addItem(sceneItem);
        }

        return true;
    };

    if (!populate(first, validatedScenes.first(), error)) {
        return false;
    }

    for (int index = 1; index < validatedScenes.size(); ++index) {
        Scene* scene = sceneManager.createScene(validatedScenes.at(index).name);
        scene->setId(validatedScenes.at(index).id);
        scene->setResolution(validatedScenes.at(index).resolution);
        scene->setBackgroundColor(validatedScenes.at(index).background);
        if (!populate(scene, validatedScenes.at(index), error)) {
            return false;
        }
    }

    const QUuid activeId(root.value("activeSceneId").toString());
    Scene* active = sceneManager.sceneById(activeId);
    sceneManager.setActiveScene(active ? active : first);
    return true;
}

} // namespace WeaR
