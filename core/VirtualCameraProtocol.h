#pragma once
// =============================================================================
// WeaR-studio virtual camera frame transport protocol
// Intentionally free of Qt/CEF dependencies so the Frame Server COM source
// can share it with the Qt application.
// =============================================================================

#include <cstdint>

namespace WeaR::VirtualCameraProtocol {

inline constexpr wchar_t kPipeName[] = LR"(\\.\pipe\WeaRStudio.VirtualCamera)";
inline constexpr char kMagic[4] = {'W', 'V', 'C', '1'};
inline constexpr std::uint32_t kVersion = 1;
inline constexpr std::uint32_t kWidth = 1280;
inline constexpr std::uint32_t kHeight = 720;
inline constexpr std::uint32_t kBytesPerPixel = 4;
inline constexpr std::uint32_t kStride = kWidth * kBytesPerPixel;
inline constexpr std::uint32_t kBgraBytes = kStride * kHeight;
inline constexpr std::uint32_t kNv12Bytes = kWidth * kHeight * 3 / 2;

#pragma pack(push, 1)
struct FrameHeader {
    char magic[4];
    std::uint32_t version;
    std::uint32_t width;
    std::uint32_t height;
    std::uint32_t stride;
    std::uint32_t dataBytes;
    std::int64_t timestamp100ns;
};
#pragma pack(pop)

static_assert(sizeof(FrameHeader) == 32);

} // namespace WeaR::VirtualCameraProtocol
