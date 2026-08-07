#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
INPUT_DIR="$SCRIPT_DIR/input"
OUTPUT_DIR="$SCRIPT_DIR/output"
GENERATE_SRT=false

for arg in "$@"; do
    if [ "$arg" = "--srt" ]; then
        GENERATE_SRT=true
    fi
done

mkdir -p "$OUTPUT_DIR"

echo "===== STEP 1: Converting m2ts to mp4 ====="
cd "$INPUT_DIR"

total=$(ls -1 *.m2ts 2>/dev/null | wc -l | tr -d ' ')
echo "Found $total m2ts files"

count=0
for f in *.m2ts; do
    if [ -f "$f" ]; then
        count=$((count + 1))
        name="${f%.m2ts}"
        echo "[$count/$total] Converting: $f"
        python3 "$SCRIPT_DIR/converter.py" "$INPUT_DIR/$f" -o "$OUTPUT_DIR/${name}.mp4" -v copy -a copy
    fi
done

echo ""
echo "Step 1 complete: $count files converted"
echo ""

if [ "$GENERATE_SRT" = true ]; then
    echo "===== STEP 2: Generating SRT subtitles ====="
    cd "$OUTPUT_DIR"
    
    srt_total=$(ls -1 *.mp4 2>/dev/null | wc -l | tr -d ' ')
    echo "Found $srt_total mp4 files"
    
    srt_count=0
    for f in *.mp4; do
        if [ -f "$f" ]; then
            srt_count=$((srt_count + 1))
            echo "[$srt_count/$srt_total] Generating SRT: $f"
            python3 "$SCRIPT_DIR/converter.py" "$f" --srt
        fi
    done
    
    echo ""
    echo "Step 2 complete: $srt_count SRT files generated"
fi

echo ""
echo "===== DONE ====="
echo "Output: $OUTPUT_DIR"