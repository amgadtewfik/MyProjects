"""Contains batch prediction CLI implementation for processing multiple images efficiently.

For licensing see accompanying LICENSE file.
Copyright (C) 2025 Apple Inc. All Rights Reserved.
"""

from __future__ import annotations

import logging
from pathlib import Path
from typing import Any

import click
import numpy as np
import torch
import torch.utils.data
import torch.nn.functional as F

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

from .render import render_gaussians

LOGGER = logging.getLogger(__name__)


class ImageDataset(torch.utils.data.Dataset):
    """Dataset for loading images for batch processing with preprocessing."""
    
    def __init__(self, image_paths: list[Path]):
        """Initialize dataset with list of image paths.
        
        Args:
            image_paths: List of paths to image files
        """
        self.image_paths = image_paths
        self.internal_shape = (1536, 1536)
    
    def __len__(self) -> int:
        """Return number of images in dataset."""
        return len(self.image_paths)
    
    def __getitem__(self, idx: int) -> dict[str, Any]:
        """Load, preprocess and return a single image.
        
        Args:
            idx: Index of image to load
            
        Returns:
            Dictionary with preprocessed image tensor and metadata
        """
        image_path = self.image_paths[idx]
        image, _, f_px = io.load_rgb(image_path)
        height, width = image.shape[:2]
        
        # Convert to tensor and preprocess (normalize to [0, 1] and permute to [C, H, W])
        # We use copy() to ensure the array is contiguous before converting to tensor
        image_pt = torch.from_numpy(image.copy()).float().permute(2, 0, 1) / 255.0
        
        # Resize to internal shape
        image_resized_pt = F.interpolate(
            image_pt[None],
            size=self.internal_shape,
            mode="bilinear",
            align_corners=True,
        ).squeeze(0)
        
        disparity_factor = torch.tensor([f_px / width]).float()
        
        return {
            'image': image_resized_pt,
            'disparity_factor': disparity_factor,
            'path': image_path,
            'height': height,
            'width': width,
            'f_px': f_px
        }


def collate_fn(batch: list[dict[str, Any]]) -> dict[str, Any]:
    """Collate function for batching preprocessed images.
    
    Args:
        batch: List of dictionaries from ImageDataset
        
    Returns:
        Dictionary with batched tensors and metadata
    """
    return {
        'images': torch.stack([item['image'] for item in batch]),
        'disparity_factors': torch.cat([item['disparity_factor'] for item in batch]),
        'paths': [item['path'] for item in batch],
        'heights': [item['height'] for item in batch],
        'widths': [item['width'] for item in batch],
        'f_pxs': [item['f_px'] for item in batch]
    }


@torch.no_grad()
def predict_batch(
    predictor: RGBGaussianPredictor,
    batch: dict[str, Any],
    device: torch.device,
) -> list[Gaussians3D]:
    """Predict Gaussians from a batch of images.
    
    Args:
        predictor: The Gaussian predictor model
        batch: Dictionary containing batched images and metadata
        device: Device to run inference on
        
    Returns:
        List of Gaussians3D objects, one per image
    """
    internal_shape = (1536, 1536)
    batch_size = len(batch['paths'])
    
    LOGGER.info(f"Processing batch of {batch_size} images")
    
    # Move batched tensors to device
    images_batch = batch['images'].to(device)
    disparity_batch = batch['disparity_factors'].to(device)
    
    LOGGER.info("Running batch inference")
    gaussians_ndc_batch = predictor(images_batch, disparity_batch)
    
    LOGGER.info("Running postprocessing for batch")
    
    # Process each image in the batch
    gaussians_list = []
    for i in range(batch_size):
        f_px = batch['f_pxs'][i]
        height = batch['heights'][i]
        width = batch['widths'][i]
        
        gaussians_ndc = gaussians_ndc_batch[i:i+1]  # Keep batch dimension
        
        # Build intrinsics
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
        intrinsics_resized = intrinsics.clone()
        intrinsics_resized[0] *= internal_shape[0] / width
        intrinsics_resized[1] *= internal_shape[1] / height
        
        # Convert to metric space
        gaussians = unproject_gaussians(
            gaussians_ndc, torch.eye(4).to(device), intrinsics_resized, internal_shape
        )
        
        gaussians_list.append(gaussians)
    
    return gaussians_list


@click.command()
@click.option(
    "-i",
    "--input-path",
    type=click.Path(path_type=Path, exists=True),
    help="Path to an image or directory containing images.",
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
    "--batch-size",
    type=int,
    default=1,
    help="Number of images to process in each batch. Higher values use more GPU memory.",
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
@click.option("-v", "--verbose", is_flag=True, help="Activate debug logs.")
def batch_predict_cli(
    input_path: Path,
    output_path: Path,
    checkpoint_path: Path,
    batch_size: int,
    with_rendering: bool,
    rotate: list[tuple[str, float]],
    device: str,
    verbose: bool,
):
    """Predict Gaussians from input images with batch processing for efficiency."""
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

    LOGGER.info("Found %d valid image files.", len(image_paths))

    if device == "default":
        if torch.cuda.is_available():
            device = "cuda"
        elif torch.mps.is_available():
            device = "mps"
        else:
            device = "cpu"
    LOGGER.info("Using device %s", device)

    if with_rendering and device not in ["cuda", "cpu"]:
        LOGGER.warning("Rendering is only supported on CUDA or MPS. Rendering is disabled.")
        with_rendering = False

    # Load or download checkpoint
    if checkpoint_path is None:
        LOGGER.info("No checkpoint provided. Downloading default model from %s", "https://ml-site.cdn-apple.com/models/sharp/sharp_2572gikvuh.pt")
        state_dict = torch.hub.load_state_dict_from_url("https://ml-site.cdn-apple.com/models/sharp/sharp_2572gikvuh.pt", progress=True)
    else:
        LOGGER.info("Loading checkpoint from %s", checkpoint_path)
        state_dict = torch.load(checkpoint_path, weights_only=True)

    gaussian_predictor = create_predictor(PredictorParams())
    gaussian_predictor.load_state_dict(state_dict)
    gaussian_predictor.eval()
    gaussian_predictor.to(device)

    output_path.mkdir(exist_ok=True, parents=True)

    # Create dataset and dataloader
    dataset = ImageDataset(image_paths)
    dataloader = torch.utils.data.DataLoader(
        dataset,
        batch_size=batch_size,
        shuffle=False,
        num_workers=4,
        collate_fn=collate_fn,
        pin_memory=device == "cuda",
    )

    LOGGER.info(f"Processing {len(image_paths)} images in batches of {batch_size}")

    total_files = len(image_paths)
    processed_count = 0
    
    for batch_idx, batch in enumerate(dataloader):
        batch_size_actual = len(batch['paths'])
        batch_start = processed_count + 1
        batch_end = processed_count + batch_size_actual
        LOGGER.info(f"--- Processing batch {batch_idx + 1}/{len(dataloader)}: files {batch_start}-{batch_end}/{total_files} ---")
        
        # Run batch inference
        gaussians_batch = predict_batch(gaussian_predictor, batch, torch.device(device))
        
        # Save results for each image in the batch
        for i, gaussians in enumerate(gaussians_batch):
            image_path = batch['paths'][i]
            f_px = batch['f_pxs'][i]
            height = batch['heights'][i]
            width = batch['widths'][i]
            
            file_num = processed_count + i + 1
            LOGGER.info(f"[{file_num}/{total_files}] Processing: {image_path.name}")
            
            if rotate:
                LOGGER.info(f"[{file_num}/{total_files}] Applying rotations: {rotate}")
                gaussians = apply_rotations(gaussians, rotate)
            
            output_file = output_path / f"{image_path.stem}.ply"
            LOGGER.info(f"[{file_num}/{total_files}] Saving 3DGS to: {output_file.name}")
            save_ply(gaussians, f_px, (height, width), output_file)
            LOGGER.info(f"[{file_num}/{total_files}] ✓ Saved: {output_file.name}")
            
            if with_rendering:
                output_video_path = (output_path / image_path.stem).with_suffix(".mp4")
                LOGGER.info(f"[{file_num}/{total_files}] Rendering trajectory to: {output_video_path.name}")

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
                metadata = SceneMetaData(intrinsics[0, 0].item(), (width, height), "linearRGB")
                render_gaussians(gaussians, metadata, output_video_path)
                LOGGER.info(f"[{file_num}/{total_files}] ✓ Rendered: {output_video_path.name}")
        
        processed_count += batch_size_actual

    LOGGER.info(f"Batch processing complete! Processed {processed_count}/{total_files} files.")


if __name__ == "__main__":
    batch_predict_cli()