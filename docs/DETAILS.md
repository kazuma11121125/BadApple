# BadApple 詳細設計メモ

この文書は、[main.cpp](../main.cpp) の実装を「なぜその構成にしているか」まで含めて説明するための詳細資料です。

## 1. 全体アーキテクチャ

処理は大きく 3 系統で並行に進みます。

1. 音声抽出スレッド
   - `ffmpeg` で動画から `output.wav` を生成
2. フレーム生成スレッド（producer）
   - OpenCV で動画を読み込み
   - 端末サイズに合わせてリサイズ
   - ANSIカラー文字列へ変換してリングバッファに投入
3. 描画スレッド（consumer）
   - リングバッファから取り出し
   - 差分描画で端末へ反映
   - `SFML::Music` の再生時刻を基準に描画フレームを調整

この構成により、「重い画像変換」と「厳密な表示タイミング」を分離しています。

## 2. `SpscFrameRing` の詳細

該当実装: [main.cpp](../main.cpp#L32-L79)

`SpscFrameRing` は Single Producer / Single Consumer 専用の固定長リングバッファです。

- `buf_`
  - `std::vector<std::string>` でフレーム文字列を保持
- `head_`
  - producer 側の書き込み位置
- `tail_`
  - consumer 側の読み取り位置
- `alignas(64)`
  - キャッシュライン競合（false sharing）を軽減

### 2.1 `try_push()`

- `next == tail` なら満杯なので `false`
- それ以外は `buf_[head].swap(item)` でコピーを避けて移動
- `head` を進める

### 2.2 `try_pop()`

- `tail == head` なら空なので `false`
- それ以外は `out.swap(buf_[tail])` で取り出し
- `tail` を進める

### 2.3 メモリオーダ

- `head_`/`tail_` の公開・観測に `release/acquire` を使用
- 単一 producer / consumer でロックレスに成立

ポイントは、SPSC 前提を守ることでロックコストを避け、フレーム搬送を高速化している点です。

## 3. 端末サイズへのフィット

該当: [main.cpp](../main.cpp#L340-L370)

- 端末サイズを取得し、最下行はスクロール防止のため使用しない
- 右端アーティファクト対策で 1 カラム余白を確保
- 動画アスペクト比と `FONT_CORRECTION` で表示サイズを決定
- `MAX_RENDER_WIDTH` を超える場合は幅基準に切替

### ハーフブロックの縦解像度

1 文字行で 2 ピクセル縦を持つため、

$$
\text{imageHeight} = 2 \times \text{terminalRows}
$$

として計算しています。

## 4. 色量子化と文字列生成

### 4.1 5刻み量子化

該当: [main.cpp](../main.cpp#L102-L136)

各 RGB 値を `floor(x/5)*5` 相当へ丸め、ANSI列の種類を減らして圧縮・高速化します。

- スカラ版: `quantize_floor5_scalar()`
- SIMD版: `quantize_row_floor5_simd()`（SSE2）

### 4.2 `IntToStr` LUT

該当: [main.cpp](../main.cpp#L139-L161)

0〜255 の整数文字列を事前生成し、`snprintf` を排除。ANSIエスケープ構築時の CPU 負荷を減らしています。

### 4.3 ハーフブロック描画

該当: [main.cpp](../main.cpp#L236-L326)

- 文字 `▀`（上半分ブロック）を使う
- 前景色 = 上段ピクセル
- 背景色 = 下段ピクセル

上下同色のときはブロック文字を使わずスペース + 背景色にして転送量を削減します。

## 5. 差分描画

該当: [main.cpp](../main.cpp#L439-L505)

全画面を毎フレーム再描画せず、前回行との差分のみ送信します。

- `splitFrameRows()` でフレームを行ごとに分解
- 変更行だけ `appendCursorMove()` でカーソル移動して再描画
- 行末は `\033[0m\033[K` で残像/色漏れ防止

この方式で端末 I/O を大きく削減できます。

## 6. 音声同期

該当: [main.cpp](../main.cpp#L449-L468)

描画側は `SFML::Music` 開始時刻との差から「今表示すべきフレーム番号」を推定し、遅延時には古いフレームをドロップして追従します。

$$
\text{expectedFrame} = \lfloor \text{elapsedSeconds} \times \text{fps} \rfloor
$$

映像が多少間引かれても、体感の音ズレを抑える設計です。

## 7. OpenMP 並列化

該当: [main.cpp](../main.cpp#L328-L334)

`modify()` で各出力行（=2ピクセル高さブロック）を `#pragma omp parallel for` で並列変換します。

- 1 行ごとに独立なので並列化しやすい
- `thread_local` バッファを使って再確保を抑制

## 8. 主要チューニング項目

該当: [main.cpp](../main.cpp#L21-L29)

- `speed`
  - 再生速度。`fps` と音声ピッチ両方に影響
- `FONT_CORRECTION`
  - 端末フォント依存。映像の縦横比が崩れる場合に調整
- `MAX_RENDER_WIDTH`
  - 高すぎると端末描画が飽和
- `FRAME_RING_CAPACITY`
  - 小さすぎると producer 待ちが増える

## 9. 制約と注意点

- ANSI 24bit カラー対応ターミナル前提
- `ffmpeg` コマンド実行は `system()` 依存
- `sleep_value` は現在ほぼ未活用（将来のプリバッファ調整向け）
- 端末描画性能がボトルネックになりやすい

## 10. 改善候補

1. `ffmpeg` 実行失敗時のハンドリング強化
2. フレーム生成側の動的間引き（過負荷時のバックプレッシャ制御）
3. 端末能力検出（truecolor 非対応時のフォールバック）
4. `CMakeLists.txt` の重複設定整理

---

必要なら次に、`SpscFrameRing` をテンプレート化して `std::string` 以外にも使えるようにした版を追加できます。
