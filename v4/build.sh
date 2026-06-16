#!/usr/bin/env sh
set -eu

RC=windres
CC=gcc

if command -v x86_64-w64-mingw32-windres >/dev/null 2>&1; then
  RC=x86_64-w64-mingw32-windres
  CC=x86_64-w64-mingw32-gcc
elif command -v x86_64-w64-mingw32-gcc >/dev/null 2>&1; then
  CC=x86_64-w64-mingw32-gcc
elif command -v i686-w64-mingw32-gcc >/dev/null 2>&1; then
  CC=i686-w64-mingw32-gcc
else
  echo "Error: MinGW cross-compiler not found. Install mingw-w64 and retry." >&2
  exit 1
fi

printf 'Building resources...\n'
$RC ply_viewer.rc -O coff -o ply_viewer_res.o

printf 'Building PLY_Viewer.exe...\n'
$CC -o PLY_Viewer.exe ply_viewer.c ply_viewer_res.o -lopengl32 -lgdi32 -mwindows

printf 'Done: PLY_Viewer.exe\n'
