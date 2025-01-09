#!/bin/bash

# ASCII Art or Project Banner
echo "Building Bad-Apple with CMake and Ninja..."

# Define project and build directories
PROJECT_DIR="$PWD"
BUILD_DIR="$PROJECT_DIR/build"

# Clean old build directory if it exists
if [ -d "$BUILD_DIR" ]; then
    echo "Cleaning up old build directory..."
    rm -rf "$BUILD_DIR"
fi

# Run CMake with Ninja generator
echo "Running CMake..."
cmake -G Ninja -B "$BUILD_DIR" "$PROJECT_DIR" -DCMAKE_BUILD_TYPE=Debug

# Check if CMake succeeded
if [ $? -ne 0 ]; then
    echo "CMake configuration failed!"
    exit 1
fi

# Navigate to the build directory
cd "$BUILD_DIR" || exit

# Build the project with Ninja
echo "Building project with Ninja..."
ninja > build.log 2>&1

# Check the build result
if [ $? -eq 0 ]; then
    echo "Build successful!"
else
    echo "Build failed! Check build.log for details."
    exit 1
fi
