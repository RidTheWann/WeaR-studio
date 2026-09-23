// ==============================================================================
// WeaR-studio AudioCaptureSource Implementation
// WASAPI-based Desktop Audio (Loopback) and Microphone Capture
// ==============================================================================

#include "AudioCaptureSource.h"

#include <Windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <wrl/client.h>

#include <QDebug>
#include <QDateTime>
#include <algorithm>
#include <cmath>

namespace WeaR {

using Microsoft::WRL::ComPtr;

// ==============================================================================
// AudioCaptureSource::Impl
// ==============================================================================
class AudioCaptureSource::Impl {
public:
    explicit Impl(AudioDeviceType type)
        : m_deviceType(type) {}

    ~Impl() {
        stop();
    }

    bool start() {
        if (m_running) return true;
        m_running = true;
        m_workerThread = std::thread(&Impl::captureLoop, this);
        return true;
    }

    void stop() {
        if (!m_running) return;
        m_running = false;
        if (m_workerThread.joinable()) {
            m_workerThread.join();
        }
        QMutexLocker lock(&m_bufferMutex);
        m_buffer.clear();
    }

    AudioFrame captureAudioFrame(int samplesRequested) {
        AudioFrame frame;
        frame.sampleRate = 48000;
        frame.channels = 2;
        frame.timestamp = QDateTime::currentMSecsSinceEpoch() * 1000;

        int totalFloatsNeeded = samplesRequested * 2;
        frame.samples.resize(totalFloatsNeeded, 0.0f);

        QMutexLocker lock(&m_bufferMutex);
        int availableFloats = static_cast<int>(m_buffer.size());
        int toCopy = std::min(totalFloatsNeeded, availableFloats);

        for (int i = 0; i < toCopy; ++i) {
            frame.samples[i] = m_buffer.front();
            m_buffer.pop_front();
        }

        // Any remaining floats in frame.samples stay 0.0f (silence padding)
        return frame;
    }

private:
    void captureLoop() {
        HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        bool coInitialized = SUCCEEDED(hr);

        ComPtr<IMMDeviceEnumerator> enumerator;
        hr = CoCreateInstance(
            __uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
            __uuidof(IMMDeviceEnumerator), (void**)&enumerator
        );
        if (FAILED(hr) || !enumerator) {
            qWarning() << "Failed to create MMDeviceEnumerator:" << Qt::hex << hr;
            if (coInitialized) CoUninitialize();
            return;
        }

        EDataFlow flow = (m_deviceType == AudioDeviceType::DesktopLoopback) ? eRender : eCapture;
        ComPtr<IMMDevice> device;
        hr = enumerator->GetDefaultAudioEndpoint(flow, eConsole, &device);
        if (FAILED(hr) || !device) {
            qWarning() << "Failed to get default audio endpoint for flow" << flow;
            if (coInitialized) CoUninitialize();
            return;
        }

        ComPtr<IAudioClient> audioClient;
        hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&audioClient);
        if (FAILED(hr) || !audioClient) {
            qWarning() << "Failed to activate IAudioClient";
            if (coInitialized) CoUninitialize();
            return;
        }

        WAVEFORMATEX* pwfx = nullptr;
        hr = audioClient->GetMixFormat(&pwfx);
        if (FAILED(hr) || !pwfx) {
            qWarning() << "Failed to get mix format";
            if (coInitialized) CoUninitialize();
            return;
        }

        DWORD streamFlags = (m_deviceType == AudioDeviceType::DesktopLoopback)
            ? AUDCLNT_STREAMFLAGS_LOOPBACK
            : 0;
        REFERENCE_TIME hnsBufferDuration = 1000000; // 100 ms

        hr = audioClient->Initialize(
            AUDCLNT_SHAREMODE_SHARED,
            streamFlags,
            hnsBufferDuration,
            0,
            pwfx,
            nullptr
        );
        if (FAILED(hr)) {
            qWarning() << "Failed to initialize IAudioClient:" << Qt::hex << hr;
            CoTaskMemFree(pwfx);
            if (coInitialized) CoUninitialize();
            return;
        }

        ComPtr<IAudioCaptureClient> captureClient;
        hr = audioClient->GetService(__uuidof(IAudioCaptureClient), (void**)&captureClient);
        if (FAILED(hr) || !captureClient) {
            qWarning() << "Failed to get IAudioCaptureClient";
            CoTaskMemFree(pwfx);
            if (coInitialized) CoUninitialize();
            return;
        }

        hr = audioClient->Start();
        if (FAILED(hr)) {
            qWarning() << "Failed to start IAudioClient";
            CoTaskMemFree(pwfx);
            if (coInitialized) CoUninitialize();
            return;
        }

        int nativeChannels = pwfx->nChannels;
        int nativeRate = pwfx->nSamplesPerSec;
        bool isFloat = (pwfx->wFormatTag == WAVE_FORMAT_IEEE_FLOAT);
        if (pwfx->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
            auto* pEx = reinterpret_cast<WAVEFORMATEXTENSIBLE*>(pwfx);
            if (IsEqualGUID(pEx->SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)) {
                isFloat = true;
            }
        }
        int bitsPerSample = pwfx->wBitsPerSample;

        qDebug() << "WASAPI audio capture started:"
                 << (m_deviceType == AudioDeviceType::DesktopLoopback ? "Desktop Loopback" : "Microphone")
                 << nativeRate << "Hz," << nativeChannels << "channels," << bitsPerSample << "bits";

        while (m_running) {
            UINT32 packetLength = 0;
            hr = captureClient->GetNextPacketSize(&packetLength);
            if (FAILED(hr)) {
                break;
            }

            while (packetLength > 0) {
                BYTE* pData = nullptr;
                UINT32 numFramesAvailable = 0;
                DWORD flags = 0;

                hr = captureClient->GetBuffer(&pData, &numFramesAvailable, &flags, nullptr, nullptr);
                if (FAILED(hr)) break;

                // Process buffer
                std::vector<float> tempStereo(numFramesAvailable * 2, 0.0f);

                if (!(flags & AUDCLNT_BUFFERFLAGS_SILENT) && pData != nullptr) {
                    if (isFloat && bitsPerSample == 32) {
                        const float* fData = reinterpret_cast<const float*>(pData);
                        for (UINT32 f = 0; f < numFramesAvailable; ++f) {
                            if (nativeChannels == 1) {
                                float s = fData[f];
                                tempStereo[f * 2] = s;
                                tempStereo[f * 2 + 1] = s;
                            } else {
                                tempStereo[f * 2] = fData[f * nativeChannels];
                                tempStereo[f * 2 + 1] = fData[f * nativeChannels + 1];
                            }
                        }
                    } else if (bitsPerSample == 16) {
                        const int16_t* sData = reinterpret_cast<const int16_t*>(pData);
                        for (UINT32 f = 0; f < numFramesAvailable; ++f) {
                            if (nativeChannels == 1) {
                                float s = sData[f] / 32768.0f;
                                tempStereo[f * 2] = s;
                                tempStereo[f * 2 + 1] = s;
                            } else {
                                tempStereo[f * 2] = sData[f * nativeChannels] / 32768.0f;
                                tempStereo[f * 2 + 1] = sData[f * nativeChannels + 1] / 32768.0f;
                            }
                        }
                    }
                }

                // Resample to 48000 Hz if needed
                std::vector<float> resampledStereo;
                if (nativeRate != 48000 && nativeRate > 0) {
                    double ratio = 48000.0 / static_cast<double>(nativeRate);
                    int outFrames = static_cast<int>(std::round(numFramesAvailable * ratio));
                    resampledStereo.resize(outFrames * 2);

                    for (int outIdx = 0; outIdx < outFrames; ++outIdx) {
                        double inIdx = outIdx / ratio;
                        int idxFloor = static_cast<int>(inIdx);
                        int idxCeil = std::min(idxFloor + 1, static_cast<int>(numFramesAvailable) - 1);
                        float frac = static_cast<float>(inIdx - idxFloor);

                        // Left channel
                        float l0 = tempStereo[idxFloor * 2];
                        float l1 = tempStereo[idxCeil * 2];
                        resampledStereo[outIdx * 2] = l0 + frac * (l1 - l0);

                        // Right channel
                        float r0 = tempStereo[idxFloor * 2 + 1];
                        float r1 = tempStereo[idxCeil * 2 + 1];
                        resampledStereo[outIdx * 2 + 1] = r0 + frac * (r1 - r0);
                    }
                } else {
                    resampledStereo = std::move(tempStereo);
                }

                // Append to FIFO buffer
                {
                    QMutexLocker lock(&m_bufferMutex);
                    // Prevent memory unbounded growth (> 1 second backlog)
                    const size_t maxBufferSize = 48000 * 2;
                    if (m_buffer.size() + resampledStereo.size() > maxBufferSize) {
                        size_t drop = (m_buffer.size() + resampledStereo.size()) - maxBufferSize;
                        for (size_t d = 0; d < drop && !m_buffer.empty(); ++d) {
                            m_buffer.pop_front();
                        }
                    }
                    for (float s : resampledStereo) {
                        m_buffer.push_back(s);
                    }
                }

                captureClient->ReleaseBuffer(numFramesAvailable);
                captureClient->GetNextPacketSize(&packetLength);
            }

            Sleep(5); // Wait before polling again
        }

        audioClient->Stop();
        CoTaskMemFree(pwfx);
        if (coInitialized) {
            CoUninitialize();
        }
    }

    AudioDeviceType m_deviceType;
    std::atomic<bool> m_running{false};
    std::thread m_workerThread;
    std::deque<float> m_buffer;
    QMutex m_bufferMutex;
};

// ==============================================================================
// AudioCaptureSource
// ==============================================================================
AudioCaptureSource::AudioCaptureSource(AudioDeviceType deviceType, QObject* parent)
    : QObject(parent)
    , m_deviceType(deviceType)
    , m_impl(std::make_unique<Impl>(deviceType))
{
    m_config.fps = 60.0;
    m_samplesPerFrame = 800; // 48000 / 60 = 800
}

AudioCaptureSource::~AudioCaptureSource() {
    stop();
    shutdown();
}

PluginInfo AudioCaptureSource::info() const {
    PluginInfo i;
    i.id = (m_deviceType == AudioDeviceType::DesktopLoopback)
        ? QStringLiteral("wear.source.audio.desktop")
        : QStringLiteral("wear.source.audio.mic");
    i.name = name();
    i.version = version();
    i.author = QStringLiteral("WeaR Studio");
    i.description = (m_deviceType == AudioDeviceType::DesktopLoopback)
        ? QStringLiteral("WASAPI Desktop Audio Loopback Source")
        : QStringLiteral("WASAPI Microphone Capture Source");
    i.type = PluginType::Source;
    i.capabilities = capabilities();
    return i;
}

QString AudioCaptureSource::name() const {
    return (m_deviceType == AudioDeviceType::DesktopLoopback)
        ? QStringLiteral("Desktop Audio")
        : QStringLiteral("Mic/Aux");
}

bool AudioCaptureSource::initialize() {
    m_initialized = true;
    return true;
}

void AudioCaptureSource::shutdown() {
    stop();
    m_initialized = false;
}

bool AudioCaptureSource::isActive() const {
    return m_initialized && m_running;
}

bool AudioCaptureSource::configure(const SourceConfig& config) {
    QMutexLocker lock(&m_mutex);
    m_config = config;
    if (config.fps > 0) {
        m_samplesPerFrame = static_cast<int>(48000.0 / config.fps);
    }
    return true;
}

SourceConfig AudioCaptureSource::config() const {
    QMutexLocker lock(&m_mutex);
    return m_config;
}

bool AudioCaptureSource::start() {
    QMutexLocker lock(&m_mutex);
    if (m_running) return true;
    if (!m_initialized) initialize();
    if (m_impl->start()) {
        m_running = true;
        return true;
    }
    return false;
}

void AudioCaptureSource::stop() {
    QMutexLocker lock(&m_mutex);
    if (!m_running) return;
    m_impl->stop();
    m_running = false;
}

bool AudioCaptureSource::isRunning() const {
    return m_running;
}

AudioFrame AudioCaptureSource::captureAudioFrame() {
    if (!m_running) {
        AudioFrame frame;
        frame.sampleRate = 48000;
        frame.channels = 2;
        frame.samples.resize(m_samplesPerFrame * 2, 0.0f);
        return frame;
    }
    return m_impl->captureAudioFrame(m_samplesPerFrame);
}

// ==============================================================================
// DesktopAudioSource
// ==============================================================================
DesktopAudioSource& DesktopAudioSource::instance() {
    static DesktopAudioSource s_instance;
    return s_instance;
}

DesktopAudioSource::DesktopAudioSource(QObject* parent)
    : AudioCaptureSource(AudioDeviceType::DesktopLoopback, parent) {}

// ==============================================================================
// MicrophoneAudioSource
// ==============================================================================
MicrophoneAudioSource& MicrophoneAudioSource::instance() {
    static MicrophoneAudioSource s_instance;
    return s_instance;
}

MicrophoneAudioSource::MicrophoneAudioSource(QObject* parent)
    : AudioCaptureSource(AudioDeviceType::Microphone, parent) {}

} // namespace WeaR
