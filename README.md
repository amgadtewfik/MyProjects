# MyProjects

A collection of personal projects I've built. Each lives in its own folder.

## Projects

### <img src="./assets/atf-icon.png" alt="ATF icon" width="16" height="16"> ATF — Adaptive Tensor Format for Apple Silicon

[`amgadtewfik/atf`](https://github.com/amgadtewfik/atf) · Python · Metal + custom kernels

Introducing ATF (Adaptive Tensor Format) — a custom model format that converts quantized GGUF and MLX checkpoints into a single-file .atf container with hand-written Metal kernels, memory-mapped loads, GPU-resident weights, and an adaptive reasoning router, pushing inference close to the hardware's real memory-bandwidth ceiling in load times measured in seconds.

#### What's genuinely good about it

##### 1. Broad GGUF quantization support on Apple Metal

The README's headline claim is real: ATF has kernels for Q2_K, Q3_K, Q4_K, Q5_K, Q6_K, IQ1_S, IQ2_XXS/XS/S, IQ3_XXS/S, IQ4_XS, and IQ4_NL — fifteen formats in total — implemented through custom MSL in `gguf_fast_*.py` and `gguf_metal.py`. IQ4_NL reaches 74 GB/s, while Q6_K reaches 62 GB/s, which the docs honestly peg against the M4's approximately 84 GB/s ceiling.

The reason this matters: Ollama on macOS does not accelerate these formats on Metal — it dequantizes them on CPU or uses llama.cpp's GGML Metal path, which only covers a subset. LM Studio uses the same llama.cpp underneath, so it inherits the same gaps. oMLX is built on `mlx_lm`, which natively supports MLX-quantized safetensors but does not ingest raw GGUF at all. So if you want to run a UD-Q2_K_XL 27B on an M4 mini, ATF is one of the few tools that will actually do it — `Qwen3.8-27B-UD-Q2_K_XL.atf` runs at 4.80 tok/s here.

##### 2. OpenAI-compatible server with real, end-to-end working features

`atf/server_openai.py` is an 861-line OpenAI-compatible HTTP server (SSE streaming, `/v1/models`, `/v1/chat/completions`, and tool calling). It is not just a wrapper. It does things the wrappers do not:

 - A syscache that skips the 4,838-token system-prompt prefill on repeat requests. It is disk-persisted and hash-keyed, with a single-turn greeting detector that drops the system prompt entirely for "hi".
 - An adaptive tier router (`atf/router.py`) that scores each prompt for difficulty and picks one of four generation tiers: instant, chat, reasoning, or deep. It sets the thinking budget, temperature, and `max_tokens`.
 - A 65,536-token context with a chunked, growing preallocated KV cache (`KVCache` in `engine.py`) that fixed a previous out-of-memory issue on 16 GB systems. Prompt-prefix caching (`_pcache`) means the chat client only re-prefills the new suffix when it resends the conversation.
 - Tool calling (`atf/toolcall.py`) with seven dedicated stream-parser tests.

##### 3. Thoughtful file format

The `.atf` header is 128 bytes with the `ATF1` magic value. Startup reads only the header to populate the dropdown — no resident weights until you pick a model. The file is memory-mappable, 16-byte aligned, and self-contained. Dense weights (embed, attention, norms, and `lm_head`) are always resident; LOD-0 experts are always resident; LOD1+ stays on disk. The reader is offset-driven, so section order does not matter.

The LOD pyramid (INT8/INT4/INT2/low-rank/index-only) is a coherent idea, even if it is only partially exploited at runtime today.

##### 4. A real, well-built Electron app

This is not a "TODO UI" wrapping a CLI. The Electron renderer is approximately 2,400 lines of `app.js` plus theme, accent, density, font-size, and animation controls; a six-palette accent system; a settings modal with five sections; a multi-chat sidebar with Markdown/JSON export; full Markdown rendering with markdown-it, highlight.js, and DOMPurify; copy, edit-and-resend, and regenerate actions; a crash banner with exponential-backoff auto-restart; SVG preview in a BrowserWindow; persistent window state; a sandboxed renderer; and a strict CSP. The v0.6.0 release enabled greedy decoding by default and added an elapsed-time clock that starts on submit. It is an honest-to-goodness native macOS app.

Ships a macOS DMG via the [v0.9.0 release](https://github.com/amgadtewfik/atf/releases/tag/v0.9.0).

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

### 🎞️ M2TS format to MP4  — M2TS→MP4 Converter + JP→EN Subtitles Auto Translation
[`m2ts_to_mp4/`](./m2ts_to_mp4/) · Python · FFmpeg + faster-whisper

Batch-converts MPEG-TS `.m2ts` files (e.g. Blu-ray recordings) to
`.mp4`. Wraps `ffmpeg` for the transcode and uses `faster-whisper` +
`deep-translator` to optionally generate Japanese→English SRT
subtitles. Includes a single-file CLI (`converter.py`) plus batch
drivers for both Windows (`convert_all.bat`) and macOS/Linux
(`convert_all.sh`). Supports codec choice (`copy` / `libx264` /
`libx265`), CRF control, and `--faststart` for web playback.

### 🧠 3D Gaussian Splatting Sharp (3DGS) — Monocular 3D Gaussian Splatting
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
