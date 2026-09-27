#!/bin/bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
CC="${CC:-x86_64-w64-mingw32-gcc}"
WINDRES="${WINDRES:-x86_64-w64-mingw32-windres}"
OUT="$ROOT/dist/PLY_Viewer.exe"
OBJ="$ROOT/build/win"

command -v "$CC" >/dev/null || { echo "error: $CC not found (brew install mingw-w64)"; exit 1; }

mkdir -p "$OBJ" "$ROOT/dist"

"$WINDRES" -I "$ROOT/src/win" "$ROOT/src/win/ply_viewer.rc" -O coff -o "$OBJ/ply_viewer_res.o"

"$CC" -O2 -std=c11 \
    -I"$ROOT/src/win" -I"$ROOT/src/mac" \
    -o "$OUT" \
    "$ROOT/src/win/main_win.c" \
    "$ROOT/src/win/gl3_win.c" \
    "$ROOT/src/win/png_write.c" \
    "$ROOT/src/mac/ply_core.c" \
    "$OBJ/ply_viewer_res.o" \
    -mwindows -static -static-libgcc \
    -lopengl32 -lgdi32 -lcomdlg32 -lshell32 -ladvapi32 -lpthread -lm

echo "built $OUT"
ls -lh "$OUT"
