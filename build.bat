@echo off
set "ORIGINAL_DIR=%CD%"
echo.
echo === Claudemon Build ===

where /q cl || goto :no_cl
where /q ninja || goto :no_ninja

rem Check if we need to do a clean build
set "CLEAN_BUILD=0"
if "%1"=="clean" set "CLEAN_BUILD=1"
if "%1"=="-c" set "CLEAN_BUILD=1"
if "%1"=="--clean" set "CLEAN_BUILD=1"

if "%CLEAN_BUILD%"=="1" (
    if exist "build" (
        echo Cleaning existing build directory...
        rmdir /s /q "build"
    )
) else (
    echo Performing incremental build...
)

if not exist "build" (
    echo Creating build directory...
    mkdir build
)
cd build

echo.
echo Configuring CMake...
set "EXTRA_CMAKE="

rem Set up ccache if available
where /q ccache >nul 2>&1
if %errorlevel% equ 0 (
    echo Using ccache for faster builds
    set "EXTRA_CMAKE=%EXTRA_CMAKE% -DCMAKE_CXX_COMPILER_LAUNCHER=ccache -DCMAKE_C_COMPILER_LAUNCHER=ccache"
)

rem Set up vcpkg if available
if defined VCPKG_ROOT (
  if exist "%VCPKG_ROOT%/scripts/buildsystems/vcpkg.cmake" (
    set "EXTRA_CMAKE=%EXTRA_CMAKE% -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%/scripts/buildsystems/vcpkg.cmake"
    echo Using vcpkg toolchain: %VCPKG_ROOT%/scripts/buildsystems/vcpkg.cmake
  )
)

rem Determine number of cores for parallel compilation
if defined NUMBER_OF_PROCESSORS (
    set "CORES=%NUMBER_OF_PROCESSORS%"
) else (
    set "CORES=4"
)
echo Using %CORES% cores for parallel compilation

cmake .. -G "Ninja" -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_POLICY_VERSION_MINIMUM=3.5 %EXTRA_CMAKE%
if %errorlevel% neq 0 goto :cmake_fail

echo.
echo Building with Ninja (%CORES% parallel jobs)...
ninja -j%CORES%
if %errorlevel% neq 0 goto :build_fail

echo.
echo Build complete!
echo Look for mgba-qt.exe in the build directory.
cd /d "%ORIGINAL_DIR%"
goto :eof

:no_cl
echo [ERROR] cl.exe not found. Run from "x64 Native Tools Command Prompt for VS".
cd /d "%ORIGINAL_DIR%"
goto :eof

:no_ninja
echo [ERROR] ninja not found. Install ninja and add to PATH, or run from environment with ninja available.
echo Download from: https://github.com/ninja-build/ninja/releases
cd /d "%ORIGINAL_DIR%"
goto :eof

:cmake_fail
echo [ERROR] CMake configuration failed.
cd /d "%ORIGINAL_DIR%"
goto :eof

:build_fail
echo [ERROR] Build failed.
cd /d "%ORIGINAL_DIR%"
goto :eof