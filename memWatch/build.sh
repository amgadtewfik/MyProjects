#!/usr/bin/env bash
#
# build.sh — build memWatch into a runnable .app bundle using only
# the Swift command-line toolchain (no Xcode required).
#
# Output: ./build/memWatch.app
# Run:    open ./build/memWatch.app

set -euo pipefail

# Resolve paths relative to this script so the script works from anywhere.
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC_DIR="$SCRIPT_DIR/memWatch"
APP_DIR="$SCRIPT_DIR/build/memWatch.app"
CONTENTS_DIR="$APP_DIR/Contents"
MACOS_DIR="$CONTENTS_DIR/MacOS"
RESOURCES_DIR="$CONTENTS_DIR/Resources"

# Read deployment target from the .pbxproj so this script and the
# project stay in sync. Defaults to 13.0 if not found.
DEPLOYMENT_TARGET="$(grep -oE 'MACOSX_DEPLOYMENT_TARGET = [0-9.]+' "$SRC_DIR/../memWatch.xcodeproj/project.pbxproj" | head -1 | awk -F'= ' '{print $2}')"
DEPLOYMENT_TARGET="${DEPLOYMENT_TARGET:-13.0}"

BUNDLE_ID="com.amgad.memWatch"
APP_NAME="memWatch"
VERSION="1.0"
BUILD_NUMBER="1"

# Locate the macOS SDK that ships with the Command Line Tools.
SDK="$(xcrun --sdk macosx --show-sdk-path 2>/dev/null || true)"
if [ -z "$SDK" ] || [ ! -d "$SDK" ]; then
  echo "error: could not find macOS SDK. Install Command Line Tools:" >&2
  echo "  xcode-select --install" >&2
  exit 1
fi

# Detect host arch (arm64 or x86_64). Required for the -target flag.
ARCH="$(uname -m)"
case "$ARCH" in
  arm64)  TARGET="arm64-apple-macos${DEPLOYMENT_TARGET}" ;;
  x86_64) TARGET="x86_64-apple-macos${DEPLOYMENT_TARGET}" ;;
  *)
    echo "error: unsupported architecture: $ARCH" >&2
    exit 1
    ;;
esac

echo "Building $APP_NAME for $TARGET"

# Clean previous build.
rm -rf "$APP_DIR"
mkdir -p "$MACOS_DIR" "$RESOURCES_DIR"

# Collect sources. Any .swift file directly inside the source dir counts.
SOURCES=()
while IFS= read -r f; do
  SOURCES+=("$f")
done < <(find "$SRC_DIR" -maxdepth 1 -name '*.swift' | sort)

if [ ${#SOURCES[@]} -eq 0 ]; then
  echo "error: no Swift sources found in $SRC_DIR" >&2
  exit 1
fi

# Compile into a single Mach-O binary. swiftc auto-links libSystem
# (which provides Darwin / libproc / Mach), so we only need the
# GUI frameworks listed explicitly.
swiftc \
  -O \
  -sdk "$SDK" \
  -target "$TARGET" \
  -parse-as-library \
  -framework SwiftUI \
  -framework AppKit \
  "${SOURCES[@]}" \
  -o "$MACOS_DIR/$APP_NAME"

# Asset catalog → AppIcon.icns.
#
# macOS apps need an .icns file (not the raw .xcassets folder) when
# they're built without `actool` / full Xcode. We use `iconutil` —
# part of the Command Line Tools — to fold the 10 PNGs from the
# AppIcon set into a single .icns container. The accent color in
# the same catalog still requires .xcassets; we keep the folder too
# so the in-app accent color continues to load.
if [ -d "$SRC_DIR/Assets.xcassets/AppIcon.appiconset" ]; then
  ICONSET_TMP="$(mktemp -d)"
  cp -R "$SRC_DIR/Assets.xcassets/AppIcon.appiconset" "$ICONSET_TMP/AppIcon.iconset"
  if iconutil --convert icns \
        --output "$RESOURCES_DIR/AppIcon.icns" \
        "$ICONSET_TMP/AppIcon.iconset" 2>/dev/null; then
    echo "wrote AppIcon.icns"
  else
    echo "warning: iconutil failed; app will use the default icon" >&2
  fi
  rm -rf "$ICONSET_TMP"
fi

# Copy the accent color set (a single .colorset) so the in-app tint
# keeps working. Drop AppIcon.appiconset — its .icns is what we ship.
ACCENTS_DIR="$SRC_DIR/Assets.xcassets"
if [ -d "$ACCENTS_DIR/AccentColor.colorset" ]; then
  mkdir -p "$RESOURCES_DIR/Assets.xcassets"
  cp -R "$ACCENTS_DIR/AccentColor.colorset" "$RESOURCES_DIR/Assets.xcassets/"
  cp "$ACCENTS_DIR/Contents.json" "$RESOURCES_DIR/Assets.xcassets/Contents.json"
fi

# Copy Preview Content (used only by Xcode previews, but harmless
# to include and keeps parity with the Xcode build).
if [ -d "$SRC_DIR/Preview Content" ]; then
  cp -R "$SRC_DIR/Preview Content" "$RESOURCES_DIR/"
fi

# Generate Info.plist. The minimal keys needed for a SwiftUI app to
# launch and dock correctly.
cat > "$CONTENTS_DIR/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleName</key>                <string>${APP_NAME}</string>
  <key>CFBundleDisplayName</key>         <string>${APP_NAME}</string>
  <key>CFBundleIdentifier</key>          <string>${BUNDLE_ID}</string>
  <key>CFBundleExecutable</key>          <string>${APP_NAME}</string>
  <key>CFBundleInfoDictionaryVersion</key><string>6.0</string>
  <key>CFBundlePackageType</key>         <string>APPL</string>
  <key>CFBundleShortVersionString</key>  <string>${VERSION}</string>
  <key>CFBundleVersion</key>             <string>${BUILD_NUMBER}</string>
  <key>LSMinimumSystemVersion</key>      <string>${DEPLOYMENT_TARGET}</string>
  <key>LSApplicationCategoryType</key>   <string>public.app-category.developer-tools</string>
  <key>NSHighResolutionCapable</key>     <true/>
  <key>NSSupportsAutomaticTermination</key><true/>
  <key>NSSupportsSuddenTermination</key> <true/>
  <key>NSPrincipalClass</key>            <string>NSApplication</string>
  <key>CFBundleIconFile</key>           <string>AppIcon</string>
</dict>
</plist>
PLIST

# Ad-hoc sign the bundle so Gatekeeper is happy on first launch.
# Without this, the app launches fine in many cases but produces a
# warning on stricter setups.
codesign --force --deep --sign - "$APP_DIR" 2>/dev/null || true

echo
echo "Built: $APP_DIR"
echo "Launch with: open '$APP_DIR'"
