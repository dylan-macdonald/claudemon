#!/bin/bash
set -e

echo "🎮 Claudemon Setup - mGBA + Claude AI Integration"
echo "=================================================="

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Function to check if command exists
command_exists() {
    command -v "$1" >/dev/null 2>&1
}

# Function to print status
print_status() {
    if [ $1 -eq 0 ]; then
        echo -e "${GREEN}✓${NC} $2"
    else
        echo -e "${RED}✗${NC} $2"
    fi
}

# Function to print info
print_info() {
    echo -e "${BLUE}ℹ${NC} $1"
}

# Function to print warning
print_warning() {
    echo -e "${YELLOW}⚠${NC} $1"
}

echo ""
echo "Step 1: Checking system requirements..."
echo "======================================="

# Check OS
if [[ "$OSTYPE" == "linux-gnu"* ]]; then
    OS="linux"
    print_status 0 "Running on Linux"
elif [[ "$OSTYPE" == "darwin"* ]]; then
    OS="macos"
    print_status 0 "Running on macOS"
elif [[ "$OSTYPE" == "msys"* ]] || [[ "$OSTYPE" == "cygwin"* ]]; then
    OS="windows"
    print_status 0 "Running on Windows (WSL/MSYS2)"
else
    print_status 1 "Unknown OS: $OSTYPE"
    exit 1
fi

# Check required tools
missing_deps=()

if command_exists cmake; then
    CMAKE_VERSION=$(cmake --version | head -n1 | cut -d' ' -f3)
    print_status 0 "CMake found: $CMAKE_VERSION"
else
    print_status 1 "CMake not found"
    missing_deps+=("cmake")
fi

if command_exists pkg-config; then
    print_status 0 "pkg-config found"
else
    print_status 1 "pkg-config not found"
    missing_deps+=("pkg-config")
fi

if command_exists make; then
    print_status 0 "Make found"
else
    print_status 1 "Make not found"
    missing_deps+=("make" "build-essential")
fi

if command_exists gcc; then
    GCC_VERSION=$(gcc --version | head -n1 | cut -d')' -f2 | cut -d' ' -f2)
    print_status 0 "GCC found: $GCC_VERSION"
else
    print_status 1 "GCC not found"
    missing_deps+=("gcc" "build-essential")
fi

echo ""
echo "Step 2: Checking Qt and libraries..."
echo "====================================="

# Check Qt5/Qt6 development packages
QT_FOUND=false
if pkg-config --exists Qt5Core Qt5Widgets Qt5Network; then
    QT_VERSION=$(pkg-config --modversion Qt5Core)
    print_status 0 "Qt5 development libraries found: $QT_VERSION"
    QT_FOUND=true
elif pkg-config --exists Qt6Core Qt6Widgets Qt6Network; then
    QT_VERSION=$(pkg-config --modversion Qt6Core)
    print_status 0 "Qt6 development libraries found: $QT_VERSION"
    QT_FOUND=true
else
    print_status 1 "Qt development libraries not found"
    missing_deps+=("qtbase5-dev" "qtmultimedia5-dev")
fi

# Check other required libraries
if pkg-config --exists libpng; then
    LIBPNG_VERSION=$(pkg-config --modversion libpng)
    print_status 0 "libpng found: $LIBPNG_VERSION"
else
    print_status 1 "libpng not found"
    missing_deps+=("libpng-dev")
fi

if pkg-config --exists zlib; then
    ZLIB_VERSION=$(pkg-config --modversion zlib)
    print_status 0 "zlib found: $ZLIB_VERSION"
else
    print_status 1 "zlib not found"
    missing_deps+=("zlib1g-dev")
fi

if pkg-config --exists sdl2; then
    SDL_VERSION=$(pkg-config --modversion sdl2)
    print_status 0 "SDL2 found: $SDL_VERSION"
else
    print_warning "SDL2 not found (optional)"
fi

echo ""
if [ ${#missing_deps[@]} -eq 0 ]; then
    echo -e "${GREEN}✓ All dependencies found!${NC}"
else
    echo -e "${RED}✗ Missing dependencies detected${NC}"
    echo ""
    echo "Please install the following packages:"
    echo "======================================"
    
    if [[ "$OS" == "linux" ]]; then
        if command_exists apt; then
            echo "Ubuntu/Debian:"
            echo "sudo apt update"
            echo "sudo apt install ${missing_deps[*]}"
        elif command_exists yum; then
            echo "CentOS/RHEL:"
            echo "sudo yum install ${missing_deps[*]}"
        elif command_exists dnf; then
            echo "Fedora:"
            echo "sudo dnf install ${missing_deps[*]}"
        elif command_exists pacman; then
            echo "Arch Linux:"
            echo "sudo pacman -S ${missing_deps[*]}"
        fi
    elif [[ "$OS" == "macos" ]]; then
        echo "macOS (using Homebrew):"
        echo "brew install cmake qt5 libpng zlib sdl2 pkg-config"
    fi
    
    echo ""
    echo "After installing dependencies, re-run this script."
    exit 1
fi

echo ""
echo "Step 3: Building Claudemon..."
echo "============================="

# Create build directory
BUILD_DIR="build"
if [ -d "$BUILD_DIR" ]; then
    print_info "Removing existing build directory..."
    rm -rf "$BUILD_DIR"
fi

print_info "Creating build directory..."
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Configure CMake
print_info "Configuring CMake..."
CMAKE_ARGS="-DCMAKE_BUILD_TYPE=RelWithDebInfo"

if [[ "$OS" == "macos" ]] && command_exists brew; then
    QT_PATH=$(brew --prefix qt5 2>/dev/null || brew --prefix qt6 2>/dev/null || echo "")
    if [ ! -z "$QT_PATH" ]; then
        CMAKE_ARGS="$CMAKE_ARGS -DCMAKE_PREFIX_PATH=$QT_PATH"
    fi
fi

if cmake .. $CMAKE_ARGS; then
    print_status 0 "CMake configuration successful"
else
    print_status 1 "CMake configuration failed"
    exit 1
fi

# Build the project
print_info "Building project (this may take a few minutes)..."
CORES=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo "4")
print_info "Using $CORES CPU cores for parallel build"

if make -j"$CORES"; then
    print_status 0 "Build successful!"
else
    print_status 1 "Build failed"
    exit 1
fi

cd ..

echo ""
echo "Step 4: Setup complete!"
echo "======================="

# Find the executable
EXECUTABLE=""
if [ -f "build/mgba-qt" ]; then
    EXECUTABLE="build/mgba-qt"
elif [ -f "build/mgba-qt.exe" ]; then
    EXECUTABLE="build/mgba-qt.exe"
elif [ -f "build/src/platform/qt/mgba-qt" ]; then
    EXECUTABLE="build/src/platform/qt/mgba-qt"
fi

if [ ! -z "$EXECUTABLE" ]; then
    print_status 0 "Claudemon executable found: $EXECUTABLE"
    
    echo ""
    echo "🚀 Ready to use Claudemon!"
    echo ""
    echo "To run Claudemon:"
    echo "  ./$EXECUTABLE"
    echo ""
    echo "To use Claude AI integration:"
    echo "  1. Start Claudemon: ./$EXECUTABLE"
    echo "  2. Load a GBA ROM from the Tools menu"
    echo "  3. Open 'Tools > Claude AI...' to configure"
    echo "  4. Enter your Claude API key"
    echo "  5. Click 'Start Claude' to begin automated gameplay"
    echo ""
    echo "📁 Place ROM files in the 'roms/' directory"
    echo "🔑 Get your Claude API key from: https://console.anthropic.com/"
    
    # Make executable
    chmod +x "$EXECUTABLE"
else
    print_warning "Executable not found in expected locations"
    print_info "Look for mgba-qt in the build directory"
fi

echo ""
echo -e "${GREEN}Setup complete! 🎉${NC}"