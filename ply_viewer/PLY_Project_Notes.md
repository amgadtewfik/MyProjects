# PLY Viewer Project Notes

## Overview

This project is a Windows OpenGL 3.3 point cloud viewer implemented in C (`ply_viewer.c`) and built with MinGW-w64 on macOS. The viewer loads `.ply` files (both ASCII and binary) and renders them interactively.

## Build Environment

- Host OS: macOS
- Cross-compiler: `x86_64-w64-mingw32-gcc`
- Build script: `build.sh`
- Output: `PLY_Viewer.exe`

## Key Files

- `ply_viewer.c` — Main viewer implementation (~1000 lines)
- `build.sh` — Build wrapper for compiling resources and executable
- `create_icon.py` — Icon generator (generates app.ico)
- `docs/PLY_Viewer_Documentation.md` — Full documentation
- `docs/dense_ply_smoothing_plan.md` — Original design notes

## Recent Changes

### 2024-06-13 - Dense Cloud Smoothing

**Problem**: Dense PLY files (2M+ points) rendered with hard-edged 3-pixel discs looked grainy/speckled due to lack of blending between neighboring points with different colors.

**Solution** (3-layer fix):

1. **Soft Gaussian Splat Shader** — Replaced hard `if(dot > 0.25) discard` with `float w = exp(-r2 * 2.5); FragColor = vec4(vColor * w, w);`

2. **Additive Blending** — Enabled GL_BLEND with GL_SRC_ALPHA/GL_ONE while drawing points. Densely overlapping splats add up to smooth surface color.

3. **Adaptive Point Size** — New `ComputeAverageSpacing()` samples 64 random points, finds avg nearest-neighbor distance, sizes points to just cover their cell (clamped 1.5–16px). Dense clouds → smaller points → more overlap → smoother.

4. **Depth-Corrected Size** — Vertex shader applies `uDepthScale` so points further from camera appear smaller (matches solid behavior).

5. **Color Pre-Filter** — New `PrefilterColors()` builds 3D spatial hash grid, mixes each point's color 30% toward local mean. Kills 3DGS outlier speckle.

**Bug Fix**: Crashes were caused by buffer overflow in `PrefilterColors()` — allocated `totalCells` but accessed `totalCells*3`. Fixed by allocating `totalCells * 3`.

### Icon Update

`create_icon.py` now generates a cool 3D wireframe cube icon:
- Dark cyan/blue theme, transparent background
- Uses isometric projection with glowing edges
- Point cloud particles around the cube
- XYZ gizmo in corner
- Exports to app.ico (16/32/48/64/128/256 sizes)

## PLY Loading

The loader is robust and handles many PLY variants:
- Vertex properties: `x`, `y`, `z`, `red`, `green`, `blue`, `f_dc_0`, `f_dc_1`, `f_dc_2`
- Formats: binary_little_endian, binary_big_endian, ascii
- Property types: float, double, int, uint, uchar, char, short, ushort
- Max vertices: 10,000,000

Float colors (f_dc_0/1/2) are converted using sigmoid() to get 0-1 range, then scaled to 0-255.

## Controls

- Left drag: Rotate
- Right drag: Pan
- Scroll: Zoom
- R: Reset view
- O: Open file
- ESC: Quit
- +/-: Zoom in/out

## Verified Working PLY Files

- `/Volumes/SSD5/Ai/ml-sharp-ai/output/PXL_20240904_182612976.MP.ply` (2,259,793 points) — now renders smoothly with new smoothing
- `/Volumes/SSD5/Ai/ml-sharp-ai/output_test/Iris+sophia.ply` (1,175,000 points) — renders smoothly

---

*For full documentation, see `docs/PLY_Viewer_Documentation.md`*