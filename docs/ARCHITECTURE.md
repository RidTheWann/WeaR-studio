# WeaR Studio Architecture

> **Version:** 0.1
> **Last Updated:** 2026-09-23
> **Platform:** Windows 10/11 (64-bit)

---

## Table of Contents

1. [System Overview](#system-overview)
2. [Data Flow Pipeline](#data-flow-pipeline)
3. [Core Managers](#core-managers)
4. [Plugin System](#plugin-system)
5. [User Interface](#user-interface)
6. [Build & Run Instructions](#build--run-instructions)
7. [Plugin Development Guide](#plugin-development-guide)
8. [Phase 5 Roadmap](#phase-5-roadmap)

---

## System Overview

WeaR Studio is a professional streaming application built with **Qt 6.10** and **FFmpeg**, designed with an architecture similar to OBS Studio. The system follows a modular singleton pattern where each manager handles a specific responsibility in the streaming pipeline.

### High-Level Architecture

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                              WeaR Studio                                     │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│  ┌──────────────┐    ┌──────────────┐    ┌──────────────┐    ┌────────────┐ │
│  │   Capture    │───▶│    Scene     │───▶│   Encoder    │───▶│   Stream   │ │
│  │   Manager    │    │   Manager    │    │   Manager    │    │   Manager  │ │
│  └──────────────┘    └──────────────┘    └──────────────┘    └────────────┘ │
│         │                   │                   │                   │        │
│         ▼                   ▼                   ▼                   ▼        │
│  ┌──────────────┐    ┌──────────────┐    ┌──────────────┐    ┌────────────┐ │
│  │  WinRT GC    │    │  QPainter    │    │   FFmpeg     │    │   RTMP     │ │
│  │  D3D11       │    │  Compositor  │    │ NVENC + AAC  │    │   FLV      │ │
│  └──────────────┘    └──────────────┘    └──────────────┘    └────────────┘ │
│                             ▲                   ▲                            │
│  ┌──────────────┐           │                   │                            │
│  │ AudioCapture │           │                   │                            │
│  │ Loopback/Mic │──▶┌──────────────┐            │                            │
│  └──────────────┘   │  AudioMixer  │────────────┘                            │
│                     └──────────────┘                                         │
│                                                                              │
│  ┌──────────────────────────────────────────────────────────────────────┐   │
│  │                         Plugin Manager                                │   │
│  │  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐                   │   │
│  │  │ ISource     │  │ IFilter     │  │ IPlugin     │                   │   │
│  │  │ Plugins     │  │ Plugins     │  │ Interface   │                   │   │
│  │  └─────────────┘  └─────────────┘  └─────────────┘                   │   │
│  └──────────────────────────────────────────────────────────────────────┘   │
│                                                                              │
└─────────────────────────────────────────────────────────────────────────────┘
```

### Technology Stack

| Component | Technology |
|-----------|------------|
| UI Framework | Qt 6.10 (Widgets, QDockWidget) |
| Build System | CMake 3.21+ |
| Compiler | MSVC v143 (Visual Studio 2022/2026) |
| Video Capture | Windows Graphics Capture API (WinRT) |
| Audio Capture | Windows WASAPI (Loopback + Microphone) |
| Video Encoder | FFmpeg + NVENC (h264_nvenc) / libx264 fallback |
| Audio Encoder | FFmpeg AAC (native AAC + libswresample) |
| GPU Interop | Direct3D 11 |
| Streaming | FFmpeg libavformat (RTMP/FLV with A/V muxing) |
| Plugin System | Qt Plugin Loader |

---

## Data Flow Pipeline

The streaming pipeline follows a synchronized video & audio data flow:

```
Video:
┌────────────┐     ┌────────────┐     ┌────────────┐     ┌────────────┐
│  CAPTURE   │────▶│  COMPOSE   │────▶│   ENCODE   │────▶│   STREAM   │
│            │     │            │     │            │     │            │
│ D3D11      │     │ QPainter   │     │ NVENC      │     │ RTMP       │
│ Texture    │     │ QImage     │     │ AVPacket   │     │ TCP/IP     │
└────────────┘     └────────────┘     └────────────┘     │            │
      │                  │                  │            │            │
      ▼                  ▼                  ▼            │            │
  GPU Memory         CPU Memory          GPU Memory      │            │
  (Zero-Copy)        (Composition)       (HW Encode)     │            │
                                                         │   FLV Mux  │
Audio:                                                   │   (Video   │
┌────────────┐     ┌────────────┐     ┌────────────┐     │  + Audio)  │
│  WASAPI    │────▶│ AUDIO MIX  │────▶│ AAC ENCODE │────▶│            │
│            │     │            │     │            │     │            │
│ Loopback + │     │ Multi-track│     │ libswresample│    │            │
│ Mic Capture│     │ Volume/Mute│     │ AAC Packet │     │            │
└────────────┘     └────────────┘     └────────────┘     └────────────┘
```

### Frame Lifecycle

1. **Capture**: `CaptureManager` captures frames using Windows Graphics Capture API
   - Frames remain on GPU as `ID3D11Texture2D`
   - Zero-copy for maximum performance

2. **Compose**: `SceneManager` composites all scene items
   - Renders at 60 FPS via QTimer
   - Uses QPainter with `QImage::Format_ARGB32_Premultiplied`
   - Fans out the same composed frame to Preview, Stream encoder, and/or RecordingManager as enabled

3. **Encode**: `EncoderManager` compresses frames for streaming
   - Converts QImage to YUV (NV12/YUV420P) via swscale
   - Encodes using NVENC or libx264 fallback
   - Outputs `AVPacket` via callback

4. **Stream**: `StreamManager` transmits to RTMP server
   - Muxes packets into FLV container
   - Handles reconnection automatically

5. **Record**: `RecordingManager` independently encodes and muxes local output
   - Own video encoder configuration and bitrate
   - Own AAC encoder configuration
   - Writes MP4, MKV, or FLV without touching StreamManager

---

## Core Managers

All managers follow the **Thread-Safe Singleton** pattern:

```cpp
// Access pattern
auto& capture = CaptureManager::instance();
auto& scene = SceneManager::instance();
auto& encoder = EncoderManager::instance();
auto& stream = StreamManager::instance();
auto& plugins = PluginManager::instance();
```

### CaptureManager

**File:** `core/CaptureManager.h/.cpp`

**Purpose:** High-performance screen/window capture using Windows Graphics Capture API.

**Key Features:**
- Implements `ISource` interface
- Zero-copy GPU capture via D3D11
- Monitor and window enumeration
- Cursor and border visibility control

```cpp
// Usage
auto targets = capture.enumerateMonitors();
capture.setTarget(targets.first());
capture.start();
VideoFrame frame = capture.captureVideoFrame();
```

### SceneManager

**Files:** `core/SceneManager.h/.cpp`, `core/Scene.h/.cpp`, `core/SceneItem.h/.cpp`

**Purpose:** Manages scenes and runs the render loop for video composition.

**Key Features:**
- Multiple scene support
- Layer-based composition (SceneItem)
- Transform properties (position, scale, rotation, opacity)
- Preview callback for UI
- Encoder output integration

```cpp
// Usage
Scene* scene = SceneManager::instance().createScene("Main");
scene->addItem("Screen", &CaptureManager::instance());
SceneManager::instance().startRenderLoop();
```

### EncoderManager

**File:** `core/EncoderManager.h/.cpp`

**Purpose:** Hardware-accelerated video encoding plus AAC audio encoding using FFmpeg.

**Key Features:**
- NVENC primary, libx264 fallback
- Async encoding thread with frame queue
- CBR/VBR/CRF rate control
- Low-latency streaming presets
- Native FFmpeg AAC encoder with libswresample conversion to the encoder sample format
- Interleaved float audio FIFO with sample-based audio PTS for stable long-running A/V timing
- Separate `EncodedPacket::isAudio` packets routed through the same output callback

```cpp
// Usage
EncoderSettings settings;
settings.width = 1920;
settings.height = 1080;
settings.bitrate = 6000;
encoder.configure(settings);
encoder.setPacketCallback([](const EncodedPacket& pkt) { ... });
encoder.start();
```

### StreamManager

**File:** `core/StreamManager.h/.cpp`

**Purpose:** RTMP streaming output using FFmpeg libavformat.

**Key Features:**
- FLV muxing for RTMP compatibility
- State machine (Stopped → Connecting → Streaming)
- Automatic reconnection
- Timestamp rescaling (`av_packet_rescale_ts`)
- Service presets (Twitch, YouTube, etc.)
- Separate FLV video/audio streams with independent time-base rescaling
- Audio settings (`audioSampleRate`, `audioChannels`, `audioBitrate`) are applied to the AAC/FLV path

```cpp
// Usage
StreamSettings settings;
settings.url = "rtmp://live.twitch.tv/app";
settings.streamKey = "your_key";
stream.configure(settings);
stream.startStream();
```

### RecordingManager

**File:** `core/RecordingManager.h/.cpp`

**Purpose:** Independent local recording pipeline that consumes the same composed render ticks as the streaming path without sharing its encoder or muxer.

**Key Features:**
- Thread-safe Singleton (`RecordingManager::instance()`)
- Independent start/stop/pause/resume lifecycle
- MP4, MKV, and FLV local output
- Recording-specific video bitrate, rate-control, preset, encoder, and audio bitrate
- Dedicated FFmpeg video/AAC encoders and muxer worker thread
- Same composited frame and mixed-audio tick is fanned out once from `SceneManager`
- Bounded raw-frame queue so slow disk/encoding cannot grow memory without limit
- Pause excludes paused wall time from the displayed recording duration and keeps output timestamps contiguous

```cpp
auto& recorder = RecordingManager::instance();

RecordingSettings settings;
settings.format = RecordingFormat::MKV;
settings.videoBitrate = 12000;
settings.audioBitrate = 192;

recorder.configure(settings);
recorder.startRecording();
```

### PluginManager

**File:** `core/PluginManager.h/.cpp`

**Purpose:** Dynamic plugin discovery and loading.

**Key Features:**
- QPluginLoader-based DLL loading
- Plugin categorization (Source, Filter)
- Factory pattern for instance creation
- Plugin lifecycle management

```cpp
// Usage
plugins.discoverPlugins();  // Scans ./plugins/*.dll
plugins.loadAllPlugins();
ISource* colorSource = plugins.createSource("wear.source.color");
```

### AudioMixer

**File:** `core/AudioMixer.h/.cpp`

**Purpose:** Multi-track audio mixing, per-track volume, mute, VU metering, and soft limiting.

**Key Features:**
- Thread-safe Singleton (`AudioMixer::instance()`)
- Multi-track mixing at 48kHz stereo float
- Per-track volume gain and mute control
- Soft peak limiter / clamping preventing clipping
- Peak decay ballistics and `levelsUpdated` signal for UI VU meters
- Aligned tick-by-tick with SceneManager render loop (e.g. 800 samples per tick at 60fps)

```cpp
// Usage
auto& mixer = AudioMixer::instance();
int trackId = mixer.addTrack(desktopAudioSource);
mixer.setTrackVolume(trackId, 0.8f);
mixer.setTrackMuted(trackId, false);
AudioFrame mixed = mixer.mixTracks(800);
```

### AudioCaptureSource (WASAPI)

**File:** `core/AudioCaptureSource.h/.cpp`

**Purpose:** Windows WASAPI loopback capture (speaker output) and microphone input.

**Key Features:**
- Implements `ISource` interface (`captureAudioFrame`)
- Desktop audio loopback (`AUDCLNT_STREAMFLAGS_LOOPBACK`)
- Low latency event-driven WASAPI capture loop
- High-quality linear resampling from device native mix format to standard 48kHz stereo float
- Thread-safe FIFO audio buffer

---

## Recording / Streaming Independence

The two output paths are intentionally independent:

```text
                         ┌──> EncoderManager ──> StreamManager ──> RTMP
SceneManager ─> QImage ──┤
                         └──> RecordingManager ──> MP4 / MKV / FLV
                AudioMixer ────────────────┬──> stream audio
                                           └──> recording audio
```

Starting or stopping recording toggles only `SceneManager::setRecordingOutputEnabled()` and the RecordingManager. Starting or stopping streaming toggles only the stream encoder/output path. A recording therefore can run alone, streaming can run alone, or both can run at the same time.

Recording uses a dedicated FFmpeg encoder context so its bitrate/quality settings do not inherit or overwrite the live-stream encoder settings. The two encoders consume the same composited render tick rather than recompositing the scene.

## Plugin System

### Interface Hierarchy

```
IPlugin (Base Interface)
├── PluginInfo info()
├── initialize() / shutdown()
├── type() / capabilities()
└── settingsWidget()

ISource : IPlugin
├── VideoFrame captureVideoFrame()
├── start() / stop()
├── configure(SourceConfig)
└── nativeResolution() / nativeFps()

IFilter : IPlugin
├── processVideo(QImage)
├── processAudio(AudioFrame)
├── parameters()
└── setParameter(name, value)
```

### Plugin Registration

Plugins use Qt's plugin system:

```cpp
class MyPlugin : public QObject, public ISource {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID WEAR_SOURCE_IID FILE "MyPlugin.json")
    Q_INTERFACES(WeaR::ISource)
    ...
};
```

---

## User Interface

### MainWindow Layout

```
┌─────────────────────────────────────────────────────────────────────┐
│  File   View   Stream   Help                              [Menu Bar]│
├─────────────┬───────────────────────────────────────┬───────────────┤
│             │                                       │               │
│   SCENES    │                                       │   CONTROLS    │
│   ─────────│         PREVIEW WIDGET               │   ───────────│
│   Scene 1  │                                       │   URL: [____] │
│ ▶ Scene 2  │        ┌─────────────────────┐        │   Key: [____] │
│   Scene 3  │        │                     │        │               │
│             │        │   Video Preview     │        │ [Start Stream]│
│   [+] [-]  │        │                     │        │ [Settings]    │
│             │        └─────────────────────┘        │               │
├─────────────┤                                       │               │
│   SOURCES  │                                       │               │
│   ─────────│                                       │               │
│   Display  │                                       │               │
│   Camera   │                                       │               │
│             │                                       │               │
│   [+] [-]  │                                       │               │
├─────────────┴───────────────────────────────────────┴───────────────┤
│  Ready              FPS: 60.0     Bitrate: --     Duration: 00:00:00│
└─────────────────────────────────────────────────────────────────────┘
```

### Dark Theme

The application uses a professional dark theme:

- **Background:** #2D2D30 (Dark gray)
- **Accent:** #007ACC (Blue)
- **Text:** #DCDCDC (Light gray)
- **Selection:** #094771 (Dark blue)

---

## Build & Run Instructions

### Prerequisites

1. **Visual Studio 2022** (or 2026) with C++ workload
2. **Qt 6.10.1** (MSVC 2022 64-bit)
3. **FFmpeg** (prebuilt binaries with NVENC support)
4. **CMake 3.21+**

### Build Steps

```powershell
# Clone repository
git clone https://github.com/your-repo/WeaR-studio.git
cd WeaR-studio

# Configure with CMake
cmake -B build -G "Visual Studio 18 2026" -A x64 `
      -DCMAKE_PREFIX_PATH="C:/Qt/6.10.1/msvc2022_64" `
      -DFFMPEG_ROOT="C:/ffmpeg"

# Build
cmake --build build --config Release
```

### Runtime Dependencies

**CRITICAL:** Copy FFmpeg DLLs to the executable folder:

```powershell
# Copy FFmpeg DLLs
Copy-Item C:/ffmpeg/bin/*.dll -Destination build/bin/Release/
```

Required DLLs:
- `avcodec-*.dll`
- `avformat-*.dll`
- `avutil-*.dll`
- `swscale-*.dll`
- `swresample-*.dll`

### Plugin Folder Structure

```
build/bin/Release/
├── WeaRStudio.exe
├── avcodec-60.dll
├── avformat-60.dll
├── avutil-58.dll
├── swscale-7.dll
├── swresample-4.dll
├── Qt6Core.dll
├── Qt6Gui.dll
├── Qt6Widgets.dll
└── plugins/
    └── ExamplePlugin.dll    ← Plugins go here
```

### Running the Application

```powershell
cd build/bin/Release
./WeaRStudio.exe
```

---

## Plugin Development Guide

This guide shows how to create a new plugin for WeaR Studio.

### Step 1: Create Plugin Files

Create a new folder in `plugins/` with your plugin name:

```
plugins/
└── MyAwesomeSource/
    ├── MyAwesomeSource.h
    ├── MyAwesomeSource.cpp
    ├── MyAwesomeSource.json
    └── CMakeLists.txt
```

### Step 2: Implement the Interface

**MyAwesomeSource.h:**
```cpp
#pragma once
#include <ISource.h>
#include <QObject>
#include <QtPlugin>

namespace WeaR {

class MyAwesomeSource : public QObject, public ISource {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID WEAR_SOURCE_IID FILE "MyAwesomeSource.json")
    Q_INTERFACES(WeaR::ISource)

public:
    explicit MyAwesomeSource(QObject* parent = nullptr);
    ~MyAwesomeSource() override;

    // IPlugin interface
    PluginInfo info() const override;
    QString name() const override { return "My Awesome Source"; }
    QString version() const override { return "0.1"; }
    PluginType type() const override { return PluginType::Source; }
    PluginCapability capabilities() const override;
    
    bool initialize() override;
    void shutdown() override;
    bool isActive() const override { return m_initialized; }

    // ISource interface
    bool configure(const SourceConfig& config) override;
    SourceConfig config() const override { return m_config; }
    
    bool start() override;
    void stop() override;
    bool isRunning() const override { return m_running; }
    
    VideoFrame captureVideoFrame() override;
    
    QSize nativeResolution() const override { return m_config.resolution; }
    double nativeFps() const override { return 60.0; }
    QSize outputResolution() const override { return m_config.resolution; }
    double outputFps() const override { return m_config.fps; }

private:
    bool m_initialized = false;
    bool m_running = false;
    SourceConfig m_config;
    // Your custom members here
};

} // namespace WeaR
```

### Step 3: Create Plugin Metadata

**MyAwesomeSource.json:**
```json
{
    "Keys": ["wear.source.my-awesome"],
    "MetaData": {
        "name": "My Awesome Source",
        "version": "0.1",
        "author": "Your Name",
        "description": "Does something awesome",
        "type": "source"
    }
}
```

### Step 4: Add CMakeLists.txt

**CMakeLists.txt:**
```cmake
add_library(my_awesome_source MODULE
    MyAwesomeSource.cpp
    MyAwesomeSource.h
    MyAwesomeSource.json
)

target_link_libraries(my_awesome_source PRIVATE
    Qt6::Core
    Qt6::Widgets
    core
)

target_include_directories(my_awesome_source PRIVATE
    ${CMAKE_SOURCE_DIR}/core
)

set_target_properties(my_awesome_source PROPERTIES
    PREFIX ""
    OUTPUT_NAME "MyAwesomeSource"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/plugins"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/plugins"
)
```

### Step 5: Build & Test

```powershell
cmake --build build --config Release --target my_awesome_source
# Plugin will be in build/bin/plugins/MyAwesomeSource.dll
```

---

## Phase 5 Roadmap

### Top 3 Features for Next Development Phase

#### 1. 🎨 GPU Shader Effects (Qt RHI)

**Priority:** High

Replace QPainter composition with Qt RHI for GPU-accelerated rendering:

- **Benefits**: 60+ FPS composition without CPU bottleneck
- **Shader Effects**: Blur, chroma key, color correction, LUTs
- **Seamless Scaling**: Hardware scaling without quality loss

```cpp
// Proposed architecture
class RhiRenderer {
    void beginFrame();
    void drawTexture(QRhiTexture* texture, const QMatrix4x4& transform);
    void applyShader(const QString& shaderPath);
    void endFrame();
};
```

#### 2. 📼 Recording Module

**Priority:** High

Add local recording alongside streaming:

- **Dual Output**: Stream + Record simultaneously
- **File Formats**: MP4, MKV, FLV
- **Quality Options**: Separate bitrate settings for recording
- **Remux Support**: Convert recordings to other formats

```cpp
// Proposed interface
class RecordingManager : public QObject {
    bool startRecording(const QString& outputPath);
    void stopRecording();
    void setFormat(RecordingFormat format);
    void setBitrate(int kbps);
};
```

### Honorable Mentions

| Feature | Description |
|---------|-------------|
| **Transitions** | Scene transition effects (fade, slide, stinger) |
| **Browser Source** | Embedded Chromium for web overlays |
| **Virtual Camera** | Output as virtual webcam for Zoom/Teams |
| **Hotkeys** | Global keyboard shortcuts |
| **Profiles** | Save/load streaming configurations |
| **Multi-track Audio** | Separate audio tracks in recording |

---

## Conclusion

WeaR Studio provides a solid foundation for a professional streaming application. The modular architecture allows for easy extension through the plugin system, and the use of industry-standard technologies (FFmpeg, Qt, Direct3D) ensures compatibility and performance.

For questions or contributions, please open an issue on the GitHub repository.

---

*Documentation generated for WeaR Studio v0.1*


---

## Build & Audio Pipeline Audit — 2026-09-23

This audit supersedes stale build logs and documents the current source tree only.

### Clean-build requirements

The supported Windows toolchain is:

- Qt 6.10.1, MSVC 2022 x64
- FFmpeg 6.0 or newer
- CMake 3.21 or newer
- Visual Studio 2022 generator / MSVC v143

CI now deletes the build directory before configuration, configures from the repository root, builds **Debug and Release**, and runs the AudioMixer unit test in both configurations. Build logs are retained only as CI artifacts and are not committed.

The historical `build_output.txt` file is not part of the current repository tree. The `.gitignore` also excludes build directories, CMake cache/output, and build log files.

### Current audio data flow

```
WASAPI Desktop Loopback ─┐
                         ├─> AudioMixer ─> SceneManager 60 Hz tick
WASAPI Microphone ───────┘                    │
                                              ▼
                                      EncoderManager
                                      AAC + swresample
                                              │
                                EncodedPacket(isAudio=true)
                                              │
                                              ▼
                                      StreamManager
                                      FLV / RTMP mux
```

The mixer produces 48 kHz stereo interleaved float frames sized to the render tick. The AAC encoder accumulates these frames until the codec frame size is available, assigns PTS in the audio sample time base, and emits audio packets separately from video packets. StreamManager rescales audio timestamps from the AAC sample time base to the FLV stream time base before interleaving.

### Resource/lifetime invariants

- WASAPI COM objects are owned by the capture worker thread and released before COM uninitialization.
- Capture FIFO growth is bounded to approximately one second.
- FFmpeg `AVFrame`, `AVPacket`, `SwrContext`, `SwsContext`, and codec contexts have explicit cleanup paths.
- Stream packets are cloned before entering the asynchronous RTMP queue.
- `AudioMixer` owns track metadata only; source lifetime remains with the source owner.

### Validation status

Source-level inspection confirms that the obsolete `WeaR::IPluginFactory` type is not referenced by the current `PluginManager.h` and that `ISource`, `IFilter`, and `IPlugin` use Qt `Q_DECLARE_INTERFACE` declarations.

**CI/build status:** validated by the GitHub Actions workflow attached to the commit containing this audit.  
**Manual RTMP smoke test:** must be performed on a Windows development machine with Qt/FFmpeg runtime dependencies and a reachable RTMP test server. The repository automation cannot truthfully claim a manual GUI interaction or five-minute external RTMP playback verification.

Recommended smoke-test sequence:

1. Launch `WeaR-Studio.exe`.
2. Create a scene and add **Screen Capture**.
3. Confirm the preview updates continuously.
4. Confirm **Audio Mixer** shows Desktop Audio and Mic/Aux VU activity.
5. Configure a local RTMP endpoint such as MediaMTX.
6. Start streaming for at least five minutes.
7. Monitor the output with ffplay/VLC and verify continuous video + audio with no drift.
8. Stop streaming and confirm clean process shutdown.

### Known implementation scope

Audio capture currently targets the default Windows console render endpoint for desktop loopback and the default console capture endpoint for microphone input. Device-selection UI and advanced WASAPI format/event-mode configuration remain future work.
