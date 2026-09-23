// ==============================================================================
// WeaR-studio RecordingManager Implementation
// ==============================================================================

#include "RecordingManager.h"

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QThread>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <deque>
#include <string>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/pixfmt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

namespace WeaR {

namespace {

const char* formatName(RecordingFormat format) {
    switch (format) {
        case RecordingFormat::MP4: return "mp4";
        case RecordingFormat::MKV: return "matroska";
        case RecordingFormat::FLV: return "flv";
    }
    return "matroska";
}

const char* extensionForFormat(RecordingFormat format) {
    switch (format) {
        case RecordingFormat::MP4: return "mp4";
        case RecordingFormat::MKV: return "mkv";
        case RecordingFormat::FLV: return "flv";
    }
    return "mkv";
}

const char* presetToString(EncoderPreset preset, bool nvenc) {
    if (nvenc) {
        switch (preset) {
            case EncoderPreset::UltraFast:
            case EncoderPreset::SuperFast:
                return "p1";
            case EncoderPreset::VeryFast:
            case EncoderPreset::Faster:
                return "p2";
            case EncoderPreset::Fast:
                return "p3";
            case EncoderPreset::Medium:
                return "p4";
            case EncoderPreset::Slow:
                return "p5";
            case EncoderPreset::Slower:
                return "p6";
            case EncoderPreset::VerySlow:
            case EncoderPreset::Placebo:
                return "p7";
        }
        return "p3";
    }

    switch (preset) {
        case EncoderPreset::UltraFast: return "ultrafast";
        case EncoderPreset::SuperFast: return "superfast";
        case EncoderPreset::VeryFast: return "veryfast";
        case EncoderPreset::Faster: return "faster";
        case EncoderPreset::Fast: return "fast";
        case EncoderPreset::Medium: return "medium";
        case EncoderPreset::Slow: return "slow";
        case EncoderPreset::Slower: return "slower";
        case EncoderPreset::VerySlow: return "veryslow";
        case EncoderPreset::Placebo: return "placebo";
    }
    return "fast";
}

QString ffError(int code) {
    char buffer[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(code, buffer, sizeof(buffer));
    return QString::fromUtf8(buffer);
}

struct RecordInput {
    enum class Type {
        Video,
        Audio
    };

    Type type = Type::Video;
    QImage video;
    AudioFrame audio;
};

} // namespace

class RecordingManager::Impl {
public:
    explicit Impl(RecordingManager* parent)
        : m_parent(parent) {}

    ~Impl() {
        stopRecording();
        cleanupOutput();
    }

    bool configure(const RecordingSettings& settings) {
        QMutexLocker lock(&m_stateMutex);
        if (m_accepting.load()) {
            qWarning() << "Cannot configure recording while active";
            return false;
        }

        m_settings = settings;
        return true;
    }

    RecordingSettings settings() const {
        QMutexLocker lock(&m_stateMutex);
        return m_settings;
    }

    bool startRecording(const QString& requestedPath) {
        {
            QMutexLocker lock(&m_stateMutex);
            if (m_accepting.load()) {
                return false;
            }
        }

        QString path = requestedPath.trimmed();
        RecordingSettings localSettings = settings();
        if (!path.isEmpty()) {
            localSettings.outputPath = path;
        }

        if (localSettings.outputPath.trimmed().isEmpty()) {
            emit m_parent->recordingError("Recording output path is empty.");
            setState(RecordingState::Error);
            return false;
        }

        m_settings = localSettings;

        if (!initializeOutput()) {
            cleanupOutput();
            setState(RecordingState::Error);
            emit m_parent->recordingError(m_lastError);
            return false;
        }

        {
            QMutexLocker queueLock(&m_queueMutex);
            m_queue.clear();
        }

        m_videoPts = 0;
        m_audioPts = 0;
        m_audioFifo.clear();
        m_overflowed = false;

        {
            QMutexLocker lock(&m_stateMutex);
            m_startedAt = Clock::now();
            m_pausedAt = Clock::time_point{};
            m_pausedAccumulated = std::chrono::milliseconds::zero();
        }

        m_accepting.store(true);
        m_paused.store(false);

        m_worker = std::thread(&Impl::workerLoop, this);

        setState(RecordingState::Recording);
        emit m_parent->recordingStarted(m_settings.outputPath);
        qDebug() << "Recording started:" << m_settings.outputPath;
        return true;
    }

    bool stopRecording() {
        const bool workerExists = m_worker.joinable();
        if (!workerExists && !m_accepting.load()) {
            if (state() == RecordingState::Error) {
                cleanupOutput();
            }
            return true;
        }

        m_accepting.store(false);
        m_queueCondition.wakeAll();

        if (m_worker.joinable()) {
            m_worker.join();
        }

        const QString path = m_settings.outputPath;
        const QString error = m_lastError;
        const bool overflowed = m_overflowed;

        cleanupOutput();

        {
            QMutexLocker lock(&m_stateMutex);
            m_startedAt = Clock::time_point{};
            m_pausedAt = Clock::time_point{};
            m_pausedAccumulated = std::chrono::milliseconds::zero();
        }

        m_paused.store(false);
        m_overflowed = false;

        if (!error.isEmpty() || overflowed) {
            const QString message = !error.isEmpty()
                ? error
                : "Recording queue overflowed; the recording was finalized safely.";
            emit m_parent->recordingError(message);
        }

        setState(RecordingState::Stopped);
        emit m_parent->recordingStopped(path);
        qDebug() << "Recording stopped:" << path;
        return true;
    }

    bool pauseRecording() {
        if (state() != RecordingState::Recording) {
            return false;
        }

        {
            QMutexLocker lock(&m_stateMutex);
            m_pausedAt = Clock::now();
        }

        m_paused.store(true);
        setState(RecordingState::Paused);
        qDebug() << "Recording paused";
        return true;
    }

    bool resumeRecording() {
        if (state() != RecordingState::Paused) {
            return false;
        }

        {
            QMutexLocker lock(&m_stateMutex);
            if (m_pausedAt != Clock::time_point{}) {
                m_pausedAccumulated += std::chrono::duration_cast<std::chrono::milliseconds>(
                    Clock::now() - m_pausedAt);
            }
            m_pausedAt = Clock::time_point{};
        }

        m_paused.store(false);
        setState(RecordingState::Recording);
        qDebug() << "Recording resumed";
        return true;
    }

    RecordingState state() const {
        QMutexLocker lock(&m_stateMutex);
        return m_state;
    }

    bool isRecording() const {
        return state() == RecordingState::Recording;
    }

    bool isPaused() const {
        return state() == RecordingState::Paused;
    }

    qint64 durationMs() const {
        QMutexLocker lock(&m_stateMutex);

        if (m_startedAt == Clock::time_point{}) {
            return 0;
        }

        const auto now = Clock::now();
        auto active = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - m_startedAt) - m_pausedAccumulated;

        if (m_pausedAt != Clock::time_point{}) {
            active -= std::chrono::duration_cast<std::chrono::milliseconds>(
                now - m_pausedAt);
        }

        return std::max<qint64>(0, active.count());
    }

    QString outputPath() const {
        QMutexLocker lock(&m_stateMutex);
        return m_settings.outputPath;
    }

    void pushFrame(const QImage& frame) {
        if (frame.isNull() || !m_accepting.load() || m_paused.load()) {
            return;
        }

        RecordInput input;
        input.type = RecordInput::Type::Video;
        input.video = frame;
        enqueue(std::move(input));
    }

    void pushAudioFrame(const AudioFrame& frame) {
        if (frame.samples.empty() || !m_accepting.load() || m_paused.load()) {
            return;
        }

        RecordInput input;
        input.type = RecordInput::Type::Audio;
        input.audio = frame;
        enqueue(std::move(input));
    }

private:
    using Clock = std::chrono::steady_clock;

    void setState(RecordingState next) {
        bool changed = false;
        {
            QMutexLocker lock(&m_stateMutex);
            if (m_state != next) {
                m_state = next;
                changed = true;
            }
        }

        if (changed) {
            emit m_parent->stateChanged(next);
        }
    }

    void enqueue(RecordInput&& input) {
        bool overflow = false;

        {
            QMutexLocker lock(&m_queueMutex);
            if (!m_accepting.load()) {
                return;
            }

            // A raw 1080p QImage can be several MiB. Keep this queue deliberately
            // small so a slow disk/encoder cannot consume unbounded RAM or affect
            // the streaming pipeline.
            constexpr std::size_t kMaxQueueItems = 24;
            if (m_queue.size() >= kMaxQueueItems) {
                m_accepting.store(false);
                m_overflowed = true;
                overflow = true;
            } else {
                m_queue.emplace_back(std::move(input));
            }
        }

        if (overflow) {
            qWarning() << "Recording queue overflow; finalizing current recording.";
        }
        m_queueCondition.wakeOne();
    }

    bool initializeOutput() {
        m_lastError.clear();

        const QString path = m_settings.outputPath;
        QFileInfo info(path);
        QDir parentDir = info.dir();
        if (!parentDir.exists() && !parentDir.mkpath(".")) {
            m_lastError = QString("Unable to create recording directory: %1")
                .arg(parentDir.absolutePath());
            return false;
        }

        const QByteArray format = QByteArray(formatName(m_settings.format));
        int ret = avformat_alloc_output_context2(
            &m_formatContext,
            nullptr,
            format.constData(),
            path.toUtf8().constData());

        if (ret < 0 || !m_formatContext) {
            m_lastError = QString("Failed to create output context for %1: %2")
                .arg(format.constData(), ffError(ret));
            return false;
        }

        if (!initializeVideoCodec()) {
            return false;
        }

        if (m_settings.audioEnabled && !initializeAudioCodec()) {
            return false;
        }

        m_videoStream = avformat_new_stream(m_formatContext, nullptr);
        if (!m_videoStream) {
            m_lastError = "Failed to allocate video output stream.";
            return false;
        }
        m_videoStream->time_base = m_videoCodec->time_base;
        ret = avcodec_parameters_from_context(m_videoStream->codecpar, m_videoCodec);
        if (ret < 0) {
            m_lastError = QString("Failed to copy video codec parameters: %1").arg(ffError(ret));
            return false;
        }

        if (m_audioCodec) {
            m_audioStream = avformat_new_stream(m_formatContext, nullptr);
            if (!m_audioStream) {
                m_lastError = "Failed to allocate audio output stream.";
                return false;
            }
            m_audioStream->time_base = m_audioCodec->time_base;
            ret = avcodec_parameters_from_context(m_audioStream->codecpar, m_audioCodec);
            if (ret < 0) {
                m_lastError = QString("Failed to copy audio codec parameters: %1")
                    .arg(ffError(ret));
                return false;
            }
        }

        if (!(m_formatContext->oformat->flags & AVFMT_NOFILE)) {
            ret = avio_open(&m_formatContext->pb, path.toUtf8().constData(), AVIO_FLAG_WRITE);
            if (ret < 0) {
                m_lastError = QString("Failed to open recording file: %1").arg(ffError(ret));
                return false;
            }
        }

        if (m_settings.format == RecordingFormat::MP4) {
            // Make the final MP4 web/player friendly without using a second
            // post-processing/remux stage.
            av_opt_set(m_formatContext->priv_data, "movflags", "+faststart", 0);
        }

        ret = avformat_write_header(m_formatContext, nullptr);
        if (ret < 0) {
            m_lastError = QString("Failed to write recording header: %1").arg(ffError(ret));
            return false;
        }

        m_headerWritten = true;
        return true;
    }

    const AVCodec* selectVideoCodec(bool& nvenc) const {
        nvenc = false;

        const auto requested = m_settings.encoderType;
        const bool allowHardware = requested == EncoderType::Auto ||
                                    requested == EncoderType::NVENC_H264 ||
                                    requested == EncoderType::NVENC_HEVC ||
                                    requested == EncoderType::AMF_H264 ||
                                    requested == EncoderType::AMF_HEVC ||
                                    requested == EncoderType::QSV_H264 ||
                                    requested == EncoderType::QSV_HEVC;

        if (allowHardware) {
            if (requested == EncoderType::NVENC_H264 || requested == EncoderType::NVENC_HEVC ||
                requested == EncoderType::Auto) {
                const char* name = requested == EncoderType::NVENC_HEVC
                    ? "hevc_nvenc" : "h264_nvenc";
                if (const AVCodec* codec = avcodec_find_encoder_by_name(name)) {
                    nvenc = true;
                    return codec;
                }
            }

            if (requested == EncoderType::AMF_H264 || requested == EncoderType::AMF_HEVC ||
                requested == EncoderType::Auto) {
                const char* name = requested == EncoderType::AMF_HEVC
                    ? "hevc_amf" : "h264_amf";
                if (const AVCodec* codec = avcodec_find_encoder_by_name(name)) {
                    return codec;
                }
            }

            if (requested == EncoderType::QSV_H264 || requested == EncoderType::QSV_HEVC ||
                requested == EncoderType::Auto) {
                const char* name = requested == EncoderType::QSV_HEVC
                    ? "hevc_qsv" : "h264_qsv";
                if (const AVCodec* codec = avcodec_find_encoder_by_name(name)) {
                    return codec;
                }
            }
        }

        if (requested == EncoderType::X265) {
            if (const AVCodec* codec = avcodec_find_encoder_by_name("libx265")) {
                return codec;
            }
        }

        if (const AVCodec* codec = avcodec_find_encoder_by_name("libx264")) {
            return codec;
        }

        return avcodec_find_encoder(AV_CODEC_ID_H264);
    }

    bool initializeVideoCodec() {
        bool nvenc = false;
        const AVCodec* codec = selectVideoCodec(nvenc);
        if (!codec) {
            m_lastError = "No supported H.264/HEVC encoder was found.";
            return false;
        }

        // FLV local files are kept on the broadly compatible H.264 + AAC path.
        if (m_settings.format == RecordingFormat::FLV &&
            codec->id == AV_CODEC_ID_HEVC) {
            codec = avcodec_find_encoder_by_name("h264_nvenc");
            nvenc = codec && codec->name && QByteArray(codec->name) == "h264_nvenc";
            if (!codec) {
                codec = avcodec_find_encoder_by_name("libx264");
                nvenc = false;
            }
            if (!codec) {
                m_lastError = "FLV recording requires an H.264 encoder.";
                return false;
            }
        }

        m_videoCodec = avcodec_alloc_context3(codec);
        if (!m_videoCodec) {
            m_lastError = "Failed to allocate video encoder context.";
            return false;
        }

        m_videoCodec->width = m_settings.width;
        m_videoCodec->height = m_settings.height;
        m_videoCodec->time_base = AVRational{
            std::max(1, m_settings.fpsDen),
            std::max(1, m_settings.fpsNum)};
        m_videoCodec->framerate = AVRational{
            std::max(1, m_settings.fpsNum),
            std::max(1, m_settings.fpsDen)};
        m_videoCodec->gop_size = std::max(1, m_settings.fpsNum * m_settings.keyframeInterval);
        m_videoCodec->max_b_frames = std::max(0, m_settings.bFrames);
        m_videoCodec->pix_fmt = nvenc ? AV_PIX_FMT_NV12 : AV_PIX_FMT_YUV420P;
        m_videoCodec->thread_count = m_settings.threads;

        switch (m_settings.rateControl) {
            case RateControlMode::CBR:
                m_videoCodec->bit_rate = static_cast<int64_t>(m_settings.videoBitrate) * 1000;
                m_videoCodec->rc_max_rate = static_cast<int64_t>(m_settings.videoBitrate) * 1000;
                m_videoCodec->rc_buffer_size = static_cast<int64_t>(m_settings.bufferSize) * 1000;
                break;
            case RateControlMode::VBR:
                m_videoCodec->bit_rate = static_cast<int64_t>(m_settings.videoBitrate) * 1000;
                m_videoCodec->rc_max_rate = static_cast<int64_t>(m_settings.maxVideoBitrate) * 1000;
                m_videoCodec->rc_buffer_size = static_cast<int64_t>(m_settings.bufferSize) * 1000;
                break;
            case RateControlMode::CRF:
                av_opt_set_int(m_videoCodec->priv_data, "crf", m_settings.crf, 0);
                break;
            case RateControlMode::CQP:
                av_opt_set_int(m_videoCodec->priv_data, "qp", m_settings.qp, 0);
                break;
        }

        av_opt_set(m_videoCodec->priv_data, "preset",
                   presetToString(m_settings.preset, nvenc), 0);

        if (m_formatContext->oformat->flags & AVFMT_GLOBALHEADER) {
            m_videoCodec->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        }

        int ret = avcodec_open2(m_videoCodec, codec, nullptr);
        if (ret < 0) {
            m_lastError = QString("Failed to open video encoder: %1").arg(ffError(ret));
            return false;
        }

        m_videoFrame = av_frame_alloc();
        if (!m_videoFrame) {
            m_lastError = "Failed to allocate video frame.";
            return false;
        }

        m_videoFrame->format = m_videoCodec->pix_fmt;
        m_videoFrame->width = m_settings.width;
        m_videoFrame->height = m_settings.height;
        ret = av_frame_get_buffer(m_videoFrame, 32);
        if (ret < 0) {
            m_lastError = QString("Failed to allocate video frame buffer: %1").arg(ffError(ret));
            return false;
        }

        m_swsContext = sws_getContext(
            m_settings.width, m_settings.height, AV_PIX_FMT_BGRA,
            m_settings.width, m_settings.height, m_videoCodec->pix_fmt,
            SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);
        if (!m_swsContext) {
            m_lastError = "Failed to initialize video color conversion.";
            return false;
        }

        m_packet = av_packet_alloc();
        if (!m_packet) {
            m_lastError = "Failed to allocate video packet.";
            return false;
        }

        return true;
    }

    bool initializeAudioCodec() {
        const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_AAC);
        if (!codec) {
            m_lastError = "AAC encoder is not available in the current FFmpeg build.";
            return false;
        }

        m_audioCodec = avcodec_alloc_context3(codec);
        if (!m_audioCodec) {
            m_lastError = "Failed to allocate audio encoder context.";
            return false;
        }

        m_audioCodec->bit_rate = static_cast<int64_t>(m_settings.audioBitrate) * 1000;
        m_audioCodec->sample_rate = m_settings.audioSampleRate;
        av_channel_layout_default(&m_audioCodec->ch_layout, m_settings.audioChannels);
        m_audioCodec->sample_fmt = AV_SAMPLE_FMT_FLTP;
        m_audioCodec->time_base = AVRational{1, m_settings.audioSampleRate};

        if (m_formatContext->oformat->flags & AVFMT_GLOBALHEADER) {
            m_audioCodec->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        }

        int ret = avcodec_open2(m_audioCodec, codec, nullptr);
        if (ret < 0) {
            m_lastError = QString("Failed to open AAC encoder: %1").arg(ffError(ret));
            return false;
        }

        const int frameSize = m_audioCodec->frame_size > 0 ? m_audioCodec->frame_size : 1024;

        m_audioFrame = av_frame_alloc();
        if (!m_audioFrame) {
            m_lastError = "Failed to allocate audio frame.";
            return false;
        }

        m_audioFrame->nb_samples = frameSize;
        m_audioFrame->format = m_audioCodec->sample_fmt;
        m_audioFrame->sample_rate = m_audioCodec->sample_rate;
        ret = av_channel_layout_copy(&m_audioFrame->ch_layout, &m_audioCodec->ch_layout);
        if (ret < 0) {
            m_lastError = QString("Failed to copy audio channel layout: %1").arg(ffError(ret));
            return false;
        }

        ret = av_frame_get_buffer(m_audioFrame, 0);
        if (ret < 0) {
            m_lastError = QString("Failed to allocate audio buffer: %1").arg(ffError(ret));
            return false;
        }

        m_audioPacket = av_packet_alloc();
        if (!m_audioPacket) {
            m_lastError = "Failed to allocate audio packet.";
            return false;
        }

        AVChannelLayout inputLayout;
        av_channel_layout_default(&inputLayout, m_settings.audioChannels);
        ret = swr_alloc_set_opts2(
            &m_swrContext,
            &m_audioCodec->ch_layout,
            m_audioCodec->sample_fmt,
            m_audioCodec->sample_rate,
            &inputLayout,
            AV_SAMPLE_FMT_FLT,
            m_settings.audioSampleRate,
            0,
            nullptr);
        av_channel_layout_uninit(&inputLayout);

        if (ret < 0 || !m_swrContext || swr_init(m_swrContext) < 0) {
            m_lastError = "Failed to initialize audio resampler.";
            return false;
        }

        return true;
    }

    void workerLoop() {
        for (;;) {
            RecordInput input;

            {
                QMutexLocker lock(&m_queueMutex);
                while (m_queue.empty() && m_accepting.load()) {
                    m_queueCondition.wait(&m_queueMutex, 100);
                }

                if (m_queue.empty() && !m_accepting.load()) {
                    break;
                }

                if (m_queue.empty()) {
                    continue;
                }

                input = std::move(m_queue.front());
                m_queue.pop_front();
            }

            if (input.type == RecordInput::Type::Video) {
                encodeVideo(input.video);
            } else {
                encodeAudio(input.audio);
            }
        }

        flushVideo();
        flushAudio();
        writeTrailer();
    }

    bool encodeVideo(const QImage& image) {
        if (!m_videoCodec || image.isNull()) {
            return false;
        }

        QImage converted = image;
        if (converted.format() != QImage::Format_ARGB32 &&
            converted.format() != QImage::Format_RGB32) {
            converted = converted.convertToFormat(QImage::Format_ARGB32);
        }

        if (converted.size() != QSize(m_settings.width, m_settings.height)) {
            converted = converted.scaled(
                m_settings.width,
                m_settings.height,
                Qt::IgnoreAspectRatio,
                Qt::FastTransformation);
        }

        if (av_frame_make_writable(m_videoFrame) < 0) {
            m_lastError = "Failed to make video frame writable.";
            return false;
        }

        m_videoFrame->pts = m_videoPts++;

        const uint8_t* source[1] = { converted.constBits() };
        const int sourceStride[1] = {
            static_cast<int>(converted.bytesPerLine())
        };

        sws_scale(
            m_swsContext,
            source,
            sourceStride,
            0,
            m_settings.height,
            m_videoFrame->data,
            m_videoFrame->linesize);

        int ret = avcodec_send_frame(m_videoCodec, m_videoFrame);
        if (ret < 0) {
            m_lastError = QString("Video encoder rejected frame: %1").arg(ffError(ret));
            return false;
        }

        while (ret >= 0) {
            ret = avcodec_receive_packet(m_videoCodec, m_packet);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                break;
            }
            if (ret < 0) {
                m_lastError = QString("Video encoder packet error: %1").arg(ffError(ret));
                break;
            }

            writePacket(m_packet, m_videoCodec->time_base, m_videoStream);
            av_packet_unref(m_packet);
        }

        return true;
    }

    bool encodeAudio(const AudioFrame& frame) {
        if (!m_audioCodec || !m_swrContext || frame.samples.empty()) {
            return true;
        }

        const int channels = std::max(1, m_settings.audioChannels);
        if (frame.channels != channels) {
            m_lastError = QString("Recording audio channel mismatch: mixer=%1 encoder=%2")
                .arg(frame.channels)
                .arg(channels);
            return false;
        }

        m_audioFifo.insert(
            m_audioFifo.end(),
            frame.samples.begin(),
            frame.samples.end());

        const int frameSize = m_audioFrame->nb_samples;
        const int floatsPerFrame = frameSize * channels;

        while (static_cast<int>(m_audioFifo.size()) >= floatsPerFrame) {
            if (!encodeAudioBlock(
                    m_audioFifo.data(),
                    frameSize)) {
                return false;
            }

            m_audioFifo.erase(
                m_audioFifo.begin(),
                m_audioFifo.begin() + floatsPerFrame);
        }

        return true;
    }

    bool encodeAudioBlock(const float* interleaved, int sampleCount) {
        if (av_frame_make_writable(m_audioFrame) < 0) {
            m_lastError = "Failed to make audio frame writable.";
            return false;
        }

        m_audioFrame->pts = m_audioPts;

        const uint8_t* input[1] = {
            reinterpret_cast<const uint8_t*>(interleaved)
        };

        const int converted = swr_convert(
            m_swrContext,
            m_audioFrame->data,
            sampleCount,
            input,
            sampleCount);

        if (converted <= 0) {
            m_lastError = "Audio resampling produced no samples.";
            return false;
        }

        m_audioPts += converted;

        int ret = avcodec_send_frame(m_audioCodec, m_audioFrame);
        if (ret < 0) {
            m_lastError = QString("Audio encoder rejected frame: %1").arg(ffError(ret));
            return false;
        }

        while (ret >= 0) {
            ret = avcodec_receive_packet(m_audioCodec, m_audioPacket);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                break;
            }
            if (ret < 0) {
                m_lastError = QString("Audio encoder packet error: %1").arg(ffError(ret));
                break;
            }

            writePacket(m_audioPacket, m_audioCodec->time_base, m_audioStream);
            av_packet_unref(m_audioPacket);
        }

        return true;
    }

    void flushVideo() {
        if (!m_videoCodec || !m_packet) {
            return;
        }

        int ret = avcodec_send_frame(m_videoCodec, nullptr);
        if (ret < 0) {
            return;
        }

        while (ret >= 0) {
            ret = avcodec_receive_packet(m_videoCodec, m_packet);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                break;
            }
            if (ret < 0) {
                break;
            }

            writePacket(m_packet, m_videoCodec->time_base, m_videoStream);
            av_packet_unref(m_packet);
        }
    }

    void flushAudio() {
        if (!m_audioCodec || !m_audioPacket) {
            return;
        }

        const int channels = std::max(1, m_settings.audioChannels);
        const int frameSize = m_audioFrame->nb_samples;
        const int floatsPerFrame = frameSize * channels;

        if (!m_audioFifo.empty()) {
            std::vector<float> padded(static_cast<std::size_t>(floatsPerFrame), 0.0f);
            const std::size_t count = std::min<std::size_t>(
                m_audioFifo.size(),
                padded.size());
            std::copy_n(m_audioFifo.begin(), count, padded.begin());
            encodeAudioBlock(padded.data(), frameSize);
            m_audioFifo.clear();
        }

        int ret = avcodec_send_frame(m_audioCodec, nullptr);
        if (ret < 0) {
            return;
        }

        while (ret >= 0) {
            ret = avcodec_receive_packet(m_audioCodec, m_audioPacket);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                break;
            }
            if (ret < 0) {
                break;
            }

            writePacket(m_audioPacket, m_audioCodec->time_base, m_audioStream);
            av_packet_unref(m_audioPacket);
        }
    }

    void writePacket(AVPacket* packet, AVRational sourceTimeBase, AVStream* stream) {
        if (!packet || !stream || !m_formatContext || !m_headerWritten) {
            return;
        }

        av_packet_rescale_ts(packet, sourceTimeBase, stream->time_base);
        packet->stream_index = stream->index;

        const int ret = av_interleaved_write_frame(m_formatContext, packet);
        if (ret < 0) {
            m_lastError = QString("Failed to mux recording packet: %1").arg(ffError(ret));
        }
    }

    void writeTrailer() {
        if (!m_formatContext || !m_headerWritten) {
            return;
        }

        const int ret = av_write_trailer(m_formatContext);
        if (ret < 0 && m_lastError.isEmpty()) {
            m_lastError = QString("Failed to finalize recording: %1").arg(ffError(ret));
        }
        m_headerWritten = false;
    }

    void cleanupOutput() {
        if (m_headerWritten && m_formatContext) {
            av_write_trailer(m_formatContext);
        }
        m_headerWritten = false;

        if (m_swsContext) {
            sws_freeContext(m_swsContext);
            m_swsContext = nullptr;
        }

        if (m_swrContext) {
            swr_free(&m_swrContext);
            m_swrContext = nullptr;
        }

        if (m_packet) {
            av_packet_free(&m_packet);
        }

        if (m_audioPacket) {
            av_packet_free(&m_audioPacket);
        }

        if (m_videoFrame) {
            av_frame_free(&m_videoFrame);
        }

        if (m_audioFrame) {
            av_frame_free(&m_audioFrame);
        }

        if (m_videoCodec) {
            avcodec_free_context(&m_videoCodec);
        }

        if (m_audioCodec) {
            avcodec_free_context(&m_audioCodec);
        }

        m_videoStream = nullptr;
        m_audioStream = nullptr;

        if (m_formatContext) {
            if (!(m_formatContext->oformat->flags & AVFMT_NOFILE) &&
                m_formatContext->pb) {
                avio_closep(&m_formatContext->pb);
            }
            avformat_free_context(m_formatContext);
            m_formatContext = nullptr;
        }

        {
            QMutexLocker lock(&m_queueMutex);
            m_queue.clear();
        }
        m_audioFifo.clear();
    }

    RecordingManager* m_parent = nullptr;

    mutable QMutex m_stateMutex;
    mutable QMutex m_queueMutex;
    QWaitCondition m_queueCondition;

    RecordingSettings m_settings;
    RecordingState m_state = RecordingState::Stopped;
    std::atomic<bool> m_accepting{false};
    std::atomic<bool> m_paused{false};

    Clock::time_point m_startedAt{};
    Clock::time_point m_pausedAt{};
    std::chrono::milliseconds m_pausedAccumulated{0};

    std::deque<RecordInput> m_queue;
    std::thread m_worker;

    bool m_overflowed = false;
    QString m_lastError;

    AVFormatContext* m_formatContext = nullptr;
    AVCodecContext* m_videoCodec = nullptr;
    AVCodecContext* m_audioCodec = nullptr;
    AVStream* m_videoStream = nullptr;
    AVStream* m_audioStream = nullptr;
    AVPacket* m_packet = nullptr;
    AVPacket* m_audioPacket = nullptr;
    AVFrame* m_videoFrame = nullptr;
    AVFrame* m_audioFrame = nullptr;
    SwsContext* m_swsContext = nullptr;
    SwrContext* m_swrContext = nullptr;

    std::vector<float> m_audioFifo;
    int64_t m_videoPts = 0;
    int64_t m_audioPts = 0;
    bool m_headerWritten = false;
};

RecordingManager& RecordingManager::instance() {
    static RecordingManager s_instance;
    return s_instance;
}

RecordingManager::RecordingManager(QObject* parent)
    : QObject(parent)
    , m_impl(std::make_unique<Impl>(this)) {}

RecordingManager::~RecordingManager() = default;

bool RecordingManager::configure(const RecordingSettings& settings) {
    return m_impl->configure(settings);
}

RecordingSettings RecordingManager::settings() const {
    return m_impl->settings();
}

bool RecordingManager::startRecording(const QString& outputPath) {
    return m_impl->startRecording(outputPath);
}

bool RecordingManager::stopRecording() {
    return m_impl->stopRecording();
}

bool RecordingManager::pauseRecording() {
    return m_impl->pauseRecording();
}

bool RecordingManager::resumeRecording() {
    return m_impl->resumeRecording();
}

RecordingState RecordingManager::state() const {
    return m_impl->state();
}

bool RecordingManager::isRecording() const {
    return m_impl->isRecording();
}

bool RecordingManager::isPaused() const {
    return m_impl->isPaused();
}

qint64 RecordingManager::durationMs() const {
    return m_impl->durationMs();
}

QString RecordingManager::outputPath() const {
    return m_impl->outputPath();
}

void RecordingManager::pushFrame(const QImage& frame) {
    m_impl->pushFrame(frame);
}

void RecordingManager::pushAudioFrame(const AudioFrame& frame) {
    m_impl->pushAudioFrame(frame);
}

} // namespace WeaR
