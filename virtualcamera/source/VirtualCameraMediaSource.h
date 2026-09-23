#pragma once
// =============================================================================
// WeaR Studio Virtual Camera COM source
// Windows 11 Media Foundation software camera source.
// =============================================================================

#include <windows.h>
#include <guiddef.h>

inline constexpr wchar_t WEAR_VCAM_CLSID_STRING[] =
    L"{A5E4C9E0-0F54-4A2E-9C10-74E1F6E4DCD1}";

inline constexpr GUID CLSID_WeaRVirtualCamera =
{
    0xa5e4c9e0, 0x0f54, 0x4a2e,
    { 0x9c, 0x10, 0x74, 0xe1, 0xf6, 0xe4, 0xdc, 0xd1 }
};
