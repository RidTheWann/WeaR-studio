# WeaR Studio Documentation

## Overview
WeaR Studio is a cross-platform streaming and recording software inspired by OBS Studio, built with C++17, Qt6, FFmpeg, and CMake.

## Main Components
- **WeaRApp**: Application entry point
- **CaptureManager**: Handles screen, webcam, and audio capture
- **EncoderManager**: Manages encoding using FFmpeg
- **StreamManager**: Handles streaming (RTMP/WebRTC)
- **SceneManager**: Manages scenes, sources, filters, transitions
- **PluginManager**: Loads and manages plugins

## UI
Modern Qt6 interface with dockable panels, menus, and control buttons.

## Build Instructions
See `README.md` and `/cmake` for build configuration details.
