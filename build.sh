#!/bin/bash
set -e

echo
echo "=== Claudemon Build ==="

# Check for required tools
if ! command -v cmake >/dev/null 2>&1; then
    echo "[ERROR] cmake not found. Please install cmake and add to PATH."
    exit 1
fi

if ! command -v ninja >/dev/null 2>&1; then
    echo "[ERROR] ninja not found. Please install ninja and add to PATH."
    echo "Install with: sudo apt-get install ninja-build (Ubuntu/Debian) or brew install ninja (macOS)"
    exit 1
fi

# Check if we need to do a clean build
CLEAN_BUILD=0
if [[ "$1" == "clean" ]] || [[ "$1" == "-c" ]] || [[ "$1" == "--clean" ]]; then
    CLEAN_BUILD=1
fi

if [[ $CLEAN_BUILD -eq 1 ]]; then
    if [[ -d "build" ]]; then
        echo "Cleaning existing build directory..."
        rm -rf "build"
    fi
else
    echo "Performing incremental build..."
fi

if [[ ! -d "build" ]]; then
    echo "Creating build directory..."
    mkdir build
fi
cd build

echo
echo "Configuring CMake..."
EXTRA_CMAKE=""

# Set up ccache if available
if command -v ccache >/dev/null 2>&1; then
    echo "Using ccache for faster builds"
    EXTRA_CMAKE="$EXTRA_CMAKE -DCMAKE_CXX_COMPILER_LAUNCHER=ccache -DCMAKE_C_COMPILER_LAUNCHER=ccache"
fi

# Set up vcpkg if available
if [[ -n "$VCPKG_ROOT" ]] && [[ -f "$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" ]]; then
    EXTRA_CMAKE="$EXTRA_CMAKE -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
    echo "Using vcpkg toolchain: $VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
fi

# Determine number of cores for parallel compilation
CORES=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
echo "Using $CORES cores for parallel compilation"

cmake .. -G "Ninja" -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_POLICY_VERSION_MINIMUM=3.5 $EXTRA_CMAKE
if [[ $? -ne 0 ]]; then
    echo "[ERROR] CMake configuration failed."
    cd ..
    exit 1
fi

echo
echo "Building with Ninja ($CORES parallel jobs)..."
ninja -j$CORES
if [[ $? -ne 0 ]]; then
    echo "[ERROR] Build failed."
    cd ..
    exit 1
fi

echo
echo "Build complete!"
echo "Look for mgba-qt in the build directory."
cd ..