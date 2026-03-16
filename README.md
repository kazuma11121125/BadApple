# BadApple

ターミナル上で動画をカラーASCII風（ハーフブロック描画）で再生するC++プロジェクトです。  
OpenCVで動画フレームを処理し、SFMLで音声を再生します。
color-v2の後継に当たる開発中のものです。

## 動作環境

- Linux（Ubuntu想定）
- CMake + Ninja
- g++

## 必要パッケージ

```bash
sudo apt update
sudo apt install -y build-essential cmake ninja-build libopencv-dev ffmpeg libsfml-dev libfmt-dev
```

## 使い方

1. プロジェクト直下に再生したい動画ファイルを配置
2. `main.cpp` の `FILENAME` を動画名に合わせて変更
3. ビルド
4. 実行

### ビルド

```bash
sh cmake.sh
```

Debugビルドする場合:

```bash
sh cmake.sh Debug
```

### 実行

```bash
sh run.sh
```

### ログ確認（任意）

`output.txt` を監視するには:

```bash
sh log.sh
```

## 設定項目（`main.cpp`）

```cpp
constexpr float volume = 30.0f;         // 音量
constexpr float speed = 1.0f;           // 再生速度
constexpr float sleep_value = -1;       // 事前待機制御（-1で無効）
const std::string FILENAME = "tadakimi.mp4"; // 動画ファイル名
constexpr float FONT_CORRECTION = 2.76f; // フォント縦横比補正
constexpr int MAX_RENDER_WIDTH = 2000;   // 描画幅上限
```

## 補足

- 実行時に `ffmpeg` で音声を `output.wav` に抽出します。
- ターミナルサイズに合わせて描画解像度を自動調整します。
- 実行中はカーソル非表示・折り返し無効化を行い、終了時に復元します。
