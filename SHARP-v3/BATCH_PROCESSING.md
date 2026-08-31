# Batch Processing for SHARP

This document describes the batch processing capabilities for the SHARP model.

## Overview

The batch processing feature allows you to process multiple images efficiently without reloading the model for each image. This significantly improves throughput, especially on GPU.

## Usage

### Basic Batch Processing

```bash
# Process all images in a directory with batch size of 4
sharp batch-predict -i ./data/original -o ./output/batch_results -c sharp_2572gikvuh.pt --batch-size 4 --device cpu
```

### With Rotation

```bash
# Process with 180-degree rotation around X axis
sharp batch-predict -i ./data/original -o ./output/rotated -c sharp_2572gikvuh.pt --batch-size 4 --rotate x 180
```

### CPU Processing (smaller batches)

```bash
# For CPU, use smaller batch sizes
sharp batch-predict -i ./data/original -o ./output/cpu_results --batch-size 1 --device cpu
```

## Batch Size Guidelines

| Device              | Recommended Batch Size | Notes                                   |
| ------------------- | ---------------------- | --------------------------------------- |
| GPU (8GB)           | 2-4                    | Start with 2, increase if memory allows |
| GPU (16GB+)         | 4-8                    | Can process more images simultaneously  |
| GPU (24GB+)         | 8-16                   | Maximum throughput                      |
| CPU                 | 1-2                    | Limited by system memory and speed      |
| MPS (Apple Silicon) | 2-4                    | Good balance for Mac GPUs               |

## Command Options

| Option                  | Description                  | Default       |
| ----------------------- | ---------------------------- | ------------- |
| `-i, --input-path`      | Input image or directory     | Required      |
| `-o, --output-path`     | Output directory             | Required      |
| `-c, --checkpoint-path` | Path to model checkpoint     | Auto-download |
| `--batch-size`          | Images per batch             | 1             |
| `--device`              | Device to use (cpu/mps/cuda) | auto-detect   |
| `--rotate`              | Apply rotation (axis angle)  | None          |
| `--render/--no-render`  | Generate video renderings    | False         |
| `-v, --verbose`         | Enable debug logging         | False         |

## Examples

### Single Image (for comparison)

```bash
sharp predict -i ./data/original/image001.png -o ./output/single --device cuda
```

### Batch of 8 images

```bash
sharp batch-predict -i ./data/original -o ./output/batch8 --batch-size 8 --device cuda
```

### Batch with Multiple Rotations

```bash
sharp batch-predict -i ./data/original -o ./output/rotated \
  --batch-size 4 \
  --rotate z 90 \
  --rotate x 90
```

## Performance Comparison

| Method                 | 100 Images (GPU) | 100 Images (CPU) |
| ---------------------- | ---------------- | ---------------- |
| Individual predict     | ~5-10 min        | ~30-60 min       |
| Batch predict (size=4) | ~2-3 min         | ~15-30 min       |
| Batch predict (size=8) | ~1.5-2 min       | N/A              |

_Times are approximate and depend on image resolution and hardware_

## Using the run.sh Script

The included `run.sh` script provides a convenient way to run batch processing:

```bash
# Make executable
chmod +x run.sh

# Edit BATCH_SIZE in the script as needed
./run.sh
```

## Programmatic Usage

You can also use the batch processing module directly in Python:

```python
from sharp.cli.batch_predict import ImageDataset, predict_batch, collate_fn
from sharp.models import create_predictor, PredictorParams
import torch
from torch.utils.data import DataLoader

# Setup
device = torch.device("cuda")
predictor = create_predictor(PredictorParams())
predictor.load_state_dict(torch.load("sharp_2572gikvuh.pt"))
predictor.to(device).eval()

# Create dataset and dataloader
image_paths = list(Path("./data/original").glob("*.png"))
dataset = ImageDataset(image_paths)
dataloader = DataLoader(dataset, batch_size=4, collate_fn=collate_fn)

# Process batches
for batch in dataloader:
    gaussians_list = predict_batch(predictor, batch, device)
    # Save results...
```

## Troubleshooting

### Out of Memory Errors

- Reduce `--batch-size`
- Use `--device cpu` if GPU memory is insufficient
- Process fewer images at a time

### Slow Processing

- Increase `--batch-size` for better GPU utilization
- Ensure you're using `--device cuda` on NVIDIA GPUs
- Check that no other processes are using the GPU

### No Images Found

- Check that the input directory contains valid image files (.jpg, .png, .heic, etc.)
- Verify the path is correct and accessible
