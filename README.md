# WeaR Studio

<p align="center">
  <img src="docs/images/logo.png" alt="WeaR Studio Logo" width="200"/>
</p>

<p align="center">
  <strong>Professional Open-Source Streaming Software</strong>
</p>

<p align="center">
  <a href="#features">Features</a> •
  <a href="#installation">Installation</a> •
  <a href="#building">Building</a> •
  <a href="#usage">Usage</a> •
  <a href="#plugins">Plugins</a> •
  <a href="#contributing">Contributing</a> •
  <a href="#license">License</a>
</p>

<p align="center">
  <img src="https://img.shields.io/badge/version-0.1-blue.svg" alt="Version">
  <img src="https://img.shields.io/badge/platform-Windows-lightgrey.svg" alt="Platform">
  <img src="https://img.shields.io/badge/license-GPL--2.0-green.svg" alt="License">
  <img src="https://img.shields.io/badge/Qt-6.10-41CD52.svg" alt="Qt">
  <img src="https://img.shields.io/badge/FFmpeg-6.0-007808.svg" alt="FFmpeg">
</p>

---

## Overview

WeaR Studio is a professional, open-source streaming and recording application built with modern C++20, Qt 6.10, and FFmpeg. Inspired by OBS Studio, it provides a modular architecture with plugin support for maximum extensibility.

### Key Technologies

| Technology | Purpose |
|------------|---------|
| **Qt 6.10** | UI Framework (Widgets, Docking) |
| **FFmpeg** | Video encoding (NVENC, x264) |
| **Windows Graphics Capture** | High-performance screen capture |
| **Direct3D 11** | GPU texture handling |
| **RTMP/FLV** | Live streaming protocol |

---

## Features

### Implemented (v0.1)

- **Screen Capture** — Windows Graphics Capture API with zero-copy GPU frames
- **Scene System** — Multiple scenes with layer-based composition
- **Hardware Encoding** — NVENC (NVIDIA) with libx264 fallback
- **RTMP Streaming** — Stream to Twitch, YouTube, Facebook, custom servers
- **Plugin System** — Dynamic plugin loading with Qt Plugin Loader
- **Dark Theme UI** — Professional OBS-like interface with docking

### Roadmap (v0.2+)

- Audio capture and mixing (WASAPI)
- GPU shader effects (Qt RHI)
- Local recording (MP4, MKV)
- Scene transitions
- Browser source
- Webcam support

---

## Installation

### Prerequisites

- Windows 10/11 (64-bit)
- NVIDIA GPU (for NVENC hardware encoding, optional)
- [Visual C++ Redistributable 2022](https://aka.ms/vs/17/release/vc_redist.x64.exe)

### Download

Download the latest release from the [Releases](https://github.com/RidTheWann/WeaR-studio/releases) page.

### Manual Installation

1. Extract the ZIP file to your desired location
2. Run `WeaRStudio.exe`

---

## Building

### Requirements

- **Visual Studio 2022** (or 2026) with C++ workload
- **Qt 6.10.1** (MSVC 2022 64-bit)
- **FFmpeg 6.0+** (with development libraries)
- **CMake 3.21+**

### Build Steps

```powershell
# Clone repository
git clone https://github.com/RidTheWann/WeaR-studio.git
cd WeaR-studio

# Configure with CMake
cmake -B build -G "Visual Studio 17 2022" -A x64 `
      -DCMAKE_PREFIX_PATH="C:/Qt/6.10.1/msvc2022_64" `
      -DFFMPEG_ROOT="C:/ffmpeg"

# Build Release
cmake --build build --config Release

# Copy FFmpeg DLLs (REQUIRED)
Copy-Item C:/ffmpeg/bin/*.dll -Destination build/bin/Release/
```

### Run

```powershell
cd build/bin/Release
./WeaRStudio.exe
```

---

## Usage

### Quick Start

1. **Launch** WeaR Studio
2. **Add a Scene** — Click `+` in the Scenes panel
3. **Add a Source** — Click `+` in the Sources panel, select "Screen Capture"
4. **Configure Stream** — Enter your RTMP URL and stream key in Controls
5. **Start Streaming** — Click "Start Streaming"

### Stream Settings

| Service | URL Example |
|---------|-------------|
| Twitch | `rtmp://live.twitch.tv/app` |
| YouTube | `rtmp://a.rtmp.youtube.com/live2` |
| Facebook | `rtmp://live-api-s.facebook.com:443/rtmp/` |
| Custom | `rtmp://your-server.com/live` |

---

## Plugins

WeaR Studio supports dynamic plugins for extending functionality.

### Installing Plugins

1. Copy plugin `.dll` files to the `plugins/` folder next to the executable
2. Restart WeaR Studio
3. Plugins will appear in the Sources/Filters list

### Creating Plugins

See [Plugin Development Guide](docs/ARCHITECTURE.md#plugin-development-guide) for detailed instructions.

```cpp
// Example: Minimal source plugin
class MySource : public QObject, public WeaR::ISource {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID WEAR_SOURCE_IID FILE "MySource.json")
    Q_INTERFACES(WeaR::ISource)
    // ... implement ISource interface
};
```

---

## Project Structure

```
WeaR-studio/
├── core/                    # Backend engine
│   ├── CaptureManager.*     # Screen capture (WinRT)
│   ├── EncoderManager.*     # Video encoding (FFmpeg)
│   ├── StreamManager.*      # RTMP streaming
│   ├── SceneManager.*       # Scene composition
│   ├── PluginManager.*      # Plugin system
│   ├── IPlugin.h            # Plugin interface
│   ├── ISource.h            # Source interface
│   └── IFilter.h            # Filter interface
├── ui/                      # Qt user interface
│   ├── WeaRApp.*            # Application & theme
│   ├── MainWindow.*         # Main window
│   └── PreviewWidget.*      # Video preview
├── plugins/                 # Example plugins
├── cmake/                   # CMake modules
└── docs/                    # Documentation
```

---

## Contributing

We welcome contributions! Please read our [Contributing Guide](CONTRIBUTING.md) before submitting pull requests.

### Quick Contribution Steps

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/amazing-feature`)
3. Commit your changes (`git commit -m 'Add amazing feature'`)
4. Push to the branch (`git push origin feature/amazing-feature`)
5. Open a Pull Request

---

## License

WeaR Studio is licensed under the **GNU General Public License v2.0** (GPL-2.0).

See [LICENSE](LICENSE) for the full license text.

---

## Acknowledgments

- [OBS Studio](https://obsproject.com/) — Inspiration for architecture
- [Qt Project](https://www.qt.io/) — UI framework
- [FFmpeg](https://ffmpeg.org/) — Video encoding
- [Microsoft](https://docs.microsoft.com/en-us/windows/uwp/audio-video-camera/screen-capture) — Windows Graphics Capture API

---

## Author

**RidTheWann** — Creator and Lead Developer

- GitHub: [@RidTheWann](https://github.com/RidTheWann)

---

