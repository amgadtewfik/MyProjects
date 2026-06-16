# Smoothing Dense PLY Point Clouds

## Problem

- `/Volumes/SSD5/Ai/ml-sharp-ai/output/PXL_20240904_182612976.MP.ply` — **2,259,793 points** → looks grainy
- `/Volumes/SSD5/Ai/ml-sharp-ai/output_test/Iris+sophia.ply` — **1,175,000 points** → looks smooth
- Both are 3DGS-style PLYs with `f_dc_0/1/2` (DC SH band) float colors

## Root Cause (in `ply_viewer.c`)

Current fragment shader (`fragment_shader_points`, around line ~145):

```glsl
void main() {
  vec2 coord = gl_PointCoord - vec2(0.5);
  if(dot(coord, coord) > 0.25) discard;
  FragColor = vec4(vColor, 1.0);
}
```

Combined with `glUniform1f(uPointSize, 3.0f);` in `DrawScene()`:

- Every point is a hard-edged 3×3 disc.
- No blending → no color interpolation between neighbors.
- Dense clouds: thousands of small solid squares, each one a different color → speckle/salt-and-pepper noise.
- Sparse clouds: discs cover more empty space → no visible grain.

## Smoothing Strategy (3 layered fixes, in order of impact)

### 1. **Soft Gaussian splat in the fragment shader** (biggest visual win)

Replace the hard disc with a smooth radial falloff that blends into neighbors. Same draw call, same geometry, just shader change.

```glsl
float r2 = dot(coord, coord) * 4.0;
float w = exp(-r2 * 2.5);
float alpha = w * uAlpha;
FragColor = vec4(vColor * w, alpha);
```

Then enable additive blending: `glBlendFunc(GL_SRC_ALPHA, GL_ONE)`. Densely overlapping points add up to bright surface color and individual point boundaries disappear.

### 2. **Adaptive point size based on density**

- Compute avg nearest-neighbor distance once after load (subsample, build a small uniform grid, sample 64 random points, average nearest hit distance).
- Set `gl_PointSize` proportional to that distance.
- Cap at 16px, floor at 1.5px.

### 3. **Subtle pre-filter on color** (cheap insurance)

CPU pre-pass: for each point, look up ~5 nearest neighbors in a spatial hash, mix own color 70% with local mean 30%. Kills remaining speckle from 3DGS outliers.

## Implementation

Files changed: `ply_viewer.c` only. No new dependencies.

**Try in this order and stop at the first one that looks good enough** — each is independent:

1. Shader + blending (1+2 together, ~20 lines changed)
2. Color pre-filter (~60 lines added)

Build with `sh build.sh` after each step.
