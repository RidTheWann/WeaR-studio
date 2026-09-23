# Windows 11 Virtual Camera

WeaR Studio's virtual-camera output uses the Windows 11 Media Foundation Virtual Camera API. The design does not require a kernel-mode camera driver for the current Windows 11 path.

## Runtime architecture

SceneManager renders one composited QImage per frame and calls VirtualCameraManager::pushFrame(). That call only swaps the latest frame reference under a short mutex.

A dedicated named-pipe server thread exposes the latest composited frame to the Media Foundation frame-server process. The COM DLL WeaRVirtualCamera.dll is activated by Windows Frame Server using a registered CLSID. Its IMFMediaSourceEx / IMFMediaStream2 implementation requests the latest BGRA frame and converts it to NV12 1280x720.

The camera is registered with:
- MFVirtualCameraType_SoftwareCameraSource
- session lifetime
- current-user access
- source CLSID {A5E4C9E0-0F54-4A2E-9C10-74E1F6E4DCD1}

## Installation

The media-source COM DLL must be registered under HKLM because Windows Frame Server services may load it from service processes. Do not register the DLL from a user-only build directory that the Frame Server cannot access.

Build Release first, then run PowerShell as administrator:

powershell -ExecutionPolicy Bypass -File tools/register_virtual_camera.ps1

The script copies the x64 DLL to C:\Program Files\WeaR Studio\ and invokes the 64-bit regsvr32.exe.

## Application use

Start WeaR Studio and click Start Virtual Camera in the Controls panel. The device is named WeaR Studio Virtual Camera (Windows Virtual Camera).

The virtual camera exists only for the current WeaR Studio session. Stop it before exiting the application; the shutdown path removes the device and closes the named-pipe server.

## Compatibility

The virtual camera is intended for Windows 11 applications that enumerate Media Foundation / Windows virtual cameras, including video-conference software. Final validation should be performed manually in Windows Camera plus the target Zoom/Teams/Google Meet build because those applications and their camera pipelines vary by release.

## Security

The frame pipe ACL grants access to LocalSystem, LocalService, and authenticated users because the Windows Frame Server can run under service identities. The pipe carries only the current composited video frame.

## References

- Microsoft Media Foundation Virtual Camera API
- Microsoft Windows-Camera VirtualCamera sample
- Windows 11 MFCreateVirtualCamera / IMFVirtualCamera documentation
