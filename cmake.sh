#!/bin/bash

echo "${ART}"

PROJECT_DIR="$PWD"
BUILD_DIR="$PROJECT_DIR/build"

# ビルドタイプを引数から取得（デフォルト: Release）
BUILD_TYPE="${1:-Release}"

echo "Build type: $BUILD_TYPE"

if [ -d "$BUILD_DIR" ]; then
    echo "Cleaning up old build directory..."
    rm -rf "$BUILD_DIR"
fi

echo "Running CMake..."
cmake -G Ninja -B "$BUILD_DIR" "$PROJECT_DIR" -DCMAKE_BUILD_TYPE="$BUILD_TYPE"

cd "$BUILD_DIR" || exit

echo "Building project with Ninja..."
ninja > build.log 2>&1

if [ $? -eq 0 ]; then
    echo "Build successful!"
else
    echo "Build failed! Check build.log for details."
fi
