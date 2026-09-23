#include "VirtualCameraProtocol.h"

#include <QDebug>

#include <cstring>

int main() {
    using namespace WeaR::VirtualCameraProtocol;

    if (std::memcmp(kMagic, "WVC1", 4) != 0 ||
        kVersion != 1 ||
        kWidth != 1280 ||
        kHeight != 720 ||
        kStride != 5120 ||
        kBgraBytes != 1280U * 720U * 4U ||
        kNv12Bytes != 1280U * 720U * 3U / 2U) {
        qCritical() << "Virtual camera protocol constants are invalid";
        return 1;
    }

    if (sizeof(FrameHeader) != 32) {
        qCritical() << "Unexpected virtual camera frame header size";
        return 1;
    }

    qDebug() << "VIRTUAL_CAMERA_PROTOCOL: PASS";
    return 0;
}
