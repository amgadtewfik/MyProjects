"""Contains `sharp predict` CLI implementation.

For licensing see accompanying LICENSE file.
Copyright (C) 2025 Apple Inc. All Rights Reserved.
"""

from __future__ import annotations

import logging
from pathlib import Path

import click
import numpy as np
import torch
import torch.nn.functional as F
import torch.utils.data

from sharp.models import (
    PredictorParams,
    RGBGaussianPredictor,
    create_predictor,
)
from sharp.utils import io
from sharp.utils import logging as logging_utils
from sharp.utils.gaussians import (
    Gaussians3D,
    SceneMetaData,
    apply_rotations,
    save_ply,
    unproject_gaussians,
)

from v3.utils.postprocess import (
    DEFAULT_OPACITY_THRESHOLD,
    DEFAULT_SCALE_THRESHOLD,
    DEFAULT_VOXEL_FACTOR,
    save_ply_compressed,
)

from .render import render_gaussians

LOGGER = logging.getLogger(__name__)

DEFAULT_MODEL_URL = "https://ml-site.cdn-apple.com/models/sharp/sharp_2572gikvuh.pt"


@click.command()
@click.option(
    "-i",
    "--input-path",
    type=click.Path(path_type=Path, exists=True),
    help="Path to an image or containing a list of images.",
    required=True,
)
@click.option(
    "-o",
    "--output-path",
    type=click.Path(path_type=Path, file_okay=False),
    help="Path to save the predicted Gaussians and renderings.",
    required=True,
)
@click.option(
    "-c",
    "--checkpoint-path",
    type=click.Path(path_type=Path, dir_okay=False),
    default=None,
    help="Path to the .pt checkpoint. If not provided, downloads the default model automatically.",
    required=False,
)
@click.option(
    "--render/--no-render",
    "with_rendering",
    is_flag=True,
    default=False,
    help="Whether to render trajectory for checkpoint.",
)
@click.option(
    "--device",
    type=str,
    default="default",
    help="Device to run on. ['cpu', 'mps', 'cuda']",
)
@click.option(
    "--rotate",
    type=(click.Choice(["x", "y", "z"]), float),
    multiple=True,
    help="Rotate the object around the given axis by the given angle in degrees. E.g. --rotate z 180",
)
@click.option(
    "--quality",
    type=click.Choice(["standard", "high", "best"], case_sensitive=False),
    default="standard",
    show_default=True,
    help="Quality preset for Gaussian enhancement and postprocessing.",
)
@click.option(
    "--best-quality",
    is_flag=True,
    help="Shortcut for --quality best.",
)
@click.option(
    "--compress/--no-compress",
    "with_compression",
    is_flag=True,
    default=True,
    show_default=True,
    help="Save a pruned + voxel-deduped '<stem>.ply'. This is the only output by default.",
)
@click.option(
    "--full-precision",
    "save_uncompressed",
    is_flag=True,
    default=False,
    show_default=True,
    help=(
        "Also save the full-precision, one-gaussian-per-pixel '<stem>_full_precision.ply' "
        "alongside the compressed output. Off by default since it's typically 5-10x larger."
    ),
)
@click.option(
    "--opacity-threshold",
    type=float,
    default=DEFAULT_OPACITY_THRESHOLD,
    show_default=True,
    help="Minimum opacity to keep a gaussian when --compress is enabled.",
)
@click.option(
    "--scale-threshold",
    type=float,
    default=DEFAULT_SCALE_THRESHOLD,
    show_default=True,
    help="Minimum scale (smallest axis) to keep a gaussian when --compress is enabled.",
)
@click.option(
    "--voxel-size",
    type=float,
    default=None,
    help=(
        "Explicit voxel cell size (world units) for spatial dedup when --compress is "
        "enabled. Splats closer together than this are collapsed to the single "
        "highest-opacity splat per cell. If omitted, derived automatically from the "
        "scene's own median gaussian scale (see --voxel-factor)."
    ),
)
@click.option(
    "--voxel-factor",
    type=float,
    default=DEFAULT_VOXEL_FACTOR,
    show_default=True,
    help=(
        "Multiplier on the scene's median gaussian scale used to auto-derive "
        "--voxel-size when it isn't explicitly set. Higher = more aggressive dedup "
        "(smaller file, more risk of visible quality loss)."
    ),
)
@click.option("-v", "--verbose", is_flag=True, help="Activate debug logs.")
def predict_cli(
    input_path: Path,
    output_path: Path,
    checkpoint_path: Path,
    with_rendering: bool,
    device: str,
    rotate: list[tuple[str, float]],
    quality: str,
    best_quality: bool,
    with_compression: bool,
    save_uncompressed: bool,
    opacity_threshold: float,
    scale_threshold: float,
    voxel_size: float | None,
    voxel_factor: float,
    verbose: bool,
):
    """Predict Gaussians from input images."""
    logging_utils.configure(logging.DEBUG if verbose else logging.INFO)

    extensions = io.get_supported_image_extensions()

    image_paths = []
    if input_path.is_file():
        if input_path.suffix in extensions:
            image_paths = [input_path]
    else:
        for ext in extensions:
            image_paths.extend(list(input_path.glob(f"**/*{ext}")))

    if len(image_paths) == 0:
        LOGGER.info("No valid images found. Input was %s.", input_path)
        return

    LOGGER.info("Processing %d valid image files.", len(image_paths))

    if device == "default":
        if torch.cuda.is_available():
            device = "cuda"
        elif torch.mps.is_available():
            device = "mps"
        else:
            device = "cpu"
    LOGGER.info("Using device %s", device)

    if with_rendering and device != "cuda":
        LOGGER.warning("Can only run rendering with gsplat on CUDA. Rendering is disabled.")
        with_rendering = False

    if best_quality:
        quality = "best"

    quality = quality.lower()
    if quality not in {"standard", "high", "best"}:
        raise click.BadParameter(
            "Quality must be one of: standard, high, best."
        )

    enhanced = quality in {"high", "best"}
    LOGGER.info("Using quality preset: %s", quality)
    if enhanced:
        LOGGER.info("Auto-enable enhanced postprocessing for quality=%s", quality)

    # Load or download checkpoint
    if checkpoint_path is None:
        LOGGER.info("No checkpoint provided. Downloading default model from %s", DEFAULT_MODEL_URL)
        state_dict = torch.hub.load_state_dict_from_url(DEFAULT_MODEL_URL, progress=True)
    else:
        LOGGER.info("Loading checkpoint from %s", checkpoint_path)
        state_dict = torch.load(checkpoint_path, weights_only=True)

    gaussian_predictor = create_predictor(PredictorParams())
    gaussian_predictor.load_state_dict(state_dict)
    gaussian_predictor.eval()
    gaussian_predictor.to(device)

    output_path.mkdir(exist_ok=True, parents=True)

    for image_path in image_paths:
        LOGGER.info("Processing %s", image_path)
        image, _, f_px = io.load_rgb(image_path)
        height, width = image.shape[:2]
        intrinsics = torch.tensor(
            [
                [f_px, 0, (width - 1) / 2.0, 0],
                [0, f_px, (height - 1) / 2.0, 0],
                [0, 0, 1, 0],
                [0, 0, 0, 1],
            ],
            device=device,
            dtype=torch.float32,
        )
        gaussians = predict_image(
            gaussian_predictor,
            image,
            f_px,
            torch.device(device),
            quality=quality,
            enhanced=enhanced,
        )

        if rotate:
            LOGGER.info("Applying rotations: %s", rotate)
            gaussians = apply_rotations(gaussians, rotate)

        full_path = output_path / f"{image_path.stem}_full_precision.ply"
        if save_uncompressed or not with_compression:
            LOGGER.info("Saving full-precision 3DGS to %s", output_path)
            save_ply(gaussians, f_px, (height, width), full_path)

        if with_compression:
            LOGGER.info("Saving compressed 3DGS to %s", output_path)
            compressed_path = output_path / f"{image_path.stem}.ply"
            save_ply_compressed(
                gaussians,
                f_px,
                (height, width),
                compressed_path,
                opacity_threshold=opacity_threshold,
                scale_threshold=scale_threshold,
                voxel_size=voxel_size,
                voxel_factor=voxel_factor,
            )
            comp_size = compressed_path.stat().st_size
            if full_path.exists():
                full_size = full_path.stat().st_size
                LOGGER.info(
                    "File sizes: full=%.2f MB, compressed=%.2f MB (%.1f%% reduction)",
                    full_size / 1e6,
                    comp_size / 1e6,
                    100.0 * (1.0 - comp_size / max(full_size, 1)),
                )
            else:
                LOGGER.info("Compressed file size: %.2f MB", comp_size / 1e6)

        if with_rendering:
            output_video_path = (output_path / image_path.stem).with_suffix(".mp4")
            LOGGER.info("Rendering trajectory to %s", output_video_path)

            metadata = SceneMetaData(intrinsics[0, 0].item(), (width, height), "linearRGB")
            render_gaussians(gaussians, metadata, output_video_path)


def _upsample_spatial(tensor: torch.Tensor, target_h: int, target_w: int) -> torch.Tensor:
    """Upsample a [B, C, N, H, W] tensor to [B, C, N, target_h, target_w].

    Merges C and N dims for interpolation, then restores shape.
    """
    B, C, N, H, W = tensor.shape
    # Merge B, C, N into batch dim for F.interpolate: [B*C*N, 1, H, W]
    merged = tensor.reshape(B * C * N, 1, H, W)
    upsampled = F.interpolate(merged, size=(target_h, target_w), mode="bilinear", align_corners=True)
    return upsampled.reshape(B, C, N, target_h, target_w)


@torch.no_grad()
def predict_image(
    predictor: RGBGaussianPredictor,
    image: np.ndarray,
    f_px: float,
    device: torch.device,
    quality: str = "standard",
    enhanced: bool = False,
) -> Gaussians3D:
    """Predict Gaussians from an image with 1:1 pixel-to-gaussian correspondence.

    The model runs inference at a fixed internal resolution (1536x1536), producing
    gaussian attribute maps at output_resolution (768x768). These maps are then
    bilinearly upsampled to the original image's native resolution so that the
    resulting PLY has exactly width * height gaussians — one per pixel.
    """
    internal_shape = (1536, 1536)

    LOGGER.info("Running preprocessing. quality=%s enhanced=%s", quality, enhanced)
    image_pt = torch.from_numpy(image.copy()).float().to(device).permute(2, 0, 1) / 255.0
    _, height, width = image_pt.shape
    disparity_factor = torch.tensor([f_px / width]).float().to(device)

    image_resized_pt = F.interpolate(
        image_pt[None],
        size=(internal_shape[1], internal_shape[0]),
        mode="bilinear",
        align_corners=True,
    )

    # Predict Gaussians in NDC space with spatial structure preserved.
    LOGGER.info("Running inference.")
    gaussians_ndc = predictor(image_resized_pt, disparity_factor, flatten_output=False)

    # gaussians_ndc fields are now [B, C, N, H_model, W_model] (unflatted spatial).
    # Upsample each attribute map to the native image resolution.
    LOGGER.info(
        "Upsampling gaussians from model resolution to native %dx%d (%d points).",
        width,
        height,
        width * height,
    )

    native_h, native_w = height, width

    # mean_vectors: [B, 3, N, H, W] -> upsample to [B, 3, N, native_h, native_w]
    mean_vectors = _upsample_spatial(gaussians_ndc.mean_vectors, native_h, native_w)

    # singular_values: [B, 3, N, H, W]
    singular_values = _upsample_spatial(gaussians_ndc.singular_values, native_h, native_w)

    # quaternions: [B, 4, N, H, W] -> upsample then renormalize
    quaternions = _upsample_spatial(gaussians_ndc.quaternions, native_h, native_w)
    # Renormalize quaternions to unit length after bilinear interpolation
    quat_norm = quaternions.norm(dim=1, keepdim=True).clamp(min=1e-8)
    quaternions = quaternions / quat_norm

    # colors: [B, 3, N, H, W]
    colors = _upsample_spatial(gaussians_ndc.colors, native_h, native_w)

    # opacities: [B, N, H, W] -> need to add channel dim, upsample, remove it
    opacities_5d = gaussians_ndc.opacities.unsqueeze(1)  # [B, 1, N, H, W]
    opacities_5d = _upsample_spatial(opacities_5d, native_h, native_w)
    opacities = opacities_5d.squeeze(1)  # [B, N, H, W]

    # Flatten spatial dims: [B, C, N, H, W] -> [B, N*H*W, C]
    mean_vectors = mean_vectors.permute(0, 2, 3, 4, 1).flatten(1, 3)
    singular_values = singular_values.permute(0, 2, 3, 4, 1).flatten(1, 3)
    quaternions = quaternions.permute(0, 2, 3, 4, 1).flatten(1, 3)
    colors = colors.permute(0, 2, 3, 4, 1).flatten(1, 3)
    opacities = opacities.flatten(1, 3)

    gaussians_ndc_upsampled = Gaussians3D(
        mean_vectors=mean_vectors,
        singular_values=singular_values,
        quaternions=quaternions,
        colors=colors,
        opacities=opacities,
    )

    # Unproject using native image intrinsics and dimensions.
    LOGGER.info("Running postprocessing (unprojection at native resolution).")
    intrinsics = (
        torch.tensor(
            [
                [f_px, 0, width / 2, 0],
                [0, f_px, height / 2, 0],
                [0, 0, 1, 0],
                [0, 0, 0, 1],
            ]
        )
        .float()
        .to(device)
    )

    # Unproject at native resolution — gaussians are already in native pixel space.
    gaussians = unproject_gaussians(
        gaussians_ndc_upsampled, torch.eye(4).to(device), intrinsics, (width, height)
    )

    return gaussians
