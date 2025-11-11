@echo off
REM mGBA Claude AI Integration Setup Script for Windows
REM This script helps you build mGBA with Claude AI support

echo ========================================
echo mGBA Claude AI Integration Setup
echo ========================================
echo.

REM Check if running in Visual Studio environment
if not defined VSINSTALLDIR (
    echo WARNING: Not running in Visual Studio Developer Command Prompt
    echo Please run this script from "x64 Native Tools Command Prompt for VS"
    echo.
    pause
    exit /b 1
)

REM Get API Key
set /p CLAUDE_API_KEY="Enter your Claude API Key (or press Enter to skip): "

REM Create build directory
if not exist build mkdir build
cd build

echo.
echo Configuring CMake with Claude support...
echo.

REM Configure with CMake
cmake .. ^
    -G "Visual Studio 17 2022" ^
    -A x64 ^
    -DUSE_CLAUDE=ON ^
    -DBUILD_QT=ON ^
    -DENABLE_SCRIPTING=ON ^
    -DM_CORE_GBA=ON ^
    -DM_CORE_GB=ON

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo ERROR: CMake configuration failed!
    echo Make sure you have:
    echo   - Visual Studio 2022 installed
    echo   - Qt 5 or Qt 6 installed
    echo   - libcurl development libraries
    echo.
    pause
    exit /b 1
)

echo.
echo Building mGBA...
echo This may take several minutes...
echo.

cmake --build . --config Release

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo ERROR: Build failed!
    echo Check the error messages above.
    echo.
    pause
    exit /b 1
)

echo.
echo ========================================
echo Build completed successfully!
echo ========================================
echo.

REM Create config file if API key was provided
if not "%CLAUDE_API_KEY%"=="" (
    echo Creating configuration file...
    cd Release

    (
        echo {
        echo   "api_key": "%CLAUDE_API_KEY%",
        echo   "model": "claude-sonnet-4-5-20250929",
        echo   "max_tokens": 1024,
        echo   "frames_per_query": 60,
        echo   "include_screenshot": true,
        echo   "include_ram": true,
        echo   "temperature": 1.0
        echo }
    ) > claude-config.json

    echo Configuration saved to: %CD%\claude-config.json
    echo.
    cd ..
)

echo.
echo The mGBA executable is located at:
echo %CD%\Release\mGBA-qt.exe
echo.
echo To use Claude AI:
echo   1. Load a GBA ROM (like Pokemon Emerald)
echo   2. Go to Tools -^> Claude AI Player...
echo   3. Configure your API key in Settings (if not done above)
echo   4. Click "Start AI" to let Claude play!
echo.
echo Configuration file will be stored at:
echo   %APPDATA%\mGBA\claude-config.json
echo.

pause
