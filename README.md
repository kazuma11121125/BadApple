# BadApple

動画を [speed_terminal](../speed_terminal)（自作 GPU ターミナル）に直接流し込んで再生する C++ プロジェクトです。

**v4 で ANSI エスケープシーケンス方式を全廃し、共有メモリ直結方式に変更しました。**
文字列組み立て・PTY・端末側パースを一切通さず、`speed_terminal` の `SharedBuffer`
(`/speed_terminal_shm`) に `ScreenCell`（前景色/背景色/文字コード）をバイナリで直接書き込みます。
GPU 側は書き込まれたセルをテクスチャバッファ経由でそのまま合成描画します。

- 動画フレーム処理: `OpenCV`
- 音声再生: `SFML (audio)`
- 端末描画: 使わない（`speed_terminal` の GPU レンダラーに直結）

## 動作環境

- Linux（Ubuntu 想定）
- CMake 3.18+ / Ninja / g++
- [speed_terminal](../speed_terminal) がビルド済みであること（`viewer` バイナリ）

## 依存パッケージ

```bash
sudo apt update
sudo apt install -y build-essential cmake ninja-build libopencv-dev ffmpeg libsfml-dev libfmt-dev
```

## クイックスタート

1. プロジェクト直下に再生したい動画ファイル（例: `se.mp4`）を置く
2. [main.cpp](main.cpp) の `FILENAME` を動画名に合わせる
3. `speed_terminal` を先にビルドしておく（`../speed_terminal/build/viewer`）
4. ビルドする
5. 実行する（**2プロセス構成**）

### ビルド

```bash
sh cmake.sh        # Release
sh cmake.sh Debug   # Debug
```

### 実行

1. まず writer（本プロジェクト）を起動し、共有メモリを作成する

   ```bash
   sh run.sh
   ```

   起動すると `[INFO] Shared memory ready: /speed_terminal_shm ... Waiting for viewer` と表示され、
   viewer の接続を待つ。

2. 別ターミナルで viewer（speed_terminal）を `--shared` モードで起動する

   ```bash
   cd ../speed_terminal/build
   ./viewer --shared --vsync 0
   ```

   接続すると自動的に再生が始まる（writer 側は viewer 接続を待たずに音声・フレーム生成を始めるため、
   viewer 起動が遅れるとその分頭出しがずれる点に注意）。

### ログ監視（任意）

```bash
sh log.sh
```

## 設定項目

主な調整パラメータは [main.cpp](main.cpp) 冒頭の定数です。

- `volume`: 音量
- `speed`: 再生速度倍率
- `sleep_value`: 事前待機制御（現状はほぼ未使用）
- `FILENAME` / `FILENAME_MJPEG`: 再生する動画ファイル名とMJPEGキャッシュ名
- `FONT_CORRECTION`: `speed_terminal` 側 `g_font_correction`（デフォルト2.76）と必ず一致させること。ズレるとアスペクト比が崩れる
- `GRID_WIDTH`: 出力グリッド幅（品質ノブ）。大きいほど高精細だが共有メモリサイズと転送量が増える
- `FRAME_RING_CAPACITY`: デコード先読み用リングバッファ容量（フレーム単位）
- `is_debug`: デバッグログ制御

## 仕組み（概要）

1. `ffmpeg` で動画から音声を `output.wav` に、映像を高速デコード用に MJPEG (`se_mjpeg.avi`) に変換
2. 動画のアスペクト比と `GRID_WIDTH`/`FONT_CORRECTION` から出力グリッドサイズを算出し、
   `/speed_terminal_shm` にトリプルバッファ共有メモリを作成
3. decode スレッドが OpenCV でフレームを読み込み、ハーフブロック用に `ScreenCell` 配列へ直接変換
   （ANSI文字列は一切生成しない）してリングバッファに投入
4. publish スレッドが `SFML::Music` の再生時刻を基準に、適切なタイミングでフレームを
   共有メモリのトリプルバッファへ `memcpy` して公開（差分計算・エスケープ生成・write syscall なし）
5. `speed_terminal` の viewer が共有メモリを直接読み、GPU (OpenGL) でテクスチャバッファとして合成描画

## 生成物

- `build/Bad-Apple`: 実行ファイル（writer）
- `output.wav`: 抽出された音声
- `output.txt`: 実行ログ
- `/dev/shm/speed_terminal_shm`: 共有メモリ（プロセス終了時に自動 unlink）

## トラブルシューティング

- `Error: Could not open file` が出る
	- `FILENAME` と動画ファイルの配置場所を確認
- 音が出ない
	- `ffmpeg` と `libsfml-dev` の導入状態を確認
- viewer 側で `shm_open failed. Is the writer running?` と出る
	- writer（本プロジェクト）を先に起動しておく必要がある
- 映像のアスペクト比が崩れる
	- `main.cpp` の `FONT_CORRECTION` と `speed_terminal` の `--font-correction`（未指定ならデフォルト2.76）が一致しているか確認
- 重い/コマ落ちする
	- `output.txt` の `process_frame`/`publish_frame` ログで decode 側・publish 側どちらが律速か確認。`GRID_WIDTH` を下げる
- ビルド失敗
	- [cmake.sh](cmake.sh) 実行後に `build/build.log` を確認

## 詳細ドキュメント

- 実装の詳細説明: [docs/DETAILS.md](docs/DETAILS.md)
