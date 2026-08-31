#!/bin/bash
set -e

# Single-image processing script for SHARP model v3
# This script processes all images in the input folder one by one.
source .venv/bin/activate

INPUT_FOLDER=./input
OUTPUT_FOLDER=./output
MODEL_PATH=./sharp_2572gikvuh.pt

show_usage() {
  cat <<EOF
Usage: $0 [sharp predict options]

Examples:
  $0 --best-quality
  $0 --quality high
  $0 --render

Any additional arguments are forwarded to the underlying sharp predict command.
EOF
  exit 0
}

if [[ $# -gt 0 ]]; then
  case "$1" in
    -h|--help)
      show_usage
      ;;
  esac
fi

echo "Running SHARP v3 ($(sharp --version 2>/dev/null || echo 'v3.0.0'))"
echo "Input Dir: $INPUT_FOLDER"
echo "Output Dir: $OUTPUT_FOLDER"
echo "Model: $MODEL_PATH"
echo "Forwarding additional args: $@"
echo ""

# Create output directory
mkdir -p "$OUTPUT_FOLDER"

PROCESSED=0
FAILED=0

for FILE in "$INPUT_FOLDER"/*; do
  if [ -f "$FILE" ]; then
    echo "--- Processing: $FILE ---"

    sharp predict \
      -i "$FILE" \
      -o "$OUTPUT_FOLDER" \
      -c "$MODEL_PATH" \
      --rotate x 180 \
      "$@"

    if [ $? -eq 0 ]; then
      PROCESSED=$((PROCESSED + 1))
      echo "--- Done: $FILE ---"
    else
      FAILED=$((FAILED + 1))
      echo "--- FAILED: $FILE ---"
    fi
  fi
done

echo ""
echo "=== COMPLETE ==="
echo "Processed: $PROCESSED  Failed: $FAILED"
