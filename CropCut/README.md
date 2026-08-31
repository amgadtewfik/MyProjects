# 🎬 CropCut — Video Crop & Resolution Enhancer

A desktop video editor for cropping regions and upscaling resolution, built with PyQt6 + OpenCV + FFmpeg.

---

## Features

- **Open** any MP4, MOV, AVI, MKV, WebM video
- **Play / Pause / Seek** with timeline scrubber
- **Visual Crop Tool** — drag directly on the video to select a region
- **Video Rotation** — rotate 90°, 180°, or 270° before cropping/upscaling
- **Resolution Upscaling** — 1.5×, 2×, 3×, 4× with Lanczos / Bicubic / Bilinear
- **Multiple output codecs**: H.264, H.265/HEVC, VP9, ProRes
- **CRF quality control** (0 = lossless, 18 = excellent, 23 = default)
- **Keyboard shortcuts**: Space (play/pause), ← → (step frame)

---

## ⚙️ Prerequisites

### 1. Python 3.10 or newer
Download from https://www.python.org/downloads/

### 2. FFmpeg (REQUIRED for export)

**Windows:**
```
winget install ffmpeg
```
or download from https://ffmpeg.org/download.html and add to PATH

**macOS:**
```bash
brew install ffmpeg
```

**Ubuntu / Debian:**
```bash
sudo apt update && sudo apt install ffmpeg -y
```

---

## 🚀 Quick Start (Run from source)

### Step 1 — Clone / Download the project
```bash
# If using git:
git clone <repo-url>
cd video_crop_app

# Or just put all files in a folder and cd into it.
```

### Step 2 — Create a virtual environment (recommended)
```bash
python -m venv venv

# Activate:
# Windows:
venv\Scripts\activate
# macOS/Linux:
source venv/bin/activate
```

### Step 3 — Install Python dependencies
```bash
pip install -r requirements.txt
```

### Step 4 — Run the app
```bash
python main.py
```

---

## 📦 Build a Standalone Executable (no Python needed on target machine)

Uses **PyInstaller** to bundle everything into a single exe/app.

### Install PyInstaller
```bash
pip install pyinstaller
```

### Build (Windows — produces CropCut.exe)
```bash
pyinstaller cropcut.spec
```
Output: `dist/CropCut.exe`

### Build (macOS — produces CropCut.app)
```bash
pyinstaller cropcut.spec
```
Output: `dist/CropCut` (double-click to run)

### Build (Linux — produces CropCut binary)
```bash
pyinstaller cropcut.spec
```
Output: `dist/CropCut`

> **Note:** Build on the same OS as your target platform. You cannot cross-compile (e.g., build a Windows .exe on macOS).

---

## 📁 Project Structure

```
video_crop_app/
├── main.py           ← Full application source
├── requirements.txt  ← Python dependencies
├── cropcut.spec      ← PyInstaller build config
└── README.md         ← This file
```

---

## 🎮 How to Use

1. **Open video** — Click "📂 Open Video" or drag-and-drop an MP4 file
2. **Play** — Click Play or press **Space**; use ← → to step frame-by-frame
3. **Rotate** — Choose 0°, 90°, 180°, or 270° in the Rotation panel (crop is cleared on rotation change)
4. **Crop** — Click "✚ Enable Crop Tool", then drag on the video to draw a selection box
5. **Upscale** — Pick a scale factor (2×, 4×, etc.) and algorithm in the right sidebar
5. **Quality** — Adjust CRF (18 = excellent; lower = larger file; 0 = lossless)
6. **Export** — Click "💾 Export / Save Video"; choose output name and location

---

## 🔧 Troubleshooting

| Problem | Solution |
|---------|----------|
| `ModuleNotFoundError: cv2` | Run `pip install opencv-python` |
| `ModuleNotFoundError: PyQt6` | Run `pip install PyQt6` |
| Export fails: "FFmpeg not found" | Install FFmpeg and make sure it's in PATH (`ffmpeg -version` should work) |
| Black screen on video open | Update OpenCV: `pip install --upgrade opencv-python` |
| H.265 export fails | Your FFmpeg may lack libx265 — use H.264 instead, or rebuild FFmpeg with H.265 |
| App won't start on macOS | Allow unsigned apps: System Preferences → Security & Privacy → Allow |

---

## 📋 Full Dependency List

| Package | Version | Purpose |
|---------|---------|---------|
| PyQt6 | ≥ 6.5 | GUI framework, video display widget |
| opencv-python | ≥ 4.8 | Video decode, frame display |
| numpy | ≥ 1.24 | Frame buffer manipulation |
| FFmpeg (system) | any recent | Video encode/crop/upscale via subprocess |
| PyInstaller | ≥ 6.0 | (optional) Build standalone executable |

---

## 💡 Tips

- **Lossless crop with no re-encode**: In FFmpeg terms this is `-c copy` but requires keyframe-aligned cuts. CropCut uses full re-encode for accuracy.
- **Best upscale quality**: Use Lanczos with CRF 16–18 for archival output.
- **Fastest export**: Use Bilinear scaling and CRF 26–28 for drafts.
- **4K upscale**: 4× on a 1080p input → 4320p (8K). Ensure you have disk space and time.
