# AGENTS.md — WeaR Studio

## Project
WeaR Studio: aplikasi streaming & recording open-source, C++20 + Qt 6.10.1 + FFmpeg 6.0+,
CMake 3.21+, target Windows 10/11 x64 (MSVC 2022/2026). Terinspirasi arsitektur OBS Studio.
Tujuan akhir: mendekati OBS Studio dari sisi stabilitas, fitur inti, performa, dan UX.

## Arsitektur (JANGAN diubah tanpa alasan kuat & terdokumentasi)
Pipeline: CaptureManager (WinRT Windows Graphics Capture + D3D11, zero-copy)
  -> SceneManager (compositing, saat ini QPainter, render loop 60fps)
  -> EncoderManager (FFmpeg, NVENC primer + libx264 fallback)
  -> StreamManager (FFmpeg libavformat, mux FLV, RTMP)
Semua Manager: Thread-Safe Singleton pattern, akses via `Kelas::instance()`.
Plugin system: QPluginLoader, interface hierarchy IPlugin -> ISource / IFilter,
IID lewat Q_DECLARE_INTERFACE (lihat core/ISource.h, IFilter.h, IPlugin.h).

## Aturan wajib untuk setiap task
1. SELALU verifikasi dengan clean build sebelum klaim "selesai":
   `cmake --build build --config Debug` DAN `--config Release`, harus 0 error.
   Jangan percaya log build lama di repo — selalu build ulang.
2. JANGAN menghapus atau merusak fitur yang sudah jalan (capture, scene composite,
   encode, RTMP stream) demi menambah fitur baru. Jika breaking change tidak terhindarkan,
   jelaskan alasannya di commit message & docs/ARCHITECTURE.md.
3. Ikuti konvensi yang sudah ada: namespace `WeaR::`, pola Manager singleton,
   interface `IPlugin`/`ISource`/`IFilter`, `Q_OBJECT` + signal/slot untuk cross-thread.
4. Kelola lifecycle resource dengan benar: tidak boleh leak GPU texture (D3D11),
   tidak boleh leak `AVFrame`/`AVPacket`/`AVCodecContext` FFmpeg.
5. UI thread tidak boleh blocking — capture/encode/stream masing-masing di thread sendiri,
   komunikasi ke UI lewat Qt signal/slot (`Qt::QueuedConnection` bila lintas thread).
6. Setiap fitur baru: update `docs/ARCHITECTURE.md` (bagian relevan) dan tambahkan
   entri singkat di changelog/README roadmap.
7. Jangan commit file log build (`build_output.txt` dsb) — pastikan masuk `.gitignore`.
8. Kalau ragu antara "ikuti pola existing" vs "cara yang lebih modern/benar":
   utamakan konsistensi dengan pola existing kecuali ada alasan teknis kuat.    