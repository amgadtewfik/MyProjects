# PLY Viewer

A Windows-native OpenGL 3.3 point-cloud viewer for `.ply` files (ASCII
and binary), plus a small Python preview tool.

The C viewer renders millions of points with a soft Gaussian-splat
shader for smooth dense-cloud appearance and an adaptive point-size
strategy that scales to point density.

## Features (C viewer)

- **Soft Gaussian-splat rendering** — `exp(-r² × 2.5)` falloff with
  additive blending. Overlapping splats accumulate color for smooth
  surfaces even on dense clouds (2M+ points).
- **Adaptive point sizing** — samples 64 random points, finds average
  nearest-neighbor distance, sizes points to cover their cell
  (clamped 1.5–16 px). Dense clouds → smaller points → smoother.
- **Depth-corrected size** — vertex shader applies a depth scale so far
  points render smaller.
- **Color pre-filter** — spatial hash grid mixes each point's color 30 %
  toward its local mean, killing 3DGS outlier speckle.
- **Robust PLY loader** — handles `binary_little_endian`,
  `binary_big_endian`, and `ascii` formats; properties `x/y/z`,
  `red/green/blue`, `f_dc_0/1/2`; types float/double/int/uint/uchar/
  char/short/ushort. Up to 10 M vertices.
- **3DGS color decoding** — float colors `f_dc_0/1/2` are converted with
  sigmoid to [0, 1], then scaled to 0–255.
- **Interactive camera** — left-drag rotate, right-drag pan, scroll zoom,
  `R` reset, `O` open, `+`/`-` zoom, `Esc` quit.

## Requirements

### C viewer (Windows `.exe`)

- **MinGW-w64** cross-compiler (`x86_64-w64-mingw32-gcc` /
  `x86_64-w64-mingw32-windres`) — install with Homebrew on macOS:
  ```sh
  brew install mingw-w64
  ```
  or use the MSYS2 packages on Windows.
- **Windows target** — links `opengl32` and `gdi32`.

### Python preview tool

- Python 3.10+
- `open3d`, `numpy`, `tkinter` (ships with Python)

## Build

### Windows (native, with MSYS2 / MinGW)

```bat
build.bat
```

### macOS / Linux (cross-compile to Windows)

```sh
./build.sh
```

Both scripts compile `ply_viewer.c` and `ply_viewer.rc` into
`PLY_Viewer.exe`.

## Run

```bat
PLY_Viewer.exe
```

Or on macOS, after cross-compiling, run via Wine:

```sh
wine PLY_Viewer.exe
```

`O` opens a file dialog — pick any `.ply`. The window title shows the
loaded filename and point count.

## Project Layout

```
PLY Viewer/
├── ply_viewer.c                 # Main C viewer (~1000 lines, OpenGL 3.3)
├── ply_viewer_modified.c        # Modified version (debug/in-progress)
├── ply_viewer.py                # Python preview tool (Open3D + tkinter)
├── ply_viewer.rc                # Windows resource file (icon)
├── PLY_Viewer.spec              # PyInstaller spec (alternative build path)
├── PLY_Viewer.exe               # Pre-built Windows binary
├── app.ico                      # App icon (multi-size)
├── build.sh                     # Cross-compile from macOS/Linux
├── build.bat                    # Build on Windows
├── build_windows.py             # Python build helper
├── create_icon.py               # Regenerates app.ico
├── PLY_Project_Notes.md         # Design notes and history
└── docs/
    ├── PLY_Viewer_Documentation.md
    └── dense_ply_smoothing_plan.md
```

## Verified Working Files

The dense-cloud smoothing was tested on:

- `PXL_20240904_182612976.MP.ply` — 2,259,793 points (renders smoothly)
- `Iris+sophia.ply` — 1,175,000 points (renders smoothly)

See `PLY_Project_Notes.md` for the full history of changes.

## License

Personal project — no license specified.