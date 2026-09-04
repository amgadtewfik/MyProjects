# MyProjects

A collection of personal projects I've built. Each lives in its own folder.

## Projects

### 🧠 ATF — Adaptive Tensor Format for Apple Silicon
[`amgadtewfik/atf`](https://github.com/amgadtewfik/atf) · C++ · Metal + custom kernels

Single-file `.atf` container with custom Metal kernels pushing decode
close to the memory-bandwidth ceiling on Apple Silicon. Ships a macOS
DMG via the [v0.7.0 release](https://github.com/amgadtewfik/atf/releases/tag/v0.7.0).

**v0.6.0** adds a full visual & UX overhaul of the chat app: **light, dark, and auto themes** with pre-paint persistence (no theme flash), **six accent colors** (indigo/violet/teal/green/amber/rose), a redesigned **sidebar-nav Settings panel**, **⇧⌘L** to toggle theme, **⌘1–5** to switch tabs, and a forward-looking **Qwen4 architecture** (QSA + n-gram) with structural paths validated on synthetic data. No engine changes.

### 🎬 CropCut — Video Crop & Resolution Enhancer
[`CropCut/`](./CropCut/) · Python · PyQt6 + OpenCV + FFmpeg

Desktop app for cropping regions of a video and upscaling the result.
Open any MP4/MOV/AVI/MKV/WebM file, **rotate 90°/180°/270°**, drag to select
a crop region, choose an upscale factor (1.5× / 2× / 3× / 4×), pick a codec
(H.264, H.265/HEVC, VP9, ProRes), and export. Includes timeline scrubbing,
CRF quality control, and keyboard shortcuts (Space, ←/→).

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

### 🧠 SHARP v3 — Monocular 3D Gaussian Splatting
[`SHARP-v3/`](./SHARP-v3/) · Python · PyTorch + gsplat + Apple Metal (MPS)

Fork of Apple's [SHARP](https://apple.github.io/ml-sharp/) (arXiv 2512.10685) with
quality and file-size enhancements. Given a single photograph, SHARP regresses
a 3D Gaussian Splatting (3DGS) scene in under a second on CPU or GPU.
The resulting `.ply` renders at real-time framerates in any compatible
viewer (SuperSplat, Three.js, nerfstudio, etc.).

**v3 enhancements:** PLY compression (60-90% size reduction, default), rotation
support (`--rotate x/y/z <angle>`), quality presets (`--quality standard/high/best`),
hole inpainting, and surface-aligned Gaussian disks. Includes batch processing
script (`run.sh`) and Apple Silicon MPS rasterizer via `gsplat-mps/`.

> **Note:** Model checkpoint (~2.6 GB) must be downloaded separately:
> `wget https://ml-site.cdn-apple.com/models/sharp/sharp_2572gikvuh.pt`

## Layout

```
.
├── CropCut/              # Python desktop video editor
├── m2ts_to_mp4/          # Batch m2ts → mp4 converter w/ JP→EN SRT
├── memWatch/             # macOS SwiftUI memory monitor
├── PLY Viewer/           # Windows OpenGL PLY viewer
├── SHARP-v3/             # Monocular 3D Gaussian Splatting (Apple SHARP fork)
└── README.md             # This file
```

> ATF lives in its own repository: <https://github.com/amgadtewfik/atf>
> (see the Projects section above).

## Notes

- Each project has its own README with build/run instructions.
- Build artifacts, virtualenvs, and node_modules are excluded via `.gitignore`.
