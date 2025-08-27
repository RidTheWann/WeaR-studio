# WeaR Studio

WeaR Studio is a cross-platform desktop software for live streaming and recording, inspired by OBS Studio/Streamlabs. Built with C++17, Qt6, FFmpeg, and CMake.

## Features
- Screen, webcam, and audio capture
- Scene system with sources, filters, transitions
- Streaming to YouTube, Twitch, Facebook, custom RTMP
- Local recording (MP4, MKV, FLV) with codec options
- Plugin API for extensibility
- Modern, dockable Qt UI

## Project Structure
- `/core` - Engine (capture, encoding, streaming)
- `/ui` - Qt interface
- `/plugins` - Example plugins
- `/cmake` - Build configuration
- `/docs` - Documentation

## Build Instructions
See `/docs` for details.
