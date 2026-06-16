# PLY Viewer - Full Documentation

## Overview

`PLY_Viewer.exe` is a Windows OpenGL 3.3 point cloud viewer written in C. It loads `.ply` files (both ASCII and binary), renders them interactively with mouse/keyboard controls, and supports drag-and-drop.

## Build

```bash
sh build.sh
```

Requires MinGW cross-compiler (`x86_64-w64-mingw32-gcc`).

Output: `PLY_Viewer.exe` (PE32+ GUI executable, ~220KB)

## Usage

1. Run `PLY_Viewer.exe`
2. Click "Open PLY" to load a file, or drag-and-drop a `.ply` file onto the window
3. Controls:
   - **Left mouse drag**: Rotate view
   - **Right mouse drag**: Pan view
   - **Scroll wheel**: Zoom in/out
   - **R key**: Reset view
   - **+/- keys**: Zoom in/out
   - **O key**: Open file dialog
   - **ESC**: Quit

## PLY Format Support

The loader is robust and handles many PLY variants:

- **Vertex properties**: `x`, `y`, `z` (required), plus:
  - Integer RGB: `red`, `green`, `blue` (uchar or int)
  - Float colors: `f_dc_0`, `f_dc_1`, `f_dc_2` (3DGS-style DC bands)
  - Auto-detects property names and types from header
- **Formats**: `binary_little_endian`, `binary_big_endian`, `ascii`
- **Property types**: `float`, `double`, `int`, `uint`, `uchar`, `char`, `short`, `ushort`
- **Max vertices**: 10,000,000 (capped at load time)

## Color Handling

### Integer RGB
Values 0-255 are used directly.

### Float Colors (f_dc_0/1/2)
- Values are passed through `sigmoid()` to convert from logits to 0-1 range
- Then scaled to 0-255 for display

## Smoothing Dense Point Clouds

### The Problem
Dense PLY files (2M+ points) rendered with the original hard-edged disc shader looked grainy/speckled. This is because:
- Each point was rendered as a hard 3×3 pixel disc
- No blending between neighboring points
- Different colors from nearby points created "salt and pepper" noise

### The Solution (3 layers)

#### 1. Soft Gaussian Splat Shader
The fragment shader was replaced with a smooth radial falloff:

```glsl
// Before (hard disc):
if(dot(coord, coord) > 0.25) discard;
FragColor = vec4(vColor, 1.0);

// After (soft splat):
float r2 = dot(coord, coord) * 4.0;
if(r2 > 1.0) discard;
float w = exp(-r2 * 2.5);
FragColor = vec4(vColor * w, w);
```

The fragment now outputs alpha-weighted colors that blend smoothly.

#### 2. Additive Blending
Added in `DrawScene()` when rendering points:

```c
glEnable(GL_BLEND);
glBlendFunc(GL_SRC_ALPHA, GL_ONE);  // additive
glDepthMask(GL_FALSE);              // don't write depth
// ... draw points ...
glDepthMask(GL_TRUE);
glDisable(GL_BLEND);
```

Additive blending causes densely overlapping splats to combine into a smooth surface color.

#### 3. Adaptive Point Size
New function `ComputeAverageSpacing()` samples 64 random points, finds nearest-neighbor distance, and computes the average spacing. This is used to size points:

```c
float baseSize = avgPointSpacing * 1000.0f;
baseSize = clamp(baseSize, 1.5f, 16.0f);  // cap range
glUniform1f(uPointSize, baseSize);
```

Dense clouds get smaller points (more overlap → smoother surface), sparse clouds get larger points (still visible).

#### 4. Depth-Corrected Size
The vertex shader applies a depth scale so points further from camera appear smaller:

```glsl
gl_PointSize = uPointSize * uDepthScale;
```

This matches real 3D solid behavior.

#### 5. Color Pre-Filter (Optional)
New function `PrefilterColors()` runs a cheap spatial smoothing:
- Builds a 3D hash grid at 2× average point spacing
- Accumulates per-cell RGB sums
- For each point, mixes 30% local-mean color with 70% original
- Caps at 2M cells to prevent memory issues

This kills remaining 3DGS outlier speckle without blurring real edges.

## Architecture

### Files

- `ply_viewer.c` — Main source (997 lines)
- `ply_viewer.rc` — Windows resource (links app.ico)
- `build.sh` — Build script
- `create_icon.py` — Icon generator (PIL)

### Key Data Structures

```c
typedef struct {
    float x, y, z;
    unsigned char r, g, b;
} Point;
```

### Global State

```c
static Point* points = NULL;     // vertex data
static int numPoints = 0;        // vertex count
static float rotX, rotY;         // rotation angles
static float zoom;               // zoom factor
static float panX, panY;         // pan offset
static float centerX, centerY, centerZ;  // bounding box center
static float scale;             // bounding box size (max dimension)
static float avgPointSpacing;    // average NN distance (for smoothing)
```

### Rendering Pipeline

1. **Init**: Compile shaders, create VAO/VBO
2. **Load**: Parse PLY header, read vertices into `points[]`, normalize, compute spacing, optionally pre-filter colors
3. **Upload**: Copy `points[]` to GPU VBO (interleaved x,y,z,r,g,b)
4. **Draw**:
   - Clear color/depth buffers
   - Build MVP matrix from camera (pan, rotation, zoom, aspect)
   - Draw axes (red X, green Y, blue Z) scaled to cloud size
   - Draw grid (optional, only when no file loaded)
   - Draw points with soft splat shader + additive blending
   - Swap buffers

### Shaders

| Shader | Purpose |
|--------|---------|
| `vertex_shader_points` | Projects positions, sets `gl_PointSize` with depth scale |
| `fragment_shader_points` | Soft Gaussian falloff, alpha-weighted output |
| `vertex_shader_lines` | Projects axes/grid positions |
| `fragment_shader_lines` | Passes through vertex colors |

### Key Functions

| Function | Purpose |
|----------|---------|
| `LoadPLY()` | Parse PLY header, detect properties, read vertices |
| `NormalizePointCloud()` | Compute bounding box, center, scale |
| `ComputeAverageSpacing()` | Sample 64 points, compute avg NN distance |
| `PrefilterColors()` | Spatial hash grid, local-mean color mixing |
| `DrawScene()` | Build matrices, enable blending, draw axes/grid/points |
| `InitShaders()` | Compile and link shader programs |
| `UploadPointVBO()` | Copy CPU point data to GPU VBO |

## Controls Detail

| Input | Action |
|-------|--------|
| Left drag | Rotate (orbit around center) |
| Right drag | Pan (translate in screen space) |
| Scroll | Zoom (scale camera distance) |
| R | Reset view (rotation, pan, zoom) |
| O | Open file dialog |
| ESC | Exit |
| +/- | Zoom in/out |

## Recent Changes

### 2024-06-13 (Dense Cloud Smoothing)

- Added soft Gaussian splat shader
- Added additive blending for point rendering
- Added adaptive point size based on density
- Added depth-corrected point size in vertex shader
- Added color pre-filter with spatial hash

### Icon Update

- `create_icon.py` now generates a cool 3D wireframe cube icon
- Dark cyan/blue theme, transparent background
- Uses isometric projection with glowing edges
- Point cloud particles around the cube
- XYZ gizmo in corner
- Exports to `app.ico` (16/32/48/64/128/256 sizes)

## Troubleshooting

### Crash on PLY load
- Fixed: Buffer overflow in `PrefilterColors()` — was allocating `totalCells` but accessing `totalCells*3` for RGB. Now allocates correctly.

### Points too small/large
- Adjust the `1000.0f` multiplier in `DrawScene()` line where `baseSize` is computed
- Or adjust the clamping: `if (baseSize < 1.5f) baseSize = 1.5f; if (baseSize > 16.0f) baseSize = 16.0f;`

### Grainy appearance persists
- The adaptive point size may need tuning for your specific data density
- Try increasing the `1000.0f` multiplier to make points larger and more overlapping
- Or enable color pre-filter by ensuring `PrefilterColors()` is called (it runs automatically after load)

## Future Improvements

- Add GUI sliders for point size, blending, color filter strength
- Add wireframe/toggle for surface vs. points
- Add background color picker
- Add export to screenshot
- Add mouse wheel zoom speed control
- Add keyboard shortcuts display

## License

Public domain / MIT (built with MinGW, no special dependencies beyond Windows and OpenGL)