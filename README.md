# BadApple

> [!WARNING]
> **このプロジェクトは現在使用していません。非推奨（Deprecated）です。**
> 新規利用・本番利用は推奨しません。

ターミナル上で動画を文字描画（ANSIカラー）で再生する実験用のC++プロジェクトです。  
OpenCVで動画フレームを処理し、SFMLで音声を再生します。

## 状態

- ステータス: 非推奨 / メンテナンス予定なし
- 用途: 参考・検証用

## 必要パッケージ（Ubuntu）

```bash
sudo apt update
sudo apt install -y build-essential cmake ninja-build libopencv-dev ffmpeg libsfml-dev libfmt-dev
```

## 使い方（参考）

1. 再生したい動画をプロジェクト直下に配置
2. [main.cpp](main.cpp) の `FILENAME` を変更
3. ビルド: `sh cmake.sh`
4. 実行: `sh run.sh`

## 主な設定（[main.cpp](main.cpp)）

```cpp
constexpr float volume = 30.0f;
constexpr float speed = 1.0f;
constexpr float sleep_value = -1;
const std::string FILENAME = "tadakimi.mp4";
constexpr float FONT_CORRECTION = 2.76f;
constexpr int MAX_RENDER_WIDTH = 2000;
```

## 補足

- 実行時に `ffmpeg` を使って音声を `output.wav` として抽出します。
- ログ確認は `sh log.sh` で `output.txt` を追跡できます。