// =============================================================================
// WeaR Studio Virtual Camera COM source
// =============================================================================

#include "VirtualCameraMediaSource.h"

#include "../../core/VirtualCameraProtocol.h"

#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <mfobjects.h>
#include <mfvirtualcamera.h>
#include <ks.h>
#include <shlwapi.h>
#include <cwchar>

#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <iterator>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "mfsensorgroup.lib")

namespace {

using Microsoft::WRL::ComPtr;
using namespace WeaR::VirtualCameraProtocol;

std::atomic<long> g_objectCount{0};
HINSTANCE g_module = nullptr;

LONG clampByte(int value) {
    return static_cast<LONG>(std::clamp(value, 0, 255));
}

void fillBlackNv12(BYTE* dst) {
    std::memset(
        dst,
        16,
        static_cast<size_t>(kWidth) * static_cast<size_t>(kHeight));
    std::memset(
        dst + static_cast<size_t>(kWidth) * static_cast<size_t>(kHeight),
        128,
        static_cast<size_t>(kNv12Bytes - kWidth * kHeight));
}

bool writeExact(HANDLE pipe, const void* data, DWORD bytes) {
    const auto* ptr = static_cast<const BYTE*>(data);
    DWORD remaining = bytes;

    while (remaining > 0) {
        DWORD written = 0;
        if (!WriteFile(pipe, ptr, remaining, &written, nullptr) ||
            written == 0) {
            return false;
        }
        ptr += written;
        remaining -= written;
    }

    return true;
}

bool readExact(HANDLE pipe, void* data, DWORD bytes) {
    auto* ptr = static_cast<BYTE*>(data);
    DWORD remaining = bytes;

    while (remaining > 0) {
        DWORD read = 0;
        if (!ReadFile(pipe, ptr, remaining, &read, nullptr) ||
            read == 0) {
            return false;
        }
        ptr += read;
        remaining -= read;
    }

    return true;
}

class PipeFrameClient {
public:
    ~PipeFrameClient() {
        close();
    }

    bool request(std::vector<BYTE>& bgra, LONGLONG& timestamp100ns) {
        if (!ensureConnected()) {
            return false;
        }

        const char requestByte = 1;
        if (!writeExact(m_pipe, &requestByte, 1)) {
            close();
            return false;
        }

        FrameHeader header{};
        if (!readExact(
                m_pipe,
                &header,
                static_cast<DWORD>(sizeof(header)))) {
            close();
            return false;
        }

        if (std::memcmp(header.magic, kMagic, sizeof(kMagic)) != 0 ||
            header.version != kVersion ||
            header.width != kWidth ||
            header.height != kHeight ||
            header.stride != kStride ||
            header.dataBytes != kBgraBytes) {
            close();
            return false;
        }

        bgra.resize(kBgraBytes);
        if (!readExact(
                m_pipe,
                bgra.data(),
                static_cast<DWORD>(bgra.size()))) {
            close();
            return false;
        }

        timestamp100ns = header.timestamp100ns;
        return true;
    }

private:
    bool ensureConnected() {
        if (m_pipe != INVALID_HANDLE_VALUE) {
            return true;
        }

        if (!WaitNamedPipeW(kPipeName, 50)) {
            return false;
        }

        m_pipe = CreateFileW(
            kPipeName,
            GENERIC_READ | GENERIC_WRITE,
            0,
            nullptr,
            OPEN_EXISTING,
            0,
            nullptr);

        return m_pipe != INVALID_HANDLE_VALUE;
    }

    void close() {
        if (m_pipe != INVALID_HANDLE_VALUE) {
            CloseHandle(m_pipe);
            m_pipe = INVALID_HANDLE_VALUE;
        }
    }

    HANDLE m_pipe = INVALID_HANDLE_VALUE;
};

HRESULT createNv12Sample(
    const std::vector<BYTE>& bgra,
    LONGLONG timestamp100ns,
    IMFMediaType* /*type*/,
    IUnknown* token,
    IMFMediaEventQueue* queue) {
    ComPtr<IMFSample> sample;
    HRESULT hr = MFCreateSample(&sample);
    if (FAILED(hr)) return hr;

    ComPtr<IMFMediaBuffer> buffer;
    hr = MFCreateMemoryBuffer(kNv12Bytes, &buffer);
    if (FAILED(hr)) return hr;

    BYTE* dst = nullptr;
    DWORD maxLength = 0;
    DWORD currentLength = 0;
    hr = buffer->Lock(&dst, &maxLength, &currentLength);
    if (FAILED(hr) || !dst) return FAILED(hr) ? hr : E_FAIL;

    fillBlackNv12(dst);

    if (bgra.size() == kBgraBytes) {
        BYTE* yPlane = dst;
        BYTE* uvPlane =
            dst + static_cast<size_t>(kWidth) * static_cast<size_t>(kHeight);

        for (std::uint32_t y = 0; y < kHeight; ++y) {
            for (std::uint32_t x = 0; x < kWidth; ++x) {
                const size_t offset =
                    static_cast<size_t>(y) * kStride +
                    static_cast<size_t>(x) * kBytesPerPixel;

                const int b = bgra[offset + 0];
                const int g = bgra[offset + 1];
                const int r = bgra[offset + 2];

                const int yy =
                    ((66 * r + 129 * g + 25 * b + 128) >> 8) + 16;
                yPlane[static_cast<size_t>(y) * kWidth + x] =
                    static_cast<BYTE>(clampByte(yy));
            }
        }

        for (std::uint32_t y = 0; y < kHeight; y += 2) {
            for (std::uint32_t x = 0; x < kWidth; x += 2) {
                int sumU = 0;
                int sumV = 0;

                for (std::uint32_t dy = 0; dy < 2; ++dy) {
                    for (std::uint32_t dx = 0; dx < 2; ++dx) {
                        const std::uint32_t px =
                            std::min<std::uint32_t>(x + dx, kWidth - 1);
                        const std::uint32_t py =
                            std::min<std::uint32_t>(y + dy, kHeight - 1);

                        const size_t offset =
                            static_cast<size_t>(py) * kStride +
                            static_cast<size_t>(px) * kBytesPerPixel;

                        const int b = bgra[offset + 0];
                        const int g = bgra[offset + 1];
                        const int r = bgra[offset + 2];

                        sumU += ((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128;
                        sumV += ((112 * r - 94 * g - 18 * b + 128) >> 8) + 128;
                    }
                }

                const size_t uvOffset =
                    static_cast<size_t>(y / 2) * kWidth + x;
                uvPlane[uvOffset + 0] =
                    static_cast<BYTE>(clampByte(sumU / 4));
                uvPlane[uvOffset + 1] =
                    static_cast<BYTE>(clampByte(sumV / 4));
            }
        }
    }

    buffer->Unlock();

    hr = buffer->SetCurrentLength(kNv12Bytes);
    if (FAILED(hr)) return hr;

    hr = sample->AddBuffer(buffer.Get());
    if (FAILED(hr)) return hr;

    hr = sample->SetSampleTime(
        timestamp100ns > 0 ? timestamp100ns : MFGetSystemTime());
    if (FAILED(hr)) return hr;

    hr = sample->SetSampleDuration(333333);
    if (FAILED(hr)) return hr;

    if (token) {
        hr = sample->SetUnknown(MFSampleExtension_Token, token);
        if (FAILED(hr)) return hr;
    }

    return queue->QueueEventParamUnk(
        MEMediaSample,
        GUID_NULL,
        S_OK,
        sample.Get());
}

class MediaStream final : public IMFMediaStream2 {
public:
    MediaStream(IMFMediaSource* source, IMFStreamDescriptor* descriptor)
        : m_source(source),
          m_descriptor(descriptor) {
        ++g_objectCount;
    }

    HRESULT initialize() {
        return MFCreateEventQueue(&m_events);
    }

    // IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(
        REFIID riid,
        void** ppv) override {
        if (!ppv) return E_POINTER;
        *ppv = nullptr;

        if (riid == IID_IUnknown ||
            riid == IID_IMFMediaEventGenerator ||
            riid == IID_IMFMediaStream ||
            riid == IID_IMFMediaStream2) {
            *ppv = static_cast<IMFMediaStream2*>(this);
            AddRef();
            return S_OK;
        }

        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override {
        return static_cast<ULONG>(++m_refs);
    }

    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG value = static_cast<ULONG>(--m_refs);
        if (value == 0) delete this;
        return value;
    }

    HRESULT STDMETHODCALLTYPE BeginGetEvent(
        IMFAsyncCallback* callback,
        IUnknown* state) override {
        if (!m_events) return MF_E_SHUTDOWN;
        return m_events->BeginGetEvent(callback, state);
    }

    HRESULT STDMETHODCALLTYPE EndGetEvent(
        IMFAsyncResult* result,
        IMFMediaEvent** event) override {
        if (!m_events) return MF_E_SHUTDOWN;
        return m_events->EndGetEvent(result, event);
    }

    HRESULT STDMETHODCALLTYPE GetEvent(
        DWORD flags,
        IMFMediaEvent** event) override {
        if (!m_events) return MF_E_SHUTDOWN;
        return m_events->GetEvent(flags, event);
    }

    HRESULT STDMETHODCALLTYPE QueueEvent(
        MediaEventType type,
        REFGUID extendedType,
        HRESULT status,
        const PROPVARIANT* value) override {
        if (!m_events) return MF_E_SHUTDOWN;
        return m_events->QueueEventParamVar(
            type, extendedType, status, value);
    }

    HRESULT STDMETHODCALLTYPE GetMediaSource(
        IMFMediaSource** source) override {
        if (!source) return E_POINTER;
        *source = nullptr;
        if (!m_source) return MF_E_SHUTDOWN;
        return m_source.CopyTo(source);
    }

    HRESULT STDMETHODCALLTYPE GetStreamDescriptor(
        IMFStreamDescriptor** descriptor) override {
        if (!descriptor) return E_POINTER;
        *descriptor = nullptr;
        if (!m_descriptor) return MF_E_SHUTDOWN;
        return m_descriptor.CopyTo(descriptor);
    }

    HRESULT STDMETHODCALLTYPE RequestSample(
        IUnknown* token) override {
        std::lock_guard<std::mutex> lock(m_requestMutex);

        if (!m_events || m_state != MF_STREAM_STATE_RUNNING) {
            return MF_E_NOT_INITIALIZED;
        }

        std::vector<BYTE> bgra;
        LONGLONG timestamp100ns = 0;
        m_pipe.request(bgra, timestamp100ns);

        return createNv12Sample(
            bgra,
            timestamp100ns,
            m_type.Get(),
            token,
            m_events.Get());
    }

    HRESULT STDMETHODCALLTYPE SetStreamState(
        MF_STREAM_STATE state) override {
        if (state == m_state) return S_OK;

        if (state == MF_STREAM_STATE_RUNNING) {
            m_state = MF_STREAM_STATE_RUNNING;
            return m_events
                ? m_events->QueueEventParamVar(
                      MEStreamStarted, GUID_NULL, S_OK, nullptr)
                : MF_E_SHUTDOWN;
        }

        if (state == MF_STREAM_STATE_STOPPED) {
            m_state = MF_STREAM_STATE_STOPPED;
            return m_events
                ? m_events->QueueEventParamVar(
                      MEStreamStopped, GUID_NULL, S_OK, nullptr)
                : MF_E_SHUTDOWN;
        }

        if (state == MF_STREAM_STATE_PAUSED) {
            if (m_state != MF_STREAM_STATE_RUNNING) {
                return MF_E_INVALID_STATE_TRANSITION;
            }
            m_state = MF_STREAM_STATE_PAUSED;
            return S_OK;
        }

        return MF_E_INVALID_STATE_TRANSITION;
    }

    HRESULT STDMETHODCALLTYPE GetStreamState(
        MF_STREAM_STATE* state) override {
        if (!state) return E_POINTER;
        *state = m_state;
        return S_OK;
    }

    IMFStreamDescriptor* descriptor() const {
        return m_descriptor.Get();
    }

    HRESULT setType(IMFMediaType* type) {
        m_type = type;
        return S_OK;
    }

    void shutdown() {
        if (m_events) {
            m_events->Shutdown();
            m_events.Reset();
        }
        m_type.Reset();
        m_descriptor.Reset();
        m_source.Reset();
    }

private:
    ~MediaStream() override {
        --g_objectCount;
    }

    std::atomic<ULONG> m_refs{1};
    ComPtr<IMFMediaSource> m_source;
    ComPtr<IMFStreamDescriptor> m_descriptor;
    ComPtr<IMFMediaType> m_type;
    ComPtr<IMFMediaEventQueue> m_events;
    MF_STREAM_STATE m_state = MF_STREAM_STATE_STOPPED;
    PipeFrameClient m_pipe;
    std::mutex m_requestMutex;
};

class MediaSource final
    : public IMFMediaSourceEx
    , public IMFGetService
    , public IKsControl {
public:
    MediaSource() {
        ++g_objectCount;
    }

    HRESULT initialize() {
        HRESULT hr = MFCreateEventQueue(&m_events);
        if (FAILED(hr)) return hr;

        ComPtr<IMFMediaType> type;
        hr = MFCreateMediaType(&type);
        if (FAILED(hr)) return hr;

        hr = type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        if (FAILED(hr)) return hr;
        hr = type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
        if (FAILED(hr)) return hr;
        hr = MFSetAttributeSize(type.Get(), MF_MT_FRAME_SIZE, kWidth, kHeight);
        if (FAILED(hr)) return hr;
        hr = type->SetUINT32(MF_MT_DEFAULT_STRIDE, kWidth);
        if (FAILED(hr)) return hr;
        hr = type->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
        if (FAILED(hr)) return hr;
        hr = type->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
        if (FAILED(hr)) return hr;
        hr = MFSetAttributeRatio(type.Get(), MF_MT_FRAME_RATE, 30, 1);
        if (FAILED(hr)) return hr;
        hr = MFSetAttributeRatio(type.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
        if (FAILED(hr)) return hr;
        hr = type->SetUINT32(
            MF_MT_AVG_BITRATE,
            static_cast<UINT32>(kNv12Bytes * 8ULL * 30ULL));
        if (FAILED(hr)) return hr;

        ComPtr<IMFStreamDescriptor> descriptor;
        IMFMediaType* types[] = { type.Get() };
        hr = MFCreateStreamDescriptor(
            0,
            1,
            types,
            &descriptor);
        if (FAILED(hr)) return hr;

        ComPtr<IMFMediaTypeHandler> handler;
        hr = descriptor->GetMediaTypeHandler(&handler);
        if (FAILED(hr)) return hr;
        hr = handler->SetCurrentMediaType(type.Get());
        if (FAILED(hr)) return hr;

        auto* stream = new MediaStream(this, descriptor.Get());
        hr = stream->initialize();
        if (FAILED(hr)) {
            stream->Release();
            return hr;
        }

        hr = stream->setType(type.Get());
        if (FAILED(hr)) {
            stream->Release();
            return hr;
        }

        m_stream = stream;

        IMFStreamDescriptor* descriptors[] = { descriptor.Get() };
        hr = MFCreatePresentationDescriptor(
            1,
            descriptors,
            &m_presentation);
        if (FAILED(hr)) return hr;

        hr = MFCreateAttributes(&m_sourceAttributes, 8);
        if (FAILED(hr)) return hr;

        return S_OK;
    }

    // IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(
        REFIID riid,
        void** ppv) override {
        if (!ppv) return E_POINTER;
        *ppv = nullptr;

        if (riid == IID_IUnknown ||
            riid == IID_IMFMediaEventGenerator ||
            riid == IID_IMFMediaSource ||
            riid == IID_IMFMediaSourceEx ||
            riid == IID_IMFGetService ||
            riid == IID_IKsControl) {
            *ppv = static_cast<IMFMediaSourceEx*>(this);
            AddRef();
            return S_OK;
        }

        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override {
        return static_cast<ULONG>(++m_refs);
    }

    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG value = static_cast<ULONG>(--m_refs);
        if (value == 0) delete this;
        return value;
    }

    HRESULT STDMETHODCALLTYPE BeginGetEvent(
        IMFAsyncCallback* callback,
        IUnknown* state) override {
        return m_events
            ? m_events->BeginGetEvent(callback, state)
            : MF_E_SHUTDOWN;
    }

    HRESULT STDMETHODCALLTYPE EndGetEvent(
        IMFAsyncResult* result,
        IMFMediaEvent** event) override {
        return m_events
            ? m_events->EndGetEvent(result, event)
            : MF_E_SHUTDOWN;
    }

    HRESULT STDMETHODCALLTYPE GetEvent(
        DWORD flags,
        IMFMediaEvent** event) override {
        return m_events
            ? m_events->GetEvent(flags, event)
            : MF_E_SHUTDOWN;
    }

    HRESULT STDMETHODCALLTYPE QueueEvent(
        MediaEventType type,
        REFGUID extendedType,
        HRESULT status,
        const PROPVARIANT* value) override {
        return m_events
            ? m_events->QueueEventParamVar(
                  type, extendedType, status, value)
            : MF_E_SHUTDOWN;
    }

    HRESULT STDMETHODCALLTYPE CreatePresentationDescriptor(
        IMFPresentationDescriptor** descriptor) override {
        if (!descriptor) return E_POINTER;
        *descriptor = nullptr;
        if (!m_presentation) return MF_E_SHUTDOWN;
        return m_presentation->Clone(descriptor);
    }

    HRESULT STDMETHODCALLTYPE GetCharacteristics(
        DWORD* characteristics) override {
        if (!characteristics) return E_POINTER;
        *characteristics = MFMEDIASOURCE_IS_LIVE;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Pause() override {
        return MF_E_INVALID_STATE_TRANSITION;
    }

    HRESULT STDMETHODCALLTYPE Shutdown() override {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_events) return MF_E_SHUTDOWN;

        m_events->QueueEventParamVar(
            MESourceStopped, GUID_NULL, S_OK, nullptr);
        m_events->Shutdown();
        m_events.Reset();

        if (m_stream) {
            m_stream->shutdown();
            m_stream->Release();
            m_stream = nullptr;
        }

        m_presentation.Reset();
        m_sourceAttributes.Reset();
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Start(
        IMFPresentationDescriptor* descriptor,
        const GUID* timeFormat,
        const PROPVARIANT* startPosition) override {
        if (!descriptor || !startPosition) return E_POINTER;
        if (timeFormat && *timeFormat != GUID_NULL) {
            return E_INVALIDARG;
        }
        if (!m_events || !m_stream) return MF_E_SHUTDOWN;

        HRESULT hr = m_presentation->SelectStream(0);
        if (FAILED(hr)) return hr;

        hr = m_stream->SetStreamState(MF_STREAM_STATE_RUNNING);
        if (FAILED(hr)) return hr;

        return m_events->QueueEventParamVar(
            MESourceStarted,
            GUID_NULL,
            S_OK,
            startPosition);
    }

    HRESULT STDMETHODCALLTYPE Stop() override {
        if (!m_events || !m_stream) return MF_E_SHUTDOWN;

        HRESULT hr = m_stream->SetStreamState(MF_STREAM_STATE_STOPPED);
        if (FAILED(hr) && hr != MF_E_NOT_INITIALIZED) return hr;

        return m_events->QueueEventParamVar(
            MESourceStopped,
            GUID_NULL,
            S_OK,
            nullptr);
    }

    HRESULT STDMETHODCALLTYPE GetSourceAttributes(
        IMFAttributes** attributes) override {
        if (!attributes) return E_POINTER;
        *attributes = nullptr;
        if (!m_sourceAttributes) return MF_E_SHUTDOWN;
        return m_sourceAttributes.CopyTo(attributes);
    }

    HRESULT STDMETHODCALLTYPE GetStreamAttributes(
        DWORD streamIdentifier,
        IMFAttributes** attributes) override {
        if (!attributes) return E_POINTER;
        *attributes = nullptr;
        if (streamIdentifier != 0 || !m_stream) return E_INVALIDARG;
        return m_sourceAttributes.CopyTo(attributes);
    }

    HRESULT STDMETHODCALLTYPE SetD3DManager(
        IUnknown* manager) override {
        // The first virtual-camera implementation is CPU-backed. Keeping this
        // method successful allows clients to probe for D3D support without
        // changing the output contract.
        (void)manager;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE SetMediaType(
        DWORD streamId,
        IMFMediaType* type) override {
        if (streamId != 0 || !type) return E_INVALIDARG;
        return m_stream ? m_stream->setType(type) : MF_E_SHUTDOWN;
    }

    HRESULT STDMETHODCALLTYPE GetService(
        REFGUID /*serviceIdentifier*/,
        REFIID /*riid*/,
        LPVOID* object) override {
        if (!object) return E_POINTER;
        *object = nullptr;
        return MF_E_UNSUPPORTED_SERVICE;
    }

    NTSTATUS STDMETHODCALLTYPE KsProperty(
        PKSPROPERTY property,
        ULONG propertyLength,
        LPVOID propertyData,
        ULONG dataLength,
        ULONG* bytesReturned) override {
        if (!property || !bytesReturned) {
            return static_cast<NTSTATUS>(E_POINTER);
        }
        (void)propertyLength;
        (void)propertyData;
        (void)dataLength;
        *bytesReturned = 0;
        return static_cast<NTSTATUS>(
            HRESULT_FROM_WIN32(ERROR_SET_NOT_FOUND));
    }

    NTSTATUS STDMETHODCALLTYPE KsMethod(
        PKSMETHOD method,
        ULONG methodLength,
        LPVOID methodData,
        ULONG dataLength,
        ULONG* bytesReturned) override {
        if (!method || !bytesReturned) {
            return static_cast<NTSTATUS>(E_POINTER);
        }
        (void)methodLength;
        (void)methodData;
        (void)dataLength;
        *bytesReturned = 0;
        return static_cast<NTSTATUS>(
            HRESULT_FROM_WIN32(ERROR_SET_NOT_FOUND));
    }

    NTSTATUS STDMETHODCALLTYPE KsEvent(
        PKSEVENT event,
        ULONG eventLength,
        LPVOID eventData,
        ULONG dataLength,
        ULONG* bytesReturned) override {
        if (!bytesReturned) {
            return static_cast<NTSTATUS>(E_POINTER);
        }
        (void)event;
        (void)eventLength;
        (void)eventData;
        (void)dataLength;
        *bytesReturned = 0;
        return static_cast<NTSTATUS>(
            HRESULT_FROM_WIN32(ERROR_SET_NOT_FOUND));
    }

private:
    ~MediaSource() override {
        Shutdown();
        --g_objectCount;
    }

    std::atomic<ULONG> m_refs{1};
    std::mutex m_mutex;
    ComPtr<IMFMediaEventQueue> m_events;
    ComPtr<IMFPresentationDescriptor> m_presentation;
    ComPtr<IMFAttributes> m_sourceAttributes;
    MediaStream* m_stream = nullptr;
};

class Activator final : public IMFActivate {
public:
    Activator() {
        ++g_objectCount;
    }

    HRESULT initialize() {
        return MFCreateAttributes(&m_attributes, 8);
    }

    // IUnknown + IMFAttributes
    HRESULT STDMETHODCALLTYPE QueryInterface(
        REFIID riid,
        void** ppv) override {
        if (!ppv) return E_POINTER;
        *ppv = nullptr;

        if (riid == IID_IUnknown ||
            riid == IID_IMFAttributes ||
            riid == IID_IMFActivate) {
            *ppv = static_cast<IMFActivate*>(this);
            AddRef();
            return S_OK;
        }

        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override {
        return static_cast<ULONG>(++m_refs);
    }

    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG value = static_cast<ULONG>(--m_refs);
        if (value == 0) delete this;
        return value;
    }

#define FORWARD_ATTR1(method, declaration, arguments)     HRESULT STDMETHODCALLTYPE method declaration override {         return m_attributes ? m_attributes->method arguments : MF_E_SHUTDOWN;     }

    FORWARD_ATTR1(
        GetItem,
        (REFGUID key, PROPVARIANT* value),
        (key, value))
    FORWARD_ATTR1(
        GetItemType,
        (REFGUID key, MF_ATTRIBUTE_TYPE* type),
        (key, type))
    FORWARD_ATTR1(
        CompareItem,
        (REFGUID key, const PROPVARIANT& value, BOOL* result),
        (key, value, result))
    FORWARD_ATTR1(
        Compare,
        (IMFAttributes* theirs, MF_ATTRIBUTES_MATCH_TYPE type, BOOL* result),
        (theirs, type, result))
    FORWARD_ATTR1(
        GetUINT32,
        (REFGUID key, UINT32* value),
        (key, value))
    FORWARD_ATTR1(
        GetUINT64,
        (REFGUID key, UINT64* value),
        (key, value))
    FORWARD_ATTR1(
        GetDouble,
        (REFGUID key, double* value),
        (key, value))
    FORWARD_ATTR1(
        GetGUID,
        (REFGUID key, GUID* value),
        (key, value))
    FORWARD_ATTR1(
        GetStringLength,
        (REFGUID key, UINT32* length),
        (key, length))
    FORWARD_ATTR1(
        GetString,
        (REFGUID key, LPWSTR value, UINT32 size, UINT32* length),
        (key, value, size, length))
    FORWARD_ATTR1(
        GetAllocatedString,
        (REFGUID key, LPWSTR* value, UINT32* length),
        (key, value, length))
    FORWARD_ATTR1(
        GetBlobSize,
        (REFGUID key, UINT32* size),
        (key, size))
    FORWARD_ATTR1(
        GetBlob,
        (REFGUID key, UINT8* buffer, UINT32 size, UINT32* copied),
        (key, buffer, size, copied))
    FORWARD_ATTR1(
        GetAllocatedBlob,
        (REFGUID key, UINT8** buffer, UINT32* size),
        (key, buffer, size))
    FORWARD_ATTR1(
        GetUnknown,
        (REFGUID key, REFIID riid, LPVOID* object),
        (key, riid, object))
    FORWARD_ATTR1(
        SetItem,
        (REFGUID key, const PROPVARIANT& value),
        (key, value))
    FORWARD_ATTR1(
        DeleteItem,
        (REFGUID key),
        (key))
    FORWARD_ATTR1(
        DeleteAllItems,
        (),
        ())
    FORWARD_ATTR1(
        SetUINT32,
        (REFGUID key, UINT32 value),
        (key, value))
    FORWARD_ATTR1(
        SetUINT64,
        (REFGUID key, UINT64 value),
        (key, value))
    FORWARD_ATTR1(
        SetDouble,
        (REFGUID key, double value),
        (key, value))
    FORWARD_ATTR1(
        SetGUID,
        (REFGUID key, REFGUID value),
        (key, value))
    FORWARD_ATTR1(
        SetString,
        (REFGUID key, LPCWSTR value),
        (key, value))
    FORWARD_ATTR1(
        SetBlob,
        (REFGUID key, const UINT8* buffer, UINT32 size),
        (key, buffer, size))
    FORWARD_ATTR1(
        SetUnknown,
        (REFGUID key, IUnknown* unknown),
        (key, unknown))
    FORWARD_ATTR1(
        LockStore,
        (),
        ())
    FORWARD_ATTR1(
        UnlockStore,
        (),
        ())
    FORWARD_ATTR1(
        GetCount,
        (UINT32* count),
        (count))
    FORWARD_ATTR1(
        GetItemByIndex,
        (UINT32 index, GUID* key, PROPVARIANT* value),
        (index, key, value))
    FORWARD_ATTR1(
        CopyAllItems,
        (IMFAttributes* destination),
        (destination))

#undef FORWARD_ATTR1

    HRESULT STDMETHODCALLTYPE ActivateObject(
        REFIID riid,
        void** object) override {
        if (!object) return E_POINTER;
        *object = nullptr;

        auto* source = new MediaSource();
        const HRESULT initResult = source->initialize();
        if (FAILED(initResult)) {
            source->Release();
            return initResult;
        }

        const HRESULT queryResult =
            source->QueryInterface(riid, object);
        source->Release();
        return queryResult;
    }

    HRESULT STDMETHODCALLTYPE ShutdownObject() override {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE DetachObject() override {
        return S_OK;
    }

private:
    ~Activator() override {
        --g_objectCount;
    }

    std::atomic<ULONG> m_refs{1};
    ComPtr<IMFAttributes> m_attributes;
};

class ClassFactory final : public IClassFactory {
public:
    ClassFactory() {
        ++g_objectCount;
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(
        REFIID riid,
        void** ppv) override {
        if (!ppv) return E_POINTER;
        *ppv = nullptr;

        if (riid == IID_IUnknown || riid == IID_IClassFactory) {
            *ppv = static_cast<IClassFactory*>(this);
            AddRef();
            return S_OK;
        }

        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override {
        return static_cast<ULONG>(++m_refs);
    }

    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG value = static_cast<ULONG>(--m_refs);
        if (value == 0) delete this;
        return value;
    }

    HRESULT STDMETHODCALLTYPE CreateInstance(
        IUnknown* outer,
        REFIID riid,
        void** object) override {
        if (outer) return CLASS_E_NOAGGREGATION;
        if (!object) return E_POINTER;
        *object = nullptr;

        auto* activator = new Activator();
        const HRESULT initResult = activator->initialize();
        if (FAILED(initResult)) {
            activator->Release();
            return initResult;
        }

        const HRESULT queryResult =
            activator->QueryInterface(riid, object);
        activator->Release();
        return queryResult;
    }

    HRESULT STDMETHODCALLTYPE LockServer(BOOL lock) override {
        if (lock) {
            ++g_objectCount;
        } else {
            --g_objectCount;
        }
        return S_OK;
    }

private:
    ~ClassFactory() override {
        --g_objectCount;
    }

    std::atomic<ULONG> m_refs{1};
};

HRESULT setStringValue(
    HKEY key,
    LPCWSTR name,
    LPCWSTR value) {
    return RegSetValueExW(
               key,
               name,
               0,
               REG_SZ,
               reinterpret_cast<const BYTE*>(value),
               static_cast<DWORD>((wcslen(value) + 1) * sizeof(WCHAR)))
        == ERROR_SUCCESS
        ? S_OK
        : HRESULT_FROM_WIN32(GetLastError());
}

} // namespace

extern "C" HRESULT __declspec(dllexport) DllGetClassObject(
    REFCLSID rclsid,
    REFIID riid,
    LPVOID* ppv) {
    if (!ppv) return E_POINTER;
    *ppv = nullptr;

    if (rclsid != CLSID_WeaRVirtualCamera) {
        return CLASS_E_CLASSNOTAVAILABLE;
    }

    auto* factory = new ClassFactory();
    const HRESULT hr = factory->QueryInterface(riid, ppv);
    factory->Release();
    return hr;
}

extern "C" HRESULT __declspec(dllexport) DllCanUnloadNow() {
    return g_objectCount.load() == 0 ? S_OK : S_FALSE;
}

extern "C" HRESULT __declspec(dllexport) DllRegisterServer() {
    WCHAR modulePath[MAX_PATH]{};
    if (GetModuleFileNameW(
            g_module,
            modulePath,
            static_cast<DWORD>(std::size(modulePath))) == 0) {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    WCHAR clsidPath[128]{};
    if (StringFromGUID2(
            CLSID_WeaRVirtualCamera,
            clsidPath,
            static_cast<int>(std::size(clsidPath))) == 0) {
        return E_FAIL;
    }

    std::wstring keyPath =
        L"Software\\Classes\\CLSID\\" +
        std::wstring(clsidPath);

    HKEY clsidKey = nullptr;
    DWORD disposition = 0;
    LONG result = RegCreateKeyExW(
        HKEY_LOCAL_MACHINE,
        keyPath.c_str(),
        0,
        nullptr,
        REG_OPTION_NON_VOLATILE,
        KEY_WRITE,
        nullptr,
        &clsidKey,
        &disposition);
    if (result != ERROR_SUCCESS) {
        return HRESULT_FROM_WIN32(result);
    }

    setStringValue(
        clsidKey,
        nullptr,
        L"WeaR Studio Virtual Camera");

    HKEY inprocKey = nullptr;
    result = RegCreateKeyExW(
        clsidKey,
        L"InprocServer32",
        0,
        nullptr,
        REG_OPTION_NON_VOLATILE,
        KEY_WRITE,
        nullptr,
        &inprocKey,
        &disposition);

    RegCloseKey(clsidKey);

    if (result != ERROR_SUCCESS) {
        return HRESULT_FROM_WIN32(result);
    }

    setStringValue(inprocKey, nullptr, modulePath);
    setStringValue(inprocKey, L"ThreadingModel", L"Both");
    RegCloseKey(inprocKey);

    return S_OK;
}

extern "C" HRESULT __declspec(dllexport) DllUnregisterServer() {
    WCHAR clsidPath[128]{};
    if (StringFromGUID2(
            CLSID_WeaRVirtualCamera,
            clsidPath,
            static_cast<int>(std::size(clsidPath))) == 0) {
        return E_FAIL;
    }

    const std::wstring keyPath =
        L"Software\\Classes\\CLSID\\" +
        std::wstring(clsidPath);

    const LONG result =
        SHDeleteKeyW(HKEY_LOCAL_MACHINE, keyPath.c_str());

    if (result == ERROR_FILE_NOT_FOUND) {
        return S_OK;
    }
    return result == ERROR_SUCCESS
        ? S_OK
        : HRESULT_FROM_WIN32(result);
}

BOOL APIENTRY DllMain(
    HMODULE module,
    DWORD reason,
    LPVOID reserved) {
    (void)reserved;

    if (reason == DLL_PROCESS_ATTACH) {
        g_module = module;
        DisableThreadLibraryCalls(module);
    }

    return TRUE;
}
