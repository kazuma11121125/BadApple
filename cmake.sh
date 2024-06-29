echo "${ART}"

# プロジェクトディレクトリのパス
PROJECT_DIR="$PWD"

# ビルドディレクトリのパス
BUILD_DIR="$PROJECT_DIR/build"

# CMake の実行
echo "Running CMake..."
cmake -G Ninja -B "$BUILD_DIR" "$PROJECT_DIR"

# カレントディレクトリをビルドディレクトリに変更
cd "$BUILD_DIR" || exit

# Ninja を使用してビルド
echo "Building project with Ninja..."
ninja

# ビルドが成功したかを確認
if [ $? -eq 0 ]; then
    echo "Build successful!"
else
    echo "Build failed!"
fi
