#!/bin/bash
# mGBA Claude AI Integration Setup Script for Linux/macOS
# This script helps you build mGBA with Claude AI support

set -e

echo "========================================"
echo "mGBA Claude AI Integration Setup"
echo "========================================"
echo ""

# Check for required tools
check_command() {
    if ! command -v $1 &> /dev/null; then
        echo "ERROR: $1 is not installed."
        echo "Please install $1 and try again."
        exit 1
    fi
}

echo "Checking for required tools..."
check_command cmake
check_command make
check_command curl

# Check for Qt
if ! command -v qmake &> /dev/null && ! command -v qmake-qt5 &> /dev/null && ! command -v qmake6 &> /dev/null; then
    echo "ERROR: Qt is not installed or qmake is not in PATH."
    echo "Please install Qt 5 or Qt 6 development libraries."
    echo ""
    echo "On Ubuntu/Debian: sudo apt-get install qtbase5-dev qtmultimedia5-dev libqt5opengl5-dev"
    echo "On Fedora: sudo dnf install qt5-qtbase-devel qt5-qtmultimedia-devel"
    echo "On macOS: brew install qt@5"
    exit 1
fi

# Check for libcurl
if ! pkg-config --exists libcurl; then
    echo "ERROR: libcurl development libraries not found."
    echo ""
    echo "On Ubuntu/Debian: sudo apt-get install libcurl4-openssl-dev"
    echo "On Fedora: sudo dnf install libcurl-devel"
    echo "On macOS: (curl is usually pre-installed)"
    exit 1
fi

echo "All required tools found!"
echo ""

# Get API Key
read -p "Enter your Claude API Key (or press Enter to skip): " CLAUDE_API_KEY
echo ""

# Create build directory
mkdir -p build
cd build

echo "Configuring CMake with Claude support..."
echo ""

# Configure with CMake
cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DUSE_CLAUDE=ON \
    -DBUILD_QT=ON \
    -DENABLE_SCRIPTING=ON \
    -DM_CORE_GBA=ON \
    -DM_CORE_GB=ON

echo ""
echo "Building mGBA..."
echo "This may take several minutes..."
echo ""

# Build
make -j$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 2)

echo ""
echo "========================================"
echo "Build completed successfully!"
echo "========================================"
echo ""

# Create config file if API key was provided
if [ -n "$CLAUDE_API_KEY" ]; then
    echo "Creating configuration file..."

    # Determine config directory
    if [ "$(uname)" == "Darwin" ]; then
        CONFIG_DIR="$HOME/Library/Application Support/mGBA"
    else
        CONFIG_DIR="${XDG_CONFIG_HOME:-$HOME/.config}/mGBA"
    fi

    mkdir -p "$CONFIG_DIR"
    CONFIG_FILE="$CONFIG_DIR/claude-config.json"

    cat > "$CONFIG_FILE" <<EOF
{
  "api_key": "$CLAUDE_API_KEY",
  "model": "claude-sonnet-4-5-20250929",
  "max_tokens": 1024,
  "frames_per_query": 60,
  "include_screenshot": true,
  "include_ram": true,
  "temperature": 1.0
}
EOF

    echo "Configuration saved to: $CONFIG_FILE"
    echo ""
fi

echo ""
echo "The mGBA executable is located at:"
echo "$(pwd)/qt/mGBA-qt"
echo ""
echo "To use Claude AI:"
echo "  1. Load a GBA ROM (like Pokemon Emerald)"
echo "  2. Go to Tools -> Claude AI Player..."
echo "  3. Configure your API key in Settings (if not done above)"
echo "  4. Click 'Start AI' to let Claude play!"
echo ""

# Create desktop launcher (optional)
read -p "Would you like to install mGBA to /usr/local? (requires sudo) [y/N]: " INSTALL
if [[ "$INSTALL" =~ ^[Yy]$ ]]; then
    echo ""
    sudo make install
    echo "mGBA installed to /usr/local/bin/mGBA-qt"
fi

echo ""
echo "Setup complete!"
