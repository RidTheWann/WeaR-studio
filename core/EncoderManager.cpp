// ==============================================================================
// WeaR-studio EncoderManager Implementation
// Hardware-accelerated video encoding using FFmpeg (NVENC/AMF/libx264)
// ==============================================================================

#include "EncoderManager.h"

#include <QDebug>
#include <QDateTime>
#include <QElapsedTimer>

// FFmpeg headers (C linkage)
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
#include <libavutil/pixfmt.h>
#include <libavutil/channel_layout.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
}

#include <chrono>
#include <deque>

namespace WeaR {

// ==============================================================================
// Helper: Convert EncoderPreset to FFmpeg preset string
// ==============================================================================
static const char* presetToString(EncoderPreset preset, bool isNvenc) {
    if (isNvenc) {
        // NVENC presets
        switch (preset) {
            case EncoderPreset::UltraFast:
            case EncoderPreset::SuperFast:
                return "p1";  // Fastest
            case EncoderPreset::VeryFast:
            case EncoderPreset::Faster:
                return "p2";
            case EncoderPreset::Fast:
                return "p3";
            case EncoderPreset::Medium:
                return "p4";  // Default
            case EncoderPreset::Slow:
                return "p5";
            case EncoderPreset::Slower:
                return "p6";
            case EncoderPreset::VerySlow:
            case EncoderPreset::Placebo:
                return "p7";  // Slowest
            default:
                return "p4";
        }
    } else {
        // x264 presets
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
            default: return "medium";
        }
    }
}

// ==============================================================================
// Frame wrapper for queue
// ==============================================================================
struct QueuedFrame {
    AVFrame* frame = nullptr;
    int64_t pts = 0;
    
    QueuedFrame() = default;
    QueuedFrame(AVFrame* f, int64_t p) : frame(f), pts(p) {}
    
    // Move semantics
    QueuedFrame(QueuedFrame&& other) noexcept 
        : frame(other.frame), pts(other.pts) {
        other.frame = nullptr;
    }
    
    QueuedFrame& operator=(QueuedFrame&& other) noexcept {
        if (this != &other) {
            if (frame) av_frame_free(&frame);
            frame = other.frame;
            pts = other.pts;
            other.frame = nullptr;
        }
        return *this;
    }
    
    ~QueuedFrame() {
        if (frame) av_frame_free(&frame);
    }
    
    // No copy
    QueuedFrame(const QueuedFrame&) = delete;
    QueuedFrame& operator=(const QueuedFrame&) = delete;
};

// ==============================================================================
// Implementation class (PIMPL)
// ==============================================================================
class EncoderManager::Impl {
    friend class EncoderManager;
public:
    Impl(EncoderManager* parent) : m_parent(parent) {}
    
    ~Impl() {
        stop();
        cleanup();
    }
    
    bool configure(const EncoderSettings& settings) {
        QMutexLocker lock(&m_mutex);
        
        if (m_running) {
            qWarning() << "Cannot configure while encoder is running";
            return false;
        }
        
        m_settings = settings;
        return true;
    }
    
    bool start() {
        QMutexLocker lock(&m_mutex);
        
        if (m_running) return true;
        
        // Initialize video encoder
        if (!initializeEncoder()) {
            return false;
        }

        // Initialize audio encoder
        if (!initializeAudioEncoder()) {
            qWarning() << "Audio encoder initialization failed, continuing with video only";
        }
        
        // Start encoding thread
        m_running = true;
        m_encoderThread = std::thread(&Impl::encodingLoop, this);
        
        qDebug() << "Encoder started:" << m_activeEncoderName;
        emit m_parent->encoderReady();
        
        return true;
    }
    
    void stop() {
        {
            QMutexLocker lock(&m_mutex);
            if (!m_running) return;
            m_running = false;
        }
        
        // Wake up encoder thread
        m_queueCondition.wakeAll();
        
        // Wait for thread to finish
        if (m_encoderThread.joinable()) {
            m_encoderThread.join();
        }
        
        // Flush encoder
        flush();
        
        // Cleanup
        cleanup();
        
        qDebug() << "Encoder stopped";
        emit m_parent->encoderStopped();
    }
    
    void pushFrame(const QImage& image, int64_t pts) {
        if (!m_running || !m_codecContext) return;
        
        // Check queue size
        {
            QMutexLocker lock(&m_queueMutex);
            if (m_frameQueue.size() >= static_cast<size_t>(m_maxQueueSize)) {
                m_stats.framesDropped++;
                qWarning() << "Encoder queue full, dropping frame";
                return;
            }
        }
        
        // Convert QImage to AVFrame
        AVFrame* frame = imageToAVFrame(image);
        if (!frame) {
            qWarning() << "Failed to convert image to AVFrame";
            return;
        }
        
        // Set PTS
        if (pts < 0) {
            pts = m_frameCounter * (AV_TIME_BASE / m_settings.fpsNum);
        }
        frame->pts = pts;
        m_frameCounter++;
        
        // Add to queue
        {
            QMutexLocker lock(&m_queueMutex);
            m_frameQueue.emplace_back(frame, pts);
        }
        
        m_queueCondition.wakeOne();
    }
    
    bool isRunning() const { return m_running; }
    bool isInitialized() const { return m_codecContext != nullptr; }
    
    EncoderSettings settings() const {
        QMutexLocker lock(&m_mutex);
        return m_settings;
    }
    
    QString activeEncoderName() const { return m_activeEncoderName; }
    EncoderType activeEncoderType() const { return m_activeEncoderType; }
    
    void setPacketCallback(EncodedPacketCallback callback) {
        QMutexLocker lock(&m_mutex);
        m_packetCallback = std::move(callback);
    }
    
    int queueSize() const {
        QMutexLocker lock(&m_queueMutex);
        return static_cast<int>(m_frameQueue.size());
    }
    
    int maxQueueSize() const { return m_maxQueueSize; }
    void setMaxQueueSize(int size) { m_maxQueueSize = size; }
    
    EncoderManager::Statistics statistics() const {
        QMutexLocker lock(&m_statsMutex);
        return m_stats;
    }
    
    static bool isHardwareEncodingAvailable() {
        // Try to find NVENC
        const AVCodec* nvenc = avcodec_find_encoder_by_name("h264_nvenc");
        if (nvenc) return true;
        
        // Try AMF
        const AVCodec* amf = avcodec_find_encoder_by_name("h264_amf");
        if (amf) return true;
        
        // Try QSV
        const AVCodec* qsv = avcodec_find_encoder_by_name("h264_qsv");
        if (qsv) return true;
        
        return false;
    }
    
    static QStringList availableEncoders() {
        QStringList encoders;
        
        const char* names[] = {
            "h264_nvenc", "hevc_nvenc",
            "h264_amf", "hevc_amf",
            "h264_qsv", "hevc_qsv",
            "libx264", "libx265"
        };
        
        for (const char* name : names) {
            if (avcodec_find_encoder_by_name(name)) {
                encoders.append(QString::fromUtf8(name));
            }
        }
        
        return encoders;
    }

private:
    bool initializeEncoder() {
        cleanup();
        
        const AVCodec* codec = nullptr;
        bool isNvenc = false;
        
        // Try to find the requested encoder
        if (m_settings.encoderType == EncoderType::Auto ||
            m_settings.encoderType == EncoderType::NVENC_H264) {
            codec = avcodec_find_encoder_by_name("h264_nvenc");
            if (codec) {
                m_activeEncoderType = EncoderType::NVENC_H264;
                m_activeEncoderName = "h264_nvenc";
                isNvenc = true;
                qDebug() << "Using NVENC H.264 encoder";
            }
        }
        
        if (!codec && (m_settings.encoderType == EncoderType::Auto ||
                       m_settings.encoderType == EncoderType::AMF_H264)) {
            codec = avcodec_find_encoder_by_name("h264_amf");
            if (codec) {
                m_activeEncoderType = EncoderType::AMF_H264;
                m_activeEncoderName = "h264_amf";
                qDebug() << "Using AMF H.264 encoder";
            }
        }
        
        if (!codec && (m_settings.encoderType == EncoderType::Auto ||
                       m_settings.encoderType == EncoderType::QSV_H264)) {
            codec = avcodec_find_encoder_by_name("h264_qsv");
            if (codec) {
                m_activeEncoderType = EncoderType::QSV_H264;
                m_activeEncoderName = "h264_qsv";
                qDebug() << "Using QuickSync H.264 encoder";
            }
        }
        
        // Fallback to libx264
        if (!codec) {
            codec = avcodec_find_encoder_by_name("libx264");
            if (codec) {
                m_activeEncoderType = EncoderType::X264;
                m_activeEncoderName = "libx264";
                qDebug() << "Using libx264 software encoder (hardware unavailable)";
            }
        }
        
        if (!codec) {
            qCritical() << "No suitable H.264 encoder found!";
            emit m_parent->encoderError("No suitable H.264 encoder found");
            return false;
        }
        
        // Create codec context
        m_codecContext = avcodec_alloc_context3(codec);
        if (!m_codecContext) {
            qCritical() << "Failed to allocate codec context";
            emit m_parent->encoderError("Failed to allocate codec context");
            return false;
        }
        
        // Configure codec context
        m_codecContext->width = m_settings.width;
        m_codecContext->height = m_settings.height;
        m_codecContext->time_base = AVRational{1, m_settings.fpsNum};
        m_codecContext->framerate = AVRational{m_settings.fpsNum, m_settings.fpsDen};
        m_codecContext->gop_size = m_settings.fpsNum * m_settings.keyframeInterval;
        m_codecContext->max_b_frames = m_settings.bFrames;
        
        // Pixel format - NV12 for NVENC, YUV420P for software
        if (isNvenc) {
            m_codecContext->pix_fmt = AV_PIX_FMT_NV12;
        } else {
            m_codecContext->pix_fmt = AV_PIX_FMT_YUV420P;
        }
        
        // Rate control
        switch (m_settings.rateControl) {
            case RateControlMode::CBR:
                m_codecContext->bit_rate = m_settings.bitrate * 1000;
                m_codecContext->rc_max_rate = m_settings.bitrate * 1000;
                m_codecContext->rc_buffer_size = m_settings.bufferSize * 1000;
                if (isNvenc) {
                    av_opt_set(m_codecContext->priv_data, "rc", "cbr", 0);
                }
                break;
                
            case RateControlMode::VBR:
                m_codecContext->bit_rate = m_settings.bitrate * 1000;
                m_codecContext->rc_max_rate = m_settings.maxBitrate * 1000;
                m_codecContext->rc_buffer_size = m_settings.bufferSize * 1000;
                if (isNvenc) {
                    av_opt_set(m_codecContext->priv_data, "rc", "vbr", 0);
                }
                break;
                
            case RateControlMode::CRF:
                if (!isNvenc) {
                    av_opt_set_int(m_codecContext->priv_data, "crf", m_settings.crf, 0);
                } else {
                    av_opt_set(m_codecContext->priv_data, "rc", "vbr", 0);
                    av_opt_set_int(m_codecContext->priv_data, "cq", m_settings.crf, 0);
                }
                break;
                
            case RateControlMode::CQP:
                if (isNvenc) {
                    av_opt_set(m_codecContext->priv_data, "rc", "constqp", 0);
                    av_opt_set_int(m_codecContext->priv_data, "qp", m_settings.qp, 0);
                }
                break;
        }
        
        // Preset
        const char* presetStr = presetToString(m_settings.preset, isNvenc);
        av_opt_set(m_codecContext->priv_data, "preset", presetStr, 0);
        
        // Profile
        if (!m_settings.profile.isEmpty()) {
            av_opt_set(m_codecContext->priv_data, "profile", 
                       m_settings.profile.toUtf8().constData(), 0);
        }
        
        // NVENC-specific options
        if (isNvenc) {
            if (m_settings.nvencLowLatency) {
                av_opt_set(m_codecContext->priv_data, "tune", "ll", 0);
            }
            if (m_settings.nvencZeroLatency) {
                av_opt_set(m_codecContext->priv_data, "zerolatency", "1", 0);
            }
            // Use B-frames setting
            av_opt_set_int(m_codecContext->priv_data, "bf", m_settings.bFrames, 0);
        } else {
            // x264 specific
            if (m_settings.bFrames == 0) {
                av_opt_set(m_codecContext->priv_data, "tune", "zerolatency", 0);
            }
            if (m_settings.threads > 0) {
                m_codecContext->thread_count = m_settings.threads;
            }
        }
        
        // Global header for streaming
        m_codecContext->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        
        // Open codec
        int ret = avcodec_open2(m_codecContext, codec, nullptr);
        if (ret < 0) {
            char errbuf[256];
            av_strerror(ret, errbuf, sizeof(errbuf));
            qCritical() << "Failed to open codec:" << errbuf;
            emit m_parent->encoderError(QString("Failed to open codec: %1").arg(errbuf));
            cleanup();
            return false;
        }
        
        // Allocate packet
        m_packet = av_packet_alloc();
        if (!m_packet) {
            qCritical() << "Failed to allocate packet";
            cleanup();
            return false;
        }
        
        // Initialize scaler for BGRA -> YUV conversion
        m_swsContext = sws_getContext(
            m_settings.width, m_settings.height, AV_PIX_FMT_BGRA,
            m_settings.width, m_settings.height, m_codecContext->pix_fmt,
            SWS_FAST_BILINEAR, nullptr, nullptr, nullptr
        );
        
        if (!m_swsContext) {
            qCritical() << "Failed to create scaler context";
            cleanup();
            return false;
        }
        
        m_frameCounter = 0;
        
        qDebug() << "Encoder initialized:"
                 << m_settings.width << "x" << m_settings.height
                 << "@" << m_settings.fpsNum << "fps"
                 << m_settings.bitrate << "kbps";
        
        return true;
    }
    
    void cleanup() {
        if (m_swsContext) {
            sws_freeContext(m_swsContext);
            m_swsContext = nullptr;
        }
        
        if (m_packet) {
            av_packet_free(&m_packet);
        }
        
        if (m_codecContext) {
            avcodec_free_context(&m_codecContext);
        }
        
        // Clear frame queue
        {
            QMutexLocker lock(&m_queueMutex);
            m_frameQueue.clear();
        }

        cleanupAudio();
    }
    
    void flush() {
        if (m_codecContext) {
            // Send null frame to flush
            avcodec_send_frame(m_codecContext, nullptr);
            
            // Receive remaining packets
            while (true) {
                int ret = avcodec_receive_packet(m_codecContext, m_packet);
                if (ret == AVERROR_EOF || ret == AVERROR(EAGAIN)) {
                    break;
                }
                if (ret < 0) {
                    break;
                }
                
                processPacket();
                av_packet_unref(m_packet);
            }
        }

        if (m_audioCodecContext) {
            avcodec_send_frame(m_audioCodecContext, nullptr);
            while (true) {
                int ret = avcodec_receive_packet(m_audioCodecContext, m_audioPacket);
                if (ret == AVERROR_EOF || ret == AVERROR(EAGAIN) || ret < 0) {
                    break;
                }
                processAudioPacket();
                av_packet_unref(m_audioPacket);
            }
        }
    }
    
    void encodingLoop() {
        qDebug() << "Encoding thread started";
        
        while (m_running) {
            QueuedFrame queuedFrame;
            
            // Get frame from queue
            {
                QMutexLocker lock(&m_queueMutex);
                
                if (m_frameQueue.empty()) {
                    // Wait for frame or stop signal
                    m_queueCondition.wait(&m_queueMutex, 100);
                    continue;
                }
                
                queuedFrame = std::move(m_frameQueue.front());
                m_frameQueue.pop_front();
            }
            
            if (!queuedFrame.frame) continue;
            
            // Encode frame
            QElapsedTimer timer;
            timer.start();
            
            encodeFrame(queuedFrame.frame);
            
            // Update statistics
            {
                QMutexLocker lock(&m_statsMutex);
                double encodeTime = timer.elapsed();
                m_encodeTimes.push_back(encodeTime);
                if (m_encodeTimes.size() > 60) {
                    m_encodeTimes.pop_front();
                }
                
                double sum = 0;
                for (double t : m_encodeTimes) sum += t;
                m_stats.averageEncodeTimeMs = sum / m_encodeTimes.size();
                m_stats.currentFps = 1000.0 / m_stats.averageEncodeTimeMs;
            }
        }
        
        qDebug() << "Encoding thread stopped";
    }
    
    void encodeFrame(AVFrame* frame) {
        if (!m_codecContext || !frame) return;
        
        // Send frame to encoder
        int ret = avcodec_send_frame(m_codecContext, frame);
        if (ret < 0) {
            char errbuf[256];
            av_strerror(ret, errbuf, sizeof(errbuf));
            qWarning() << "Error sending frame to encoder:" << errbuf;
            return;
        }
        
        // Receive encoded packets
        while (ret >= 0) {
            ret = avcodec_receive_packet(m_codecContext, m_packet);
            
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                break;
            }
            
            if (ret < 0) {
                char errbuf[256];
                av_strerror(ret, errbuf, sizeof(errbuf));
                qWarning() << "Error receiving packet:" << errbuf;
                break;
            }
            
            processPacket();
            av_packet_unref(m_packet);
        }
    }
    
    void processPacket() {
        bool isKeyframe = (m_packet->flags & AV_PKT_FLAG_KEY) != 0;
        
        // Update statistics
        {
            QMutexLocker lock(&m_statsMutex);
            m_stats.framesEncoded++;
            m_stats.bytesEncoded += m_packet->size;
        }
        
        // Call callback
        if (m_packetCallback) {
            EncodedPacket pkt;
            pkt.data = m_packet->data;
            pkt.size = m_packet->size;
            pkt.pts = m_packet->pts;
            pkt.dts = m_packet->dts;
            pkt.isKeyframe = isKeyframe;
            pkt.duration = m_packet->duration;
            
            m_packetCallback(pkt);
        }
        
        // Emit signal
        emit m_parent->packetEncoded(m_packet->pts, m_packet->size, isKeyframe);
    }
    
    AVFrame* imageToAVFrame(const QImage& image) {
        // Ensure correct format
        QImage converted = image;
        if (image.format() != QImage::Format_ARGB32 &&
            image.format() != QImage::Format_RGB32) {
            converted = image.convertToFormat(QImage::Format_ARGB32);
        }
        
        // Scale if needed
        if (converted.width() != m_settings.width ||
            converted.height() != m_settings.height) {
            converted = converted.scaled(
                m_settings.width, m_settings.height,
                Qt::IgnoreAspectRatio, Qt::FastTransformation
            );
        }
        
        // Allocate AVFrame
        AVFrame* frame = av_frame_alloc();
        if (!frame) return nullptr;
        
        frame->format = m_codecContext->pix_fmt;
        frame->width = m_settings.width;
        frame->height = m_settings.height;
        
        int ret = av_frame_get_buffer(frame, 32);
        if (ret < 0) {
            av_frame_free(&frame);
            return nullptr;
        }
        
        ret = av_frame_make_writable(frame);
        if (ret < 0) {
            av_frame_free(&frame);
            return nullptr;
        }
        
        // Convert BGRA to YUV using swscale
        const uint8_t* srcSlice[1] = { converted.constBits() };
        int srcStride[1] = { static_cast<int>(converted.bytesPerLine()) };
        
        sws_scale(
            m_swsContext,
            srcSlice, srcStride, 0, m_settings.height,
            frame->data, frame->linesize
        );
        
        return frame;
    }

    bool initializeAudioEncoder() {
        if (!m_settings.audioEnabled) return true;

        const AVCodec* audioCodec = avcodec_find_encoder(AV_CODEC_ID_AAC);
        if (!audioCodec) {
            qWarning() << "AAC encoder (AV_CODEC_ID_AAC) not found!";
            return false;
        }

        m_audioCodecContext = avcodec_alloc_context3(audioCodec);
        if (!m_audioCodecContext) {
            qWarning() << "Failed to allocate AAC codec context";
            return false;
        }

        m_audioCodecContext->bit_rate = m_settings.audioBitrate * 1000;
        m_audioCodecContext->sample_rate = m_settings.audioSampleRate;
        av_channel_layout_default(&m_audioCodecContext->ch_layout, m_settings.audioChannels);
        m_audioCodecContext->sample_fmt = AV_SAMPLE_FMT_FLTP;
        m_audioCodecContext->time_base = AVRational{1, m_settings.audioSampleRate};
        m_audioCodecContext->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

        int ret = avcodec_open2(m_audioCodecContext, audioCodec, nullptr);
        if (ret < 0) {
            char errbuf[256];
            av_strerror(ret, errbuf, sizeof(errbuf));
            qCritical() << "Failed to open AAC codec:" << errbuf;
            cleanupAudio();
            return false;
        }

        int frameSize = m_audioCodecContext->frame_size > 0 ? m_audioCodecContext->frame_size : 1024;
        m_audioFrame = av_frame_alloc();
        m_audioFrame->nb_samples = frameSize;
        m_audioFrame->format = m_audioCodecContext->sample_fmt;
        av_channel_layout_copy(&m_audioFrame->ch_layout, &m_audioCodecContext->ch_layout);
        m_audioFrame->sample_rate = m_audioCodecContext->sample_rate;
        ret = av_frame_get_buffer(m_audioFrame, 0);
        if (ret < 0) {
            cleanupAudio();
            return false;
        }

        m_audioPacket = av_packet_alloc();

        // Setup swresample: convert interleaved float to encoder sample_fmt
        AVChannelLayout inLayout;
        av_channel_layout_default(&inLayout, m_settings.audioChannels);
        ret = swr_alloc_set_opts2(
            &m_swrContext,
            &m_audioCodecContext->ch_layout,
            m_audioCodecContext->sample_fmt,
            m_audioCodecContext->sample_rate,
            &inLayout,
            AV_SAMPLE_FMT_FLT,
            m_settings.audioSampleRate,
            0,
            nullptr
        );
        av_channel_layout_uninit(&inLayout);

        if (ret < 0 || !m_swrContext || swr_init(m_swrContext) < 0) {
            qCritical() << "Failed to initialize swresample context";
            cleanupAudio();
            return false;
        }

        m_audioPts = 0;
        {
            QMutexLocker lock(&m_audioMutex);
            m_audioFifo.clear();
        }

        qDebug() << "AAC Audio Encoder initialized:"
                 << m_settings.audioSampleRate << "Hz,"
                 << m_settings.audioChannels << "channels,"
                 << m_settings.audioBitrate << "kbps, frame size:" << frameSize;
        return true;
    }

    void cleanupAudio() {
        if (m_swrContext) {
            swr_free(&m_swrContext);
            m_swrContext = nullptr;
        }
        if (m_audioPacket) {
            av_packet_free(&m_audioPacket);
            m_audioPacket = nullptr;
        }
        if (m_audioFrame) {
            av_frame_free(&m_audioFrame);
            m_audioFrame = nullptr;
        }
        if (m_audioCodecContext) {
            avcodec_free_context(&m_audioCodecContext);
            m_audioCodecContext = nullptr;
        }
        if (m_audioCodecpar) {
            avcodec_parameters_free(&m_audioCodecpar);
            m_audioCodecpar = nullptr;
        }
        if (m_videoCodecpar) {
            avcodec_parameters_free(&m_videoCodecpar);
            m_videoCodecpar = nullptr;
        }
        QMutexLocker lock(&m_audioMutex);
        m_audioFifo.clear();
    }

public:
    void pushAudioFrame(const AudioFrame& frame) {
        if (!m_running || !m_audioCodecContext || !m_swrContext || !m_audioFrame) return;
        if (frame.samples.empty()) return;

        QMutexLocker lock(&m_audioMutex);
        m_audioFifo.insert(m_audioFifo.end(), frame.samples.begin(), frame.samples.end());

        int frameSize = m_audioFrame->nb_samples;
        int totalFloatsPerFrame = frameSize * m_settings.audioChannels;

        while (static_cast<int>(m_audioFifo.size()) >= totalFloatsPerFrame) {
            av_frame_make_writable(m_audioFrame);

            const uint8_t* inData[1] = { reinterpret_cast<const uint8_t*>(m_audioFifo.data()) };
            int converted = swr_convert(
                m_swrContext,
                m_audioFrame->data,
                frameSize,
                inData,
                frameSize
            );

            m_audioFrame->pts = m_audioPts;
            m_audioPts += frameSize;

            m_audioFifo.erase(m_audioFifo.begin(), m_audioFifo.begin() + totalFloatsPerFrame);

            if (converted > 0) {
                encodeAudioFrame(m_audioFrame);
            }
        }
    }

    void encodeAudioFrame(AVFrame* frame) {
        if (!m_audioCodecContext) return;

        int ret = avcodec_send_frame(m_audioCodecContext, frame);
        if (ret < 0) {
            char errbuf[256];
            av_strerror(ret, errbuf, sizeof(errbuf));
            qWarning() << "Error sending audio frame to encoder:" << errbuf;
            return;
        }

        while (ret >= 0) {
            ret = avcodec_receive_packet(m_audioCodecContext, m_audioPacket);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                break;
            }
            if (ret < 0) {
                break;
            }

            processAudioPacket();
            av_packet_unref(m_audioPacket);
        }
    }

    void processAudioPacket() {
        if (m_packetCallback && m_audioPacket) {
            EncodedPacket pkt;
            pkt.data = m_audioPacket->data;
            pkt.size = m_audioPacket->size;
            pkt.pts = m_audioPacket->pts;
            pkt.dts = m_audioPacket->dts;
            pkt.isKeyframe = true;
            pkt.duration = m_audioPacket->duration;
            pkt.isAudio = true;

            m_packetCallback(pkt);
        }

        emit m_parent->audioPacketEncoded(m_audioPacket->pts, m_audioPacket->size);
    }

    const AVCodecParameters* audioCodecParameters() const {
        if (m_audioCodecContext) {
            if (!m_audioCodecpar) {
                m_audioCodecpar = avcodec_parameters_alloc();
            }
            avcodec_parameters_from_context(m_audioCodecpar, m_audioCodecContext);
            return m_audioCodecpar;
        }
        return nullptr;
    }

    const AVCodecParameters* videoCodecParameters() const {
        if (m_codecContext) {
            if (!m_videoCodecpar) {
                m_videoCodecpar = avcodec_parameters_alloc();
            }
            avcodec_parameters_from_context(m_videoCodecpar, m_codecContext);
            return m_videoCodecpar;
        }
        return nullptr;
    }
    EncoderManager* m_parent;
    
    // Thread safety
    mutable QMutex m_mutex;
    mutable QMutex m_queueMutex;
    mutable QMutex m_statsMutex;
    QWaitCondition m_queueCondition;
    
    // State
    std::atomic<bool> m_running{false};
    std::thread m_encoderThread;
    
    // Settings
    EncoderSettings m_settings;
    
    // FFmpeg objects
    AVCodecContext* m_codecContext = nullptr;
    AVPacket* m_packet = nullptr;
    SwsContext* m_swsContext = nullptr;
    
    // Audio FFmpeg objects
    AVCodecContext* m_audioCodecContext = nullptr;
    AVPacket* m_audioPacket = nullptr;
    AVFrame* m_audioFrame = nullptr;
    SwrContext* m_swrContext = nullptr;
    mutable AVCodecParameters* m_audioCodecpar = nullptr;
    mutable AVCodecParameters* m_videoCodecpar = nullptr;
    std::vector<float> m_audioFifo;
    int64_t m_audioPts = 0;
    mutable QMutex m_audioMutex;
    
    // Encoder info
    QString m_activeEncoderName;
    EncoderType m_activeEncoderType = EncoderType::X264;
    
    // Frame queue
    std::deque<QueuedFrame> m_frameQueue;
    int m_maxQueueSize = 30;  // ~0.5 second at 60fps
    int64_t m_frameCounter = 0;
    
    // Callback
    EncodedPacketCallback m_packetCallback;
    
    // Statistics
    EncoderManager::Statistics m_stats;
    std::deque<double> m_encodeTimes;
};

// ==============================================================================
// EncoderManager Singleton
// ==============================================================================
EncoderManager& EncoderManager::instance() {
    static EncoderManager instance;
    return instance;
}

EncoderManager::EncoderManager(QObject* parent)
    : QObject(parent)
    , m_impl(std::make_unique<Impl>(this))
{
}

EncoderManager::~EncoderManager() = default;

bool EncoderManager::configure(const EncoderSettings& settings) {
    return m_impl->configure(settings);
}

EncoderSettings EncoderManager::settings() const {
    return m_impl->settings();
}

void EncoderManager::setPacketCallback(EncodedPacketCallback callback) {
    m_impl->setPacketCallback(std::move(callback));
}

bool EncoderManager::start() {
    return m_impl->start();
}

void EncoderManager::stop() {
    m_impl->stop();
}

bool EncoderManager::isRunning() const {
    return m_impl->isRunning();
}

bool EncoderManager::isInitialized() const {
    return m_impl->isInitialized();
}

void EncoderManager::pushFrame(const QImage& image, int64_t pts) {
    m_impl->pushFrame(image, pts);
}

void EncoderManager::pushAudioFrame(const AudioFrame& frame) {
    m_impl->pushAudioFrame(frame);
}

const AVCodecParameters* EncoderManager::audioCodecParameters() const {
    return m_impl->audioCodecParameters();
}

const AVCodecParameters* EncoderManager::videoCodecParameters() const {
    return m_impl->videoCodecParameters();
}

int EncoderManager::queueSize() const {
    return m_impl->queueSize();
}

int EncoderManager::maxQueueSize() const {
    return m_impl->maxQueueSize();
}

void EncoderManager::setMaxQueueSize(int size) {
    m_impl->setMaxQueueSize(size);
}

QString EncoderManager::activeEncoderName() const {
    return m_impl->activeEncoderName();
}

EncoderType EncoderManager::activeEncoderType() const {
    return m_impl->activeEncoderType();
}

bool EncoderManager::isHardwareEncodingAvailable() {
    return Impl::isHardwareEncodingAvailable();
}

QStringList EncoderManager::availableEncoders() {
    return Impl::availableEncoders();
}

EncoderManager::Statistics EncoderManager::statistics() const {
    return m_impl->statistics();
}

} // namespace WeaR
