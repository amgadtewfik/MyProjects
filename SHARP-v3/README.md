# SHARP v3 — Monocular 3D Gaussian Splatting

Fork of Apple's [SHARP](https://apple.github.io/ml-sharp/) (arXiv 2512.10685) with
quality and file-size enhancements.

Given a single photograph, SHARP regresses a 3D Gaussian Splatting (3DGS) scene
in under a second on CPU or GPU. The resulting `.ply` renders at real-time
framerates in any compatible viewer (SuperSplat, Three.js, nerfstudio, etc.).

---

## What's new in v3

| Feature | What it does |
|---|---|
| **PLY compression (default)** | Voxel-based spatial deduplication — 60-90% file size reduction, no visible quality loss |
| **Rotation support** | `--rotate x/y/z <angle>`, multiple axes, applied before compression |
| **Quality presets** | `--quality standard/high/best` — trade off speed vs fidelity |
| **Compression tuning** | `--voxel-size`, `--voxel-factor`, `--opacity-threshold`, `--no-compress`, `--full-precision` |
| **Hole inpainting** | Detects and fills unfilled pixels with surface-aligned Gaussians |
| **Surface alignment** | Gaussians reoriented as flat disks — eliminates smoke/cloud distortion from novel views |

---

## Install

```bash
# Dependencies
python -m venv .venv && source .venv/bin/activate
pip install -r requirements.txt

# The model checkpoint is included (sharp_2572gikvuh.pt, 2.6 GB).
# Or download a fresh copy:
#   wget https://ml-site.cdn-apple.com/models/sharp/sharp_2572gikvuh.pt
```

Install the package:

```bash
pip install -e .
sharp --help
```

---

## ⚠️ Model Checkpoint (Required)

The model checkpoint (`sharp_2572gikvuh.pt`, ~2.6 GB) is **not included in this repository** due to size. Download it before running:

```bash
wget https://ml-site.cdn-apple.com/models/sharp/sharp_2572gikvuh.pt
```

Place it in the project root (`SHARP-v3/sharp_2572gikvuh.pt`).

---

## Usage

```bash
# Compressed output (default — ~0.5-2 MB vs 5-10 MB uncompressed)
sharp predict -i photo.jpg -o output/ -c sharp_2572gikvuh.pt

# With surface enhancement + hole inpainting
sharp predict -i photo.jpg -o output/ -c sharp_2572gikvuh.pt --enhance

# Best quality preset
sharp predict -i photo.jpg -o output/ -c sharp_2572gikvuh.pt --best-quality

# Rotate + compress
sharp predict -i photo.jpg -o output/ -c sharp_2572gikvuh.pt --rotate x 180

# Save both compressed and full-precision PLY
sharp predict -i photo.jpg -o output/ -c sharp_2572gikvuh.pt --full-precision

# Batch process a directory
sharp batch-predict -i input/dir -o output/dir -c sharp_2572gikvuh.pt

# Wrapper script — processes all images in ./input/
./run.sh
```

---

## How compression works

SHARP generates one Gaussian per input pixel. Continuous surfaces produce
near-identical neighbouring Gaussians — massive spatial redundancy.

1. **`prune_gaussians`** drops near-transparent (opacity < threshold) and
   degenerate Gaussians
2. **`voxel_dedup`** bins Gaussians into a 3D voxel grid, keeps only the
   highest-opacity splat per cell. Voxel size is auto-derived from the scene's
   own median Gaussian scale

Output PLY uses the same property layout as Apple's v2 — compatible with
SuperSplat, Three.js SplatLoader, nerfstudio, and other viewers.

```
Typical output sizes (2 MP image):
  v2 full precision:    ~5-10 MB
  v3 compressed:       ~0.5-2 MB   (60-90% smaller)
```

---

## Quality presets

| Preset | MLP steps | Hole canvas | Squash factor | Speed |
|---|---|---|---|---|
| `standard` (default) | 600 | 512x512 | 0.08 | ~5 s |
| `high` | 1000 | 1024x1024 | 0.05 | ~10 s |
| `best` / `--best-quality` | 1500 | 1536x1536 | 0.03 | ~15-30 s |

`squash_factor` controls how flat the surface-aligned disks are (lower = thinner,
more correct from novel views).

---

## Compression tuning

```bash
# Auto voxel size (default)
sharp predict -i photo.jpg -o output/ --compress

# Explicit voxel size in world units
sharp predict -i photo.jpg -o output/ --voxel-size 0.001

# More aggressive deduplication
sharp predict -i photo.jpg -o output/ --voxel-factor 2.0

# No compression
sharp predict -i photo.jpg -o output/ --no-compress
```

---

## Apple Silicon (MPS)

`gsplat-mps/` contains the gsplat rasterizer ported to Apple Metal via
OpenSplat. See `gsplat-mps/README.md` for installation.

---

## Output format

- **`.ply`** — 3D Gaussian Splatting scene (SuperSplat-compatible)
- **`.glb`** — GLB mesh export (with `--glb`)
- OpenCV coordinates (x right, y down, z forward), scene centre at (0, 0, +z)
- Quaternions are unit-normalised (required by SuperSplat, Three.js SplatLoader)

---

## Project structure

```
.
├── src/sharp/              # Python package (installed via pip install -e .)
│   ├── cli/                # CLI: predict, batch-predict, render
│   ├── models/             # Network architecture + enhancement models
│   └── utils/              # Gaussians, camera, postprocess, GLB export
│       └── postprocess.py  # v3 compression: prune + voxel_dedup + save_ply_compressed
├── gsplat-mps/             # Apple Silicon gsplat rasterizer
├── sharp_2572gikvuh.pt     # Model checkpoint (~2.6 GB)
├── run.sh                  # Process all images in ./input/ → ./output/
├── BATCH_PROCESSING.md     # batch-predict documentation
├── requirements.txt
├── pyproject.toml
└── README.md
```

## References

- Paper: [arXiv 2512.10685](https://arxiv.org/abs/2512.10685)
- Project page: https://apple.github.io/ml-sharp/
- Original repo: https://github.com/apple/ml-sharp
- gsplat: https://github.com/nerfstudio-project/gsplat
- gsplat-mps: https://github.com/iffyloop/gsplat-mps
