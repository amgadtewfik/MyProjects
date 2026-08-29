# MyProjects

A collection of personal projects I've built. Each lives in its own folder.

## Projects

### 🧠 ATF — Adaptive Tensor Format for Apple Silicon
[`amgadtewfik/atf`](https://github.com/amgadtewfik/atf) · C++ · Metal + custom kernels

Single-file `.atf` container with custom Metal kernels pushing decode
close to the memory-bandwidth ceiling on Apple Silicon. Ships a macOS
DMG via the [v0.3.0 release](https://github.com/amgadtewfik/atf/releases/tag/v0.3.0).

### 🎬 CropCut — Video Crop & Resolution Enhancer
[`CropCut/`](./CropCut/) · Python · PyQt6 + OpenCV + FFmpeg

Desktop app for cropping regions of a video and upscaling the result.
Open any MP4/MOV/AVI/MKV/WebM file, drag to select a crop region, choose
an upscale factor (1.5× / 2× / 3× / 4×), pick a codec (H.264, H.265/HEVC,
VP9, ProRes), and export. Includes timeline scrubbing, CRF quality control,
and keyboard shortcuts (Space, ←/→).

### 📊 memWatch — macOS Memory & Process Monitor
[`memWatch/`](./memWatch/) · Swift · SwiftUI + AppKit (macOS)

Native macOS menu-bar app that visualizes system memory pressure and
tracks per-process memory usage. Shows live memory stats, a sortable
process list, and configurable alerts. Built with `@Observable` state,
SwiftUI views, and AppKit integration.

### 🧊 PLY Viewer — Point Cloud Renderer
[`PLY Viewer/`](./PLY%20Viewer/) · C · OpenGL 3.3 + MinGW (Windows)

Windows OpenGL point-cloud viewer for `.ply` files (ASCII and binary).
Renders millions of points with a soft Gaussian-splat shader for
smoother dense-cloud appearance, interactive camera control, and
per-point color from PLY attributes. Cross-compiled from macOS using
MinGW-w64.

### 🎞️ m2ts_to_mp4 — Batch m2ts Converter + JP→EN Subtitles
[`m2ts_to_mp4/`](./m2ts_to_mp4/) · Python · FFmpeg + faster-whisper

Batch-converts MPEG-TS `.m2ts` files (e.g. Blu-ray recordings) to
`.mp4`. Wraps `ffmpeg` for the transcode and uses `faster-whisper` +
`deep-translator` to optionally generate Japanese→English SRT
subtitles. Includes a single-file CLI (`converter.py`) plus batch
drivers for both Windows (`convert_all.bat`) and macOS/Linux
(`convert_all.sh`). Supports codec choice (`copy` / `libx264` /
`libx265`), CRF control, and `--faststart` for web playback.

## Layout

```
.
├── CropCut/              # Python desktop video editor
├── m2ts_to_mp4/          # Batch m2ts → mp4 converter w/ JP→EN SRT
├── memWatch/             # macOS SwiftUI memory monitor
├── PLY Viewer/           # Windows OpenGL PLY viewer
└── README.md             # This file
```

> ATF lives in its own repository: <https://github.com/amgadtewfik/atf>
> (see the Projects section above).

## Notes

- Each project has its own README with build/run instructions.
- Build artifacts, virtualenvs, and node_modules are excluded via `.gitignore`.
