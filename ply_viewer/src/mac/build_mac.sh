#!/usr/bin/env sh
set -eu

SRC_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SRC_DIR/../.." && pwd)"
DIST_DIR="$ROOT_DIR/dist"
APP_NAME="PLY Viewer"
APP_DIR="$DIST_DIR/$APP_NAME.app"
BUILD_DIR="$ROOT_DIR/build/mac"

mkdir -p "$BUILD_DIR" "$DIST_DIR"

if [ ! -f "$BUILD_DIR/AppIcon.icns" ]; then
  printf 'Generating AppIcon.icns...\n'
  if python3 -c "import PIL" >/dev/null 2>&1; then
    (cd "$BUILD_DIR" && python3 "$SRC_DIR/create_icon_mac.py" AppIcon.icns)
  else
    printf 'Pillow not installed (pip3 install pillow); building without icon.\n'
  fi
fi

printf 'Compiling %s...\n' "$APP_NAME"
clang -O2 -fobjc-arc -mmacosx-version-min=11.0 \
  -Wall -Wno-deprecated-declarations \
  -o "$BUILD_DIR/$APP_NAME" \
  "$SRC_DIR/main.m" "$SRC_DIR/ply_core.c" \
  -framework Cocoa -framework OpenGL -framework QuartzCore -framework UniformTypeIdentifiers

printf 'Assembling bundle...\n'
rm -rf "$APP_DIR"
mkdir -p "$APP_DIR/Contents/MacOS" "$APP_DIR/Contents/Resources"
cp "$BUILD_DIR/$APP_NAME" "$APP_DIR/Contents/MacOS/$APP_NAME"
cp "$SRC_DIR/Info.plist" "$APP_DIR/Contents/Info.plist"
printf 'APPL????' > "$APP_DIR/Contents/PkgInfo"
if [ -f "$BUILD_DIR/AppIcon.icns" ]; then
  cp "$BUILD_DIR/AppIcon.icns" "$APP_DIR/Contents/Resources/AppIcon.icns"
fi

codesign --force --sign - "$APP_DIR" >/dev/null 2>&1 || true

printf 'Done: %s\n' "$APP_DIR"
