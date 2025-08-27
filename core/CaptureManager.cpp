#include "CaptureManager.h"

CaptureManager::CaptureManager(QObject* parent) : QObject(parent) {}

bool CaptureManager::startScreenCapture() {
    // TODO: Implement screen capture (platform-specific)
    return true;
}

bool CaptureManager::startWebcamCapture() {
    // TODO: Implement webcam capture (DirectShow/V4L2/AVFoundation)
    return true;
}

bool CaptureManager::startAudioCapture() {
    // TODO: Implement audio capture (mic/system audio)
    return true;
}

void CaptureManager::stopAll() {
    // TODO: Stop all captures
}
