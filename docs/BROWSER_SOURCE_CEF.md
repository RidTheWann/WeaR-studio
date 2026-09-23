# Browser Source — CEF + Qt 6.10 integration

## Architecture decision

WeaR Studio uses **Chromium Embedded Framework (CEF) Windowless/Off-Screen Rendering (OSR)** for Browser Source.

CEF OSR renders into a framebuffer through `CefRenderHandler::OnPaint`, which maps cleanly to the existing `ISource::captureVideoFrame()` contract. The first implementation intentionally copies the BGRA framebuffer into a `QImage`; CEF's D3D11 shared-texture path is reserved for a later zero-copy optimization.

## Qt 6.10 integration

CEF is initialized as an optional runtime before the main window is constructed. Browser subprocess handling calls `CefExecuteProcess` before the Qt application is created. The browser process then calls `CefInitialize` with `windowless_rendering_enabled=true` and `multi_threaded_message_loop=true`.

The Browser Source creates browsers asynchronously with `CefBrowserHost::CreateBrowser`. The synchronous variant is avoided because it is restricted to the CEF browser-process UI thread.

## Dependency policy

CEF is an external, optional dependency because its Windows binary distribution is large and tightly coupled to Chromium.

Configure with:

`cmake -B build ... -DWEAR_ENABLE_CEF=ON -DCEF_ROOT=C:/path/to/cef_binary_...`

The repository does not vendor the CEF distribution.

## Runtime deployment

The build copies the CEF runtime DLLs and Resources directory beside `WeaRStudio.exe`. The standard CEF binary distribution layout is used.

For production, CEF sandbox/bootstrap hardening and code-signing should be enabled before shipping a public installer.

## Verified research

The CEF documentation and current binary distribution listing were checked before implementation. Current stable CEF verified during this task is 152.0.6+g708dc14+chromium-152.0.7977.83 (September 7, 2026).

## References

- https://github.com/chromiumembedded/cef
- CEF General Usage / message-loop documentation
- CEF Windowless Rendering / CefRenderHandler documentation
- CEF CMakeLists.txt / FindCEF integration
