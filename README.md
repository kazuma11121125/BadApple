# BadApple

ターミナル上で動画をカラー表示する C++ プロジェクトです。  
描画は UTF-8 のハーフブロック（`▀`）を使い、1 文字セルで上下 2 ピクセルを表現します。

- 動画フレーム処理: `OpenCV`
- 音声再生: `SFML (audio)`
- 文字列組み立て/出力最適化: `fmt`, SIMD, OpenMP

## 動作環境

- Linux（Ubuntu 想定）
- CMake 3.18+
- Ninja
- g++

## 依存パッケージ

```bash
sudo apt update
sudo apt install -y build-essential cmake ninja-build libopencv-dev ffmpeg libsfml-dev libfmt-dev
```

## クイックスタート

1. プロジェクト直下に再生したい動画ファイル（例: `sen.mp4`）を置く
2. [main.cpp](main.cpp) の `FILENAME` を動画名に合わせる
3. ビルドする
4. 実行する

### ビルド

Release（デフォルト）:

```bash
sh cmake.sh
```

Debug:

```bash
sh cmake.sh Debug
```

### 実行

```bash
sh run.sh
```

### ログ監視（任意）

```bash
sh log.sh
```

## 設定項目

主な調整パラメータは [main.cpp](main.cpp#L20-L29) です。

- `volume`: 音量
- `speed`: 再生速度倍率
- `sleep_value`: 事前待機制御（現状はほぼ未使用）
- `FILENAME`: 再生する動画ファイル名
- `FONT_CORRECTION`: 端末フォント縦横比補正
- `MAX_RENDER_WIDTH`: 描画幅の上限
- `FRAME_RING_CAPACITY`: フレームリングバッファ容量
- `is_debug`: デバッグログ制御

## 仕組み（概要）

1. `ffmpeg` で動画から音声を `output.wav` に抽出
2. OpenCV で動画を読み込み、端末サイズに合わせてリサイズ
3. 各フレームをハーフブロック+24bit ANSI カラー列に変換
4. SPSC リングバッファで生産者/消費者スレッド間を受け渡し
5. 音声再生と同期しながらターミナルへ描画

## 生成物

- `build/Bad-Apple`: 実行ファイル
- `output.wav`: 抽出された音声
- `output.txt`: 実行ログ

## トラブルシューティング

- `Error: Could not open file` が出る
	- `FILENAME` と動画ファイルの配置場所を確認
- 音が出ない
	- `ffmpeg` と `libsfml-dev` の導入状態を確認
- 文字が崩れる / 重い
	- ターミナルを広げる、`MAX_RENDER_WIDTH` を下げる
- ビルド失敗
	- [cmake.sh](cmake.sh) 実行後に `build/build.log` を確認

## 詳細ドキュメント

- 実装の詳細説明: [docs/DETAILS.md](docs/DETAILS.md)
