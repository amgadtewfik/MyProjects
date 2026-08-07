# m2ts_to_mp4

Convert m2ts (MPEG-TS) files to mp4 with optional Japanese→English subtitles.

## Requirements

- **ffmpeg** - Download from https://www.gyan.dev/ffmpeg/builds/
- **Python 3.8+** (for subtitle generation)
- **faster-whisper** + **deep-translator** (optional, for SRT subtitles)

Install Python deps:
```bash
pip install faster-whisper deep-translator
```

## Usage

### Batch Convert (Windows)
```cmd
convert_all.bat
```

### Batch Convert (macOS/Linux)
```bash
./convert_all.sh
```

### Single File Convert
```bash
python converter.py input.m2ts -o output.mp4
```

### With Japanese→English Subtitles
```bash
./convert_all.sh --srt
# or
convert_all.bat --srt
```

## Options

| Flag | Description |
|------|-------------|
| `-v copy` | Video codec (copy, libx264, libx265) |
| `-a copy` | Audio codec (copy, aac, libmp3lame) |
| `-c 18` | CRF for re-encoding (lower = better quality) |
| `--no-faststart` | Disable faststart flag |
| `--srt` | Generate Japanese→English SRT |
| `--srt-model` | Whisper model size (tiny, base, small, medium, large) |

## Output

- Input files go in `input/` folder
- Converted mp4 files go in `output/` folder
- SRT subtitles saved alongside mp4 files