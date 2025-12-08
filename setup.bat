@echo off
setlocal enabledelayedexpansion

echo.
echo 🎮 Claudemon Setup - mGBA + Claude AI Integration
echo ==================================================

echo.
echo Step 1: Checking system requirements...
echo =======================================

:: Check if we're running from the correct directory
if not exist "CMakeLists.txt" (
    echo ❌ Error: Please run this script from the Claudemon project root directory
    echo    Make sure CMakeLists.txt is in the current directory
    pause
    exit /b 1
)

:: Check for Visual Studio Build Tools or Visual Studio
set VS_FOUND=0
if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2019" set VS_FOUND=1
if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2022" set VS_FOUND=1
if exist "%ProgramFiles%\Microsoft Visual Studio\2019" set VS_FOUND=1  
if exist "%ProgramFiles%\Microsoft Visual Studio\2022" set VS_FOUND=1

if !VS_FOUND!==1 (
    echo ✅ Visual Studio found
) else (
    echo ❌ Visual Studio not found
    echo.
    echo Please install Visual Studio 2019 or 2022 with C++ development tools:
    echo https://visualstudio.microsoft.com/downloads/
    echo.
    echo Or install Build Tools for Visual Studio:
    echo https://visualstudio.microsoft.com/visual-cpp-build-tools/
    pause
    exit /b 1
)

:: Check for CMake
cmake --version >nul 2>&1
if %errorlevel%==0 (
    echo ✅ CMake found
) else (
    echo ❌ CMake not found
    echo.
    echo Please install CMake from: https://cmake.org/download/
    echo Make sure to add CMake to your PATH during installation
    pause
    exit /b 1
)

:: Check for vcpkg (recommended for dependencies)
where vcpkg >nul 2>&1
if %errorlevel%==0 (
    echo ✅ vcpkg found
) else (
    echo ⚠️  vcpkg not found (recommended for easy dependency management)
    echo    You can install vcpkg from: https://github.com/Microsoft/vcpkg
)

echo.
echo Step 2: Dependency information...
echo ==================================
echo.
echo This build requires the following dependencies:
echo - Qt5 or Qt6 (Core, Widgets, Network, Multimedia)
echo - zlib
echo - libpng
echo - SDL2 (optional)
echo.
echo 📦 Using vcpkg (recommended):
echo   vcpkg install qt5-base qt5-multimedia zlib libpng sdl2
echo.
echo 🔧 Or install manually from:
echo   Qt: https://www.qt.io/download
echo   Other libs: Pre-built binaries or compile from source
echo.

set /p continue="Continue with build? (y/n): "
if /i not "%continue%"=="y" (
    echo Build cancelled
    pause
    exit /b 0
)

echo.
echo Step 3: Building Claudemon...
echo =============================

:: Create build directory
if exist "build" (
    echo Removing existing build directory...
    rmdir /s /q "build"
)

echo Creating build directory...
mkdir build
cd build

:: Configure CMake
echo Configuring CMake...

:: Try to find Qt automatically or use vcpkg
set CMAKE_ARGS=-DCMAKE_BUILD_TYPE=RelWithDebInfo

:: If vcpkg is available, use it
where vcpkg >nul 2>&1
if %errorlevel%==0 (
    for /f "tokens=*" %%i in ('vcpkg integrate install 2^>nul ^| findstr /C:"CMake projects"') do (
        echo Using vcpkg toolchain
        set CMAKE_ARGS=!CMAKE_ARGS! -DCMAKE_TOOLCHAIN_FILE=%%~i
    )
)

cmake .. %CMAKE_ARGS%
if %errorlevel% neq 0 (
    echo ❌ CMake configuration failed
    echo.
    echo Troubleshooting:
    echo 1. Make sure Qt is installed and CMAKE_PREFIX_PATH is set
    echo 2. Install dependencies with vcpkg
    echo 3. Check that Visual Studio C++ tools are installed
    cd ..
    pause
    exit /b 1
)

echo ✅ CMake configuration successful

:: Build the project
echo Building project (this may take several minutes)...
cmake --build . --config RelWithDebInfo --parallel
if %errorlevel% neq 0 (
    echo ❌ Build failed
    cd ..
    pause
    exit /b 1
)

echo ✅ Build successful!
cd ..

echo.
echo Step 4: Setup complete!
echo =======================

:: Find the executable
set EXECUTABLE=
if exist "build\RelWithDebInfo\mgba-qt.exe" set EXECUTABLE=build\RelWithDebInfo\mgba-qt.exe
if exist "build\Debug\mgba-qt.exe" set EXECUTABLE=build\Debug\mgba-qt.exe
if exist "build\mgba-qt.exe" set EXECUTABLE=build\mgba-qt.exe

if defined EXECUTABLE (
    echo ✅ Claudemon executable found: %EXECUTABLE%
    echo.
    echo 🚀 Ready to use Claudemon!
    echo.
    echo To run Claudemon:
    echo   %EXECUTABLE%
    echo.
    echo To use Claude AI integration:
    echo   1. Double-click %EXECUTABLE% or run from command line
    echo   2. Load a GBA ROM from the File menu
    echo   3. Open 'Tools ^> Claude AI...' to configure
    echo   4. Enter your Claude API key
    echo   5. Click 'Start Claude' to begin automated gameplay
    echo.
    echo 📁 Place ROM files in the 'roms\' directory
    echo 🔑 Get your Claude API key from: https://console.anthropic.com/
) else (
    echo ⚠️  Executable not found in expected locations
    echo    Look for mgba-qt.exe in the build directory
)

echo.
echo 🎉 Setup complete!
echo.
pause