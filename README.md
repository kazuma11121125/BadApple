# BadApple

ターミナル上で動画を **ANSI カラーブロック文字** として再生するプログラムです。  
## 概要

- 動画フレームを ANSI エスケープコードのカラーブロックに変換してターミナルに出力
- 音声は `ffmpeg` で WAV に変換後、SFML で映像と同期して再生
- OpenMP による並列処理でフレーム変換を高速化
- `fmt` ライブラリによる高速な文字列フォーマット

## 必要なもの

```bash
sudo apt install build-essential cmake ninja-build libopencv-dev ffmpeg libsfml-dev libfmt-dev
```

## ビルド方法

```bash
sh cmake.sh           # Release ビルド（デフォルト）
sh cmake.sh Debug     # Debug ビルド（AddressSanitizer 有効）
```

## 実行方法

```bash
sh run.sh
```

## セットアップ

再生したい動画ファイルをプロジェクトルートに置き、`main.cpp` の先頭にある定数を必要に応じて変更してください。

```cpp
constexpr float volume      = 30.0f;           // 音量 (0.0 〜 100.0)
constexpr float speed       = 1.0f;            // 再生速度
constexpr int   HEIGHT      = 251;             // 出力の高さ（行数）
constexpr float sleep_value = -1;              // 待機フレーム比率（-1 で無効）
const std::string FILENAME  = "bad_apple.mp4"; // 動画ファイル名
const bool is_debug         = false;           // デバッグログ出力
```

## ファイル構成

| ファイル | 説明 |
|---|---|
| `main.cpp` | メインソースコード |
| `CMakeLists.txt` | CMake ビルド設定 |
| `cmake.sh` | CMake 実行・ビルドスクリプト |
| `run.sh` | 実行スクリプト |
| `output.txt` | 実行時のログ出力先 |

## 動作環境

- OS: Ubuntu (Linux)
- C++17 以上
- OpenCV 4
- SFML 2 (Audio)
- OpenMP
- fmt
- ffmpeg
