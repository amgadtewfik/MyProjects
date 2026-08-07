# MyProjects

A collection of personal projects I've built. Each lives in its own folder.

## Projects

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

## Layout

```
.
├── CropCut/              # Python desktop video editor
├── memWatch/             # macOS SwiftUI memory monitor
├── PLY Viewer/           # Windows OpenGL PLY viewer
└── README.md             # This file
```

## Notes

- Each project has its own README with build/run instructions.
- Build artifacts, virtualenvs, and node_modules are excluded via `.gitignore`.