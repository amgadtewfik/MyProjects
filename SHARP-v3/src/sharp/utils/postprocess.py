"""Compression postprocessing for SHARP v3 PLY output.

This module is v3-only: it does NOT modify anything in v2. It reuses v2's
Gaussians3D / color-space helpers as read-only imports, since v3 is currently
layered on top of v2 internals.

REVISION 2 NOTES (see /Users/amgad/Desktop/Ai/Claude/sharp_v3_ply_compression_plan.md):
  - float16 quantization was tried and REVERTED: standard PLY has no half-float
    type, so plyfile round-trips f2 arrays back through float32 on write —
    zero actual size benefit, just wasted complexity. Removed.
  - opacity/scale threshold pruning ALONE barely reduces anything (~0.03% on
    Iris+sophia.ply) because the model is high quality — there's very little
    actual noise to threshold away.
  - The real redundancy is spatial: one Gaussian PER PIXEL means neighboring
    pixels on any continuous/flat surface project to near-identical 3D points.
    `voxel_dedup()` below collapses each small 3D cell down to its single
    highest-opacity splat, which is where the dramatic size reduction
    actually comes from.

The base `save_ply()` (v2.utils.gaussians.save_ply, inherited by v3) writes one
Gaussian PER PIXEL of the native image resolution, all 14 attributes as
float32, with zero pruning/dedup.

`save_ply_compressed()` now does, in order:
  1. opacity/scale threshold pruning (cheap, removes genuine noise/artifacts)
  2. voxel-grid dedup (the main size reduction — removes spatial redundancy)
and writes the same property layout as the original `save_ply` (still plain
float32 x/y/z/f_dc/opacity/scale/rot), so existing viewers keep working
unmodified.

For licensing of reused logic, see v2/utils/gaussians.py and the project's
LICENSE file. Copyright (C) 2025 Apple Inc. All Rights Reserved. (for the
reused conversion routines this module imports).
"""

from __future__ import annotations

import logging
from pathlib import Path

import numpy as np
import torch
from plyfile import PlyData, PlyElement

from sharp.utils import color_space as cs_utils
from sharp.utils.gaussians import Gaussians3D, convert_rgb_to_spherical_harmonics

LOGGER = logging.getLogger(__name__)

DEFAULT_OPACITY_THRESHOLD = 0.01
DEFAULT_SCALE_THRESHOLD = 1e-5
# Voxel cell size, expressed as a multiple of the median Gaussian scale.
# 1.0 means: collapse splats that are spatially closer together than their
# own typical footprint (i.e. genuinely redundant for rendering purposes).
DEFAULT_VOXEL_FACTOR = 1.0


def prune_gaussians(
    gaussians: Gaussians3D,
    opacity_threshold: float = DEFAULT_OPACITY_THRESHOLD,
    scale_threshold: float = DEFAULT_SCALE_THRESHOLD,
) -> Gaussians3D:
    """Drop near-transparent and degenerate-scale Gaussians.

    Args:
        gaussians: Batched Gaussians3D, shape [1, N, ...].
        opacity_threshold: Minimum opacity (post-sigmoid, in [0, 1]) to keep a splat.
        scale_threshold: Minimum value for the smallest singular value (scale)
            axis to keep a splat. Splats with a near-zero scale axis are
            numerical artifacts, not visible geometry.

    Returns:
        A new Gaussians3D with the surviving splats only (still batched [1, M, ...]).
    """
    opacities = gaussians.opacities.flatten(0, 1)  # [N]
    min_scale = gaussians.singular_values.flatten(0, 1).min(dim=-1).values  # [N]

    keep_mask = (opacities > opacity_threshold) & (min_scale > scale_threshold)
    num_total = keep_mask.numel()
    num_kept = int(keep_mask.sum().item())

    LOGGER.info(
        "Pruning: keeping %d / %d gaussians (%.1f%%) after opacity>%.4f, scale>%.6f filters.",
        num_kept,
        num_total,
        100.0 * num_kept / max(num_total, 1),
        opacity_threshold,
        scale_threshold,
    )

    def _select(tensor: torch.Tensor) -> torch.Tensor:
        flat = tensor.flatten(0, 1)
        return flat[keep_mask].unsqueeze(0)

    return Gaussians3D(
        mean_vectors=_select(gaussians.mean_vectors),
        singular_values=_select(gaussians.singular_values),
        quaternions=_select(gaussians.quaternions),
        colors=_select(gaussians.colors),
        opacities=_select(gaussians.opacities),
    )


def voxel_dedup(gaussians: Gaussians3D, voxel_size: float) -> Gaussians3D:
    """Collapse spatially-redundant Gaussians to one-per-voxel-cell.

    Bins all splats into a 3D grid of `voxel_size`-sized cells and keeps only
    the highest-opacity splat in each occupied cell. This is the main lever
    for reducing point count on dense per-pixel SHARP output, where most of
    the "extra" splats are duplicates of the same surface point seen from
    adjacent pixels rather than independent detail.

    Args:
        gaussians: Batched Gaussians3D, shape [1, N, ...].
        voxel_size: Edge length of each cubic voxel cell, in the same units
            as `gaussians.mean_vectors` (world units, post-unprojection).

    Returns:
        A new Gaussians3D with one splat per occupied voxel cell.
    """
    xyz = gaussians.mean_vectors.flatten(0, 1)  # [N, 3]
    opacities = gaussians.opacities.flatten(0, 1)  # [N]
    num_total = xyz.shape[0]
    original_device = xyz.device

    if voxel_size <= 0:
        LOGGER.info("voxel_size <= 0, skipping voxel dedup.")
        return gaussians

    # NOTE: scatter_reduce_ on int64 tensors is not supported on MPS
    # (confirmed via runtime error: "RuntimeError: not supported for
    # torch.int64" when this ran on an MPS device). The index bookkeeping
    # below (key packing, unique, scatter_reduce) is moved to CPU regardless
    # of the original device -- it's cheap relative to the model inference
    # that already ran -- and only the final selected indices are used to
    # gather from the original (possibly MPS) tensors.
    xyz_cpu = xyz.detach().to("cpu")
    opacities_cpu = opacities.detach().to("cpu")

    # Bin into integer grid coordinates.
    grid_coords = torch.floor(xyz_cpu / voxel_size).long()  # [N, 3]

    # Pack (gx, gy, gz) into a single int64 key. Offset by a large constant so
    # negative coordinates don't collide, then pack with 21 bits per axis
    # (+/- ~1,048,576 cells per axis, far more than any realistic scene needs
    # at typical voxel sizes).
    OFFSET = 1 << 20
    BITS = 21
    gx = (grid_coords[:, 0] + OFFSET).clamp(0, (1 << BITS) - 1)
    gy = (grid_coords[:, 1] + OFFSET).clamp(0, (1 << BITS) - 1)
    gz = (grid_coords[:, 2] + OFFSET).clamp(0, (1 << BITS) - 1)
    key = (gx << (2 * BITS)) | (gy << BITS) | gz

    unique_keys, inverse = torch.unique(key, return_inverse=True)
    num_groups = unique_keys.shape[0]

    # Find, per group (voxel cell), the index of the highest-opacity splat.
    best_opacity = torch.full(
        (num_groups,), float("-inf"), dtype=opacities_cpu.dtype, device="cpu"
    )
    best_opacity.scatter_reduce_(0, inverse, opacities_cpu, reduce="amax", include_self=True)

    is_best = opacities_cpu >= best_opacity[inverse]
    arange_idx = torch.arange(num_total, device="cpu")
    candidate_idx = torch.where(is_best, arange_idx, torch.full_like(arange_idx, num_total))

    selected_idx = torch.full((num_groups,), num_total, dtype=torch.long, device="cpu")
    selected_idx.scatter_reduce_(0, inverse, candidate_idx, reduce="amin", include_self=True)

    # Defensive: all groups should have a valid selection, but clamp just in case.
    selected_idx = selected_idx.clamp(max=num_total - 1)

    LOGGER.info(
        "Voxel dedup: %d / %d gaussians kept (%.1f%%) at voxel_size=%.6g (%d occupied cells).",
        num_groups,
        num_total,
        100.0 * num_groups / max(num_total, 1),
        voxel_size,
        num_groups,
    )

    # Move the final index list back to the original device for gathering.
    selected_idx = selected_idx.to(original_device)

    def _select(tensor: torch.Tensor) -> torch.Tensor:
        flat = tensor.flatten(0, 1)
        return flat[selected_idx].unsqueeze(0)

    return Gaussians3D(
        mean_vectors=_select(gaussians.mean_vectors),
        singular_values=_select(gaussians.singular_values),
        quaternions=_select(gaussians.quaternions),
        colors=_select(gaussians.colors),
        opacities=_select(gaussians.opacities),
    )


def compute_default_voxel_size(
    gaussians: Gaussians3D, voxel_factor: float = DEFAULT_VOXEL_FACTOR
) -> float:
    """Derive a sensible voxel size from the scene's own splat scale.

    Using an absolute world-unit voxel size isn't portable across scenes shot
    at different distances/focal lengths. Instead we scale relative to the
    median Gaussian footprint, so the cell size adapts to the scene
    automatically: `voxel_factor=1.0` collapses splats that are about as
    close together as their own size (i.e. visually redundant).
    """
    median_scale = gaussians.singular_values.flatten(0, 1).median(dim=-1).values.median()
    voxel_size = float(median_scale.item()) * voxel_factor
    LOGGER.info(
        "Derived default voxel_size=%.6g (median scale=%.6g, factor=%.2f).",
        voxel_size,
        float(median_scale.item()),
        voxel_factor,
    )
    return voxel_size


@torch.no_grad()
def save_ply_compressed(
    gaussians: Gaussians3D,
    f_px: float,
    image_shape: tuple[int, int],
    path: Path,
    opacity_threshold: float = DEFAULT_OPACITY_THRESHOLD,
    scale_threshold: float = DEFAULT_SCALE_THRESHOLD,
    voxel_size: float | None = None,
    voxel_factor: float = DEFAULT_VOXEL_FACTOR,
) -> PlyData:
    """Save a predicted Gaussians3D to a compressed ply file.

    Same property layout as v2.utils.gaussians.save_ply (so existing viewers
    keep working) — plain float32 x/y/z/f_dc/opacity/scale/rot — but applies,
    in order:
      1. opacity/scale threshold pruning (removes genuine noise/artifacts)
      2. voxel-grid dedup (removes spatial redundancy — the main size win)

    Args:
        gaussians: The predicted Gaussians (pre-pruning).
        f_px: Focal length in pixels.
        image_shape: (height, width) of the source image.
        path: Output .ply path.
        opacity_threshold: See `prune_gaussians`.
        scale_threshold: See `prune_gaussians`.
        voxel_size: Explicit voxel cell size in world units. If None, derived
            automatically from the scene via `compute_default_voxel_size`.
        voxel_factor: Used only when `voxel_size` is None; see
            `compute_default_voxel_size`.

    Returns:
        The written PlyData object.
    """

    def _inverse_sigmoid(tensor: torch.Tensor) -> torch.Tensor:
        return torch.log(tensor / (1.0 - tensor))

    pruned = prune_gaussians(gaussians, opacity_threshold, scale_threshold)

    if voxel_size is None:
        voxel_size = compute_default_voxel_size(pruned, voxel_factor)
    deduped = voxel_dedup(pruned, voxel_size)

    xyz = deduped.mean_vectors.flatten(0, 1).float()
    scale_logits = torch.log(deduped.singular_values).flatten(0, 1)
    quaternions = deduped.quaternions.flatten(0, 1)

    # Same linearRGB -> sRGB forced conversion as the uncompressed save_ply,
    # for compatibility with public renderers that don't do their own
    # linear2sRGB pass.
    colors = convert_rgb_to_spherical_harmonics(
        cs_utils.linearRGB2sRGB(deduped.colors.flatten(0, 1))
    )
    color_space_index = cs_utils.encode_color_space("sRGB")

    opacity_logits = _inverse_sigmoid(deduped.opacities).flatten(0, 1).unsqueeze(-1)

    num_gaussians = len(xyz)

    # Plain float32 throughout — PLY has no half-float type, so quantizing to
    # f2 here would just get silently widened back to f4 by plyfile with zero
    # size benefit. All the size reduction comes from pruning + voxel dedup.
    xyz_f4 = xyz.detach().cpu().numpy().astype(np.float32)
    rest = (
        torch.cat((colors, opacity_logits, scale_logits, quaternions), dim=1)
        .detach()
        .cpu()
        .numpy()
        .astype(np.float32)
    )

    dtype_full = [(attribute, "f4") for attribute in ["x", "y", "z"]] + [
        (attribute, "f4")
        for attribute in [f"f_dc_{i}" for i in range(3)]
        + ["opacity"]
        + [f"scale_{i}" for i in range(3)]
        + [f"rot_{i}" for i in range(4)]
    ]

    elements = np.empty(num_gaussians, dtype=dtype_full)
    for i, name in enumerate(["x", "y", "z"]):
        elements[name] = xyz_f4[:, i]
    rest_names = (
        [f"f_dc_{i}" for i in range(3)]
        + ["opacity"]
        + [f"scale_{i}" for i in range(3)]
        + [f"rot_{i}" for i in range(4)]
    )
    for i, name in enumerate(rest_names):
        elements[name] = rest[:, i]

    vertex_elements = PlyElement.describe(elements, "vertex")

    image_height, image_width = image_shape

    dtype_image_size = [("image_size", "u4")]
    image_size_array = np.empty(2, dtype=dtype_image_size)
    image_size_array[:] = np.array([image_width, image_height])
    image_size_element = PlyElement.describe(image_size_array, "image_size")

    dtype_intrinsic = [("intrinsic", "f4")]
    intrinsic_array = np.empty(9, dtype=dtype_intrinsic)
    intrinsic = np.array(
        [
            f_px,
            0,
            image_width * 0.5,
            0,
            f_px,
            image_height * 0.5,
            0,
            0,
            1,
        ]
    )
    intrinsic_array[:] = intrinsic.flatten()
    intrinsic_element = PlyElement.describe(intrinsic_array, "intrinsic")

    dtype_extrinsic = [("extrinsic", "f4")]
    extrinsic_array = np.empty(16, dtype=dtype_extrinsic)
    extrinsic_array[:] = np.eye(4).flatten()
    extrinsic_element = PlyElement.describe(extrinsic_array, "extrinsic")

    dtype_frames = [("frame", "i4")]
    frame_array = np.empty(2, dtype=dtype_frames)
    frame_array[:] = np.array([1, num_gaussians], dtype=np.int32)
    frame_element = PlyElement.describe(frame_array, "frame")

    dtype_disparity = [("disparity", "f4")]
    disparity_array = np.empty(2, dtype=dtype_disparity)
    disparity = 1.0 / deduped.mean_vectors[0, ..., -1]
    if disparity.numel() > 1_000_000:
        sample_idx = torch.randperm(disparity.numel(), device=disparity.device)[:1_000_000]
        disparity_sample = disparity.flatten()[sample_idx]
    else:
        disparity_sample = disparity.flatten()
    quantiles = (
        torch.quantile(disparity_sample, q=torch.tensor([0.1, 0.9], device=disparity.device))
        .float()
        .cpu()
        .numpy()
    )
    disparity_array[:] = quantiles
    disparity_element = PlyElement.describe(disparity_array, "disparity")

    dtype_color_space = [("color_space", "u1")]
    color_space_array = np.empty(1, dtype=dtype_color_space)
    color_space_array[:] = np.array([color_space_index]).flatten()
    color_space_element = PlyElement.describe(color_space_array, "color_space")

    dtype_version = [("version", "u1")]
    version_array = np.empty(3, dtype=dtype_version)
    version_array[:] = np.array([1, 5, 0], dtype=np.uint8).flatten()
    version_element = PlyElement.describe(version_array, "version")

    plydata = PlyData(
        [
            vertex_elements,
            extrinsic_element,
            intrinsic_element,
            image_size_element,
            frame_element,
            disparity_element,
            color_space_element,
            version_element,
        ]
    )

    plydata.write(path)
    return plydata
