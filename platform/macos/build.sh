#!/usr/bin/env bash
# ALI macOS Build Script
#
# Builds the C++ core then compiles the Swift app into a runnable .app bundle.
#
# Requirements:
#   brew install cmake openssl@3
#   Xcode Command Line Tools (xcode-select --install)
#
# Usage:
#   cd platform/macos
#   ./build.sh          # build only
#   ./build.sh run      # build then launch
#   ./build.sh clean    # remove ALI.app

set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUILD_DIR="$REPO_ROOT/build"
APP_BUNDLE="$SCRIPT_DIR/ALI.app"
OPENSSL_PREFIX="/opt/homebrew/opt/openssl@3"

# ── Clean ────────────────────────────────────────────────────────────────────
if [[ "${1:-}" == "clean" ]]; then
    echo "→ Cleaning…"
    rm -rf "$APP_BUNDLE" "$SCRIPT_DIR/ali_macos_bin"
    exit 0
fi

echo "══════════════════════════════════════════"
echo "  ALI macOS Build"
echo "══════════════════════════════════════════"

# ── Step 1: Build C++ core (Release) ─────────────────────────────────────────
echo "→ Building C++ core…"
cmake -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DALI_BUILD_TESTS=OFF \
    -DALI_BUILD_BENCHMARKS=OFF \
    -DALI_BUILD_TOOLS=OFF \
    -DCMAKE_OSX_ARCHITECTURES="arm64" \
    -DOPENSSL_ROOT_DIR="$OPENSSL_PREFIX" \
    -S "$REPO_ROOT" \
    -Wno-dev \
    2>&1 | grep -E "^-- |error:" | head -5

cmake --build "$BUILD_DIR" --target ali_capi -j"$(sysctl -n hw.ncpu)" 2>&1 | tail -3

CAPI_LIB="$BUILD_DIR/interfaces/c_api/libali_capi.a"
[[ -f "$CAPI_LIB" ]] || { echo "✗ C++ build failed"; exit 1; }
echo "✓ C++ core built"

# ── Step 2: Compile Swift ─────────────────────────────────────────────────────
echo "→ Compiling Swift app…"
MACOS_SDK="$(xcrun --sdk macosx --show-sdk-path)"

swiftc \
  -target arm64-apple-macosx13.0 \
  -sdk "$MACOS_SDK" \
  -O -module-name ALI -parse-as-library -swift-version 5 \
  -I "$REPO_ROOT/interfaces/c_api" \
  -import-objc-header "$SCRIPT_DIR/Sources/ALI/BridgingHeader.h" \
  -Xcc "-I$REPO_ROOT/interfaces/c_api" \
  -framework AppKit -framework WebKit -framework Foundation -framework Combine \
  -L "$BUILD_DIR/interfaces/c_api" \
  -L "$BUILD_DIR/core" \
  -L "$OPENSSL_PREFIX/lib" \
  -Xlinker -lali_capi \
  -Xlinker -lali_core \
  -Xlinker -lsqlite3 \
  -Xlinker "$OPENSSL_PREFIX/lib/libssl.a" \
  -Xlinker "$OPENSSL_PREFIX/lib/libcrypto.a" \
  -Xlinker -lc++ \
  -o "$SCRIPT_DIR/ali_macos_bin" \
  "$SCRIPT_DIR/Sources/ALI/ALIApp.swift" \
  "$SCRIPT_DIR/Sources/ALI/AliEngine.swift" \
  "$SCRIPT_DIR/Sources/ALI/BrowserModel.swift" \
  "$SCRIPT_DIR/Sources/ALI/WebViewRepresentable.swift" \
  "$SCRIPT_DIR/Sources/ALI/ContentView.swift" \
  2>&1 | grep -v "^$" | grep -v "warning:" || true

echo "✓ Swift compiled"

# ── Step 3: Assemble .app bundle ──────────────────────────────────────────────
echo "→ Assembling ALI.app…"
CONTENTS="$APP_BUNDLE/Contents"
MACOS_DIR="$CONTENTS/MacOS"
RESOURCES_DIR="$CONTENTS/Resources"

rm -rf "$APP_BUNDLE"
mkdir -p "$MACOS_DIR" "$RESOURCES_DIR"

mv "$SCRIPT_DIR/ali_macos_bin" "$MACOS_DIR/ALI"
chmod +x "$MACOS_DIR/ALI"
cp "$SCRIPT_DIR/Resources/AppIcon.icns" "$RESOURCES_DIR/AppIcon.icns" 2>/dev/null || true

cat > "$CONTENTS/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleName</key>               <string>ALI</string>
    <key>CFBundleDisplayName</key>        <string>ALI</string>
    <key>CFBundleIdentifier</key>         <string>com.ali.search</string>
    <key>CFBundleVersion</key>            <string>1.0</string>
    <key>CFBundleShortVersionString</key> <string>1.0</string>
    <key>CFBundleExecutable</key>         <string>ALI</string>
    <key>CFBundlePackageType</key>        <string>APPL</string>
    <key>CFBundleIconFile</key>           <string>AppIcon</string>
    <key>LSMinimumSystemVersion</key>     <string>13.0</string>
    <key>NSHighResolutionCapable</key>    <true/>
    <key>NSPrincipalClass</key>           <string>NSApplication</string>
    <key>NSSupportsAutomaticTermination</key> <false/>
    <key>NSAppTransportSecurity</key>
    <dict><key>NSAllowsArbitraryLoads</key><true/></dict>
</dict>
</plist>
PLIST

BIN_SIZE=$(ls -lh "$MACOS_DIR/ALI" | awk '{print $5}')
echo "✓ ALI.app assembled (binary: $BIN_SIZE)"
echo ""
echo "══════════════════════════════════════════"
echo "  Build complete."
echo ""
echo "  Launch:  open \"$APP_BUNDLE\""
echo "  Or run:  ./build.sh run"
echo "══════════════════════════════════════════"

if [[ "${1:-}" == "run" ]]; then
    open "$APP_BUNDLE"
fi
