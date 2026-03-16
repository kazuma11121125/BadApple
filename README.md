# Bad Apple ASCII Art Player

このプロジェクトは、有名な「Bad Apple!!」の動画をASCIIアート（正確にはブロック要素文字）に変換してターミナル上で再生するC++アプリケーションです。

## 機能

- 動画ファイルをリアルタイムでASCIIアート風の文字に変換
- OpenMPによる並列処理で高速な画像処理
- SFMLを使用した音声再生との同期
- フレームレートの調整とパフォーマンスログの出力

## 必要な環境

### 依存ライブラリ

- OpenCV (画像・動画処理)
- SFML (音声再生)
- OpenMP (並列処理)
- FFmpeg (音声抽出用、システムにインストール済みであること)

### ビルドツール

- CMake 3.18以上
- Ninja
- g++コンパイラ

## セットアップ

### Ubuntu環境での完全な環境構築

#### 1. システムパッケージの更新
```bash
sudo apt update
sudo apt upgrade -y
```

#### 2. ビルドツール、開発ツール、全ライブラリのインストール
```bash
sudo apt install -y build-essential cmake ninja-build git libopencv-dev libsfml-dev libomp-dev libfmt-dev ffmpeg pkg-config
```

#### 3. リポジトリをクローン
```bash
git clone git@github.com:kazuma11121125/BadApple.git
cd BadApple
```

#### 4. 動画ファイルを準備
`bad_apple.mp4` という名前の動画ファイルをプロジェクトのルートディレクトリに配置してください。

## ビルド方法

ビルドスクリプトを実行:

```bash
sh cmake.sh
```

このスクリプトは以下の処理を自動で行います:
- CMakeによるプロジェクト設定
- Ninjaを使用したビルド

## 実行方法

ビルド後、以下のコマンドで実行:

```bash
sh run.sh
```

## ログの確認

別のターミナルで以下のコマンドを実行すると、リアルタイムでパフォーマンスログを確認できます:

```bash
sh log.sh
```

## 設定のカスタマイズ

`main.cpp`の冒頭部分で以下のパラメータを調整できます:

- `volume`: 音量 (0.0〜100.0)
- `speed`: 再生速度
- `HEIGHT`: ASCII出力の高さ（ピクセル数）
- `fps_value`: フレームスキップの値（1 = スキップなし）
- `FILENAME`: 入力動画ファイル名
- `sleep_value`: フレーム確保時間 (-1 = 無効)

## 最適化オプション

`CMakeLists.txt`では以下の最適化オプションが有効になっています:

- `-O3`: 最高レベルの最適化
- `-march=native`: CPUネイティブ命令の使用
- `-ffast-math`: 高速な数学演算
- `-flto`: リンク時最適化
- `-fopenmp`: OpenMPによる並列化

## プロジェクト構造

- `main.cpp`: メインプログラム
- `CMakeLists.txt`: CMake設定ファイル
- `cmake.sh`: ビルドスクリプト
- `run.sh`: 実行スクリプト
- `log.sh`: ログ表示スクリプト

## 技術詳細

### 画像処理の流れ

1. **`resize`関数**: 動画フレームを指定の高さにリサイズ（アスペクト比維持）
2. **`grayscalify`関数**: グレースケール化とコントラスト調整
3. **`modify`関数**: ピクセル値をASCII文字に変換（OpenMPで並列化）

### マルチスレッド処理

- **音声抽出スレッド**: FFmpegで動画から音声を抽出
- **フレーム処理スレッド**: 動画フレームをASCIIアートに変換
- **表示スレッド**: 音声と同期してASCIIアートを表示

## ライセンス

このプロジェクトの利用については、各依存ライブラリのライセンスに従ってください。
