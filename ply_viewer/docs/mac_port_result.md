# PLY Viewer for macOS

Native Cocoa + OpenGL viewer for `.ply` point clouds and 3D Gaussian Splat exports.

- Source: `/Users/amgad/Desktop/Ai/ply_viewer/src/mac/` (`ply_core.c`, `ply_core.h`, `main.m`, `Info.plist`, `build_mac.sh`, `create_icon_mac.py`)
- App: `/Users/amgad/Desktop/Ai/ply_viewer/dist/PLY Viewer.app`

## Build and run

```sh
cd /Users/amgad/Desktop/Ai/ply_viewer
./src/mac/build_mac.sh
open "dist/PLY Viewer.app"
open "dist/PLY Viewer.app" --args /path/to/file.ply
```

Headless render (used for testing, also handy for batch thumbnails):

```sh
"dist/PLY Viewer.app/Contents/MacOS/PLY Viewer" file.ply --mode splats --view iso --snapshot out.png
```

`--mode points|splats`, `--view front|back|left|right|top|bottom|iso`.

## What changed (2026-09-25 rewrite)

The previous port drew every point as a large translucent disc with alpha blending, depth writes and no sorting. Overlaps resolved differently as the view rotated, which produced the noise and flicker. Point size ignored perspective and a broken depth heuristic shrank points to sub-pixel size for clouds in millimetres. 3DGS colors were decoded with a sigmoid (washed out) and opacity was ignored.

### Renderer (`ply_core.c`)

- **Gaussian splat mode** (default for 3DGS files): each splat is an instanced quad; the 3D covariance from `scale_*` and `rot_*` is projected to screen space in the vertex shader, eigen-decomposed, and shaded with a true gaussian falloff times sigmoid(opacity). Splats are depth sorted back to front by a 16-bit counting sort on a background thread and composited with premultiplied alpha. Colors use `0.5 + 0.2820948 * f_dc`.
- **Points mode**: opaque round discs, perspective correct size derived from the measured median nearest-neighbour spacing (grid based), depth tested, so the image is identical from every angle. Eye-Dome Lighting (Potree style) is applied in a post pass for depth cues. Normals, when present, add head-light shading.
- Render target is an offscreen FBO (color + depth textures) followed by a composite pass that adds the background and EDL. Axis gizmo drawn in the corner, ground grid sized to the cloud with a "nice" step.
- Camera: orbit target + distance + yaw/pitch, perspective or orthographic, scale-aware pan, zoom towards cursor, double-click picks a pivot by reading the depth buffer, canonical views, fit from robust (2 to 98 percentile) bounds so outliers do not blow up the framing. Near/far planes follow the scene.
- Loader: reads the whole vertex block in 8 MB chunks and decodes with per-property offsets (was one `fread` per property). Correct sizes for `char/uchar/short/ushort/int/uint/float/double`, big endian, ASCII, normals, `alpha`, 8/16-bit and float colors, elements before `vertex` are skipped. Memory is allocated per file (up to 50 M points).

### UI (`main.m`)

- Unified toolbar: Open, Fit, Reset, Points/Splats, Snapshot, Inspector.
- Inspector (⌘I): mode, point size, splat scale, minimum opacity, color mode (file / height ramp / normals / gray), Eye-Dome Lighting and strength, normal shading, background, grid, gizmo, up axis, projection, field of view, view presets, file statistics, control cheat sheet.
- Status bar: file, point count, mode, sort time, draw time, fps.
- Menus with shortcuts: F fit, R reset, 1–7 views, M mode, E lighting, G grid, P projection, `[` `]` size, arrows orbit, ⌘S snapshot, ⌘W close, Open Recent.
- Background loading with a progress HUD, drag and drop, Finder "open with", settings persist in user defaults, redraws only when something changed (idle CPU about 0.3 %).

## Verified

| File | Result |
|---|---|
| `Iris+sophia.ply` (1,179,648 splats, SHARP export) | Loads in 0.1 s, sort 8 ms, draw 0.9 ms; photographic result in splat mode, crisp in points mode |
| ASCII with normals + uchar colors (210 k) | Normal shading and EDL correct |
| Binary big-endian, double coords ×1000, ushort colors, trailing face element | Identical framing and colors |

## Known limits

- Spherical harmonics beyond the DC band (`f_rest_*`) are ignored, so view dependent tint is not reproduced.
- Splat mode needs a texture buffer of 4 texels per splat; if the GPU limit is exceeded the app falls back to points and disables the Splats segment.
