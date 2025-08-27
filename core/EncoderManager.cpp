#include "EncoderManager.h"

EncoderManager::EncoderManager(QObject* parent) : QObject(parent) {}

bool EncoderManager::startEncoding() {
    // TODO: Integrate FFmpeg for encoding
    return true;
}

void EncoderManager::stopEncoding() {
    // TODO: Stop encoding
}
