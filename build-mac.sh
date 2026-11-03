#!/bin/bash
# =============================================================================
#  FretForge Studio - one-step macOS build
#
#    ./build-mac.sh            Release build (Apple Silicon + Intel), tests, .dmg
#    ./build-mac.sh clean      delete the previous build first
#    ./build-mac.sh native     build only for this Mac's CPU (faster)
#
#  Needs: Xcode Command Line Tools and CMake (the script tells you how to get them).
# =============================================================================
set -e
cd "$(dirname "$0")"

CLEAN=0
ARCHS="arm64;x86_64"
for arg in "$@"; do
    case "$arg" in
        clean)  CLEAN=1 ;;
        native) ARCHS="$(uname -m)" ;;
    esac
done

BUILD_DIR="$PWD/build-mac"
echo
echo " ============================================"
echo "  FretForge Studio  -  macOS build"
echo " ============================================"
echo

# ------------------------------------------------------------------ tools
echo "[1/5] Checking tools..."
if ! xcode-select -p >/dev/null 2>&1; then
    echo
    echo " Xcode Command Line Tools are not installed."
    echo " A window will open to install them. When it finishes, run this script again."
    xcode-select --install || true
    exit 1
fi
if ! command -v cmake >/dev/null 2>&1; then
    echo
    echo " CMake is not installed. Install it with one of:"
    echo "   brew install cmake            (if you use Homebrew: https://brew.sh)"
    echo "   or download the macOS .dmg from https://cmake.org/download/ and run:"
    echo "   sudo \"/Applications/CMake.app/Contents/bin/cmake-gui\" --install"
    exit 1
fi
if ! command -v git >/dev/null 2>&1; then
    echo " git is missing (it comes with the Command Line Tools). Run: xcode-select --install"
    exit 1
fi
GENERATOR="Unix Makefiles"
command -v ninja >/dev/null 2>&1 && GENERATOR="Ninja"
echo "      CMake $(cmake --version | head -1 | awk '{print $3}'), generator: $GENERATOR, architectures: $ARCHS"

# ------------------------------------------------------------------ configure
if [ "$CLEAN" = "1" ]; then rm -rf "$BUILD_DIR"; fi
if [ -f "$BUILD_DIR/CMakeCache.txt" ] && ! grep -q "CMAKE_GENERATOR:INTERNAL=$GENERATOR" "$BUILD_DIR/CMakeCache.txt"; then
    rm -rf "$BUILD_DIR"
fi

echo "[2/5] Configuring (first run downloads JUCE - a few minutes)..."
cmake -S . -B "$BUILD_DIR" -G "$GENERATOR" \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_OSX_ARCHITECTURES="$ARCHS" \
      -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0

# ------------------------------------------------------------------ build
echo "[3/5] Compiling (10-20 minutes the first time)..."
JOBS=$(sysctl -n hw.ncpu 2>/dev/null || echo 4)
cmake --build "$BUILD_DIR" --config Release --parallel "$JOBS"

APP="$BUILD_DIR/FretForgeStudio_artefacts/Release/FretForge Studio.app"
if [ ! -d "$APP" ]; then
    echo " BUILD FAILED: the app bundle was not produced."
    exit 1
fi
# Ad-hoc signature so macOS will run the locally built app (not a Developer ID signature).
codesign --force --deep --sign - "$APP" >/dev/null 2>&1 || true

# ------------------------------------------------------------------ tests
echo "[4/5] Running unit tests..."
TESTS="$BUILD_DIR/ff_tests_artefacts/Release/ff_tests"
if [ -x "$TESTS" ]; then
    if "$TESTS" > "$BUILD_DIR/test-results.txt" 2>&1; then
        echo "      All tests passed."
    else
        echo "      WARNING: some tests failed - see build-mac/test-results.txt"
    fi
fi

# ------------------------------------------------------------------ package
echo "[5/5] Making a .dmg..."
( cd "$BUILD_DIR" && cpack -G DragNDrop >/dev/null 2>&1 ) || echo "      (dmg packaging failed; the .app itself is fine)"
DMG=$(ls "$BUILD_DIR"/FretForgeStudio-*.dmg 2>/dev/null | head -1)

echo
echo " ============================================"
echo "  BUILD SUCCEEDED"
echo " ============================================"
echo "  App: $APP"
[ -n "$DMG" ] && echo "  DMG: $DMG"
echo
echo "  To install: drag the app into Applications (or open the .dmg)."
echo "  First launch: right-click the app > Open > Open (it is not notarised yet)."
echo "  macOS will ask for microphone access so the tuner and recording can hear your guitar."
echo
open "$(dirname "$APP")" || true
read -r -p "  Launch FretForge Studio now? [y/N] " ans
if [[ "$ans" =~ ^[Yy] ]]; then open "$APP"; fi
