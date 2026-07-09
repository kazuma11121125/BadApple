# BadApple 詳細設計メモ (v4: 共有メモリ直結方式)

この文書は、[main.cpp](../main.cpp) の実装を「なぜその構成にしているか」まで含めて説明するための詳細資料です。

> v3.2 までは ANSI エスケープシーケンスを生成して PTY 経由でターミナルに書き込む方式でした。
> v4 でこれを全廃し、[speed_terminal](../../speed_terminal) の共有メモリ (`SharedBuffer.hpp`) に
> `ScreenCell` を直接書き込む方式に置き換えています。旧方式の設計意図は末尾の「9. v3.2までの旧方式」に残しています。

## 1. 全体アーキテクチャ

処理は大きく 3 系統で並行に進みます。

1. 音声/MJPEG 前処理（起動時、並列2スレッド）
   - `ffmpeg` で動画から `output.wav` を抽出
   - `ffmpeg` で動画を高速デコード用に MJPEG (`se_mjpeg.avi`) へトランスコード
2. decode スレッド（producer）
   - OpenCV で動画を読み込み、グリッドサイズにリサイズ
   - 各ピクセルを `ScreenCell`（fg/bg RGBA + codepoint）へ直接変換してリングバッファに投入
   - ANSI文字列や差分計算は一切行わない
3. publish スレッド（consumer）
   - リングバッファから取り出し
   - `SFML::Music` の再生時刻を基準に、公開すべきタイミングで
     共有メモリのトリプルバッファへ `memcpy` して `latest_buffer` を切り替える

この構成により、「重い画像変換」と「厳密な表示タイミング」を分離しています。

## 2. なぜ共有メモリ直結にしたか

v3.2 までの ANSI/PTY 方式では、実測で publish（旧: display）スレッドの平均処理時間が
33ms 予算に対して約32ms、半数以上のフレームが予算超過していました。これは

- ANSI エスケープシーケンスの文字列組み立て
- PTY 経由での `write()`（カーネルコピー + 受信側ターミナルのパース速度に律速）

が支配的コストだったためです。生成側（decode）は平均7.7msと余裕があり、ボトルネックは常に
「テキスト化→PTY→再パース」の経路にありました。

v4 では `speed_terminal` 側にすでに実装済みだった `--shared` モード（`SharedBuffer.hpp` 経由の
トリプルバッファ）に直接 `ScreenCell` を書き込むことで、この経路を丸ごと除去しました。
publish 側の平均処理時間は数msまで低下し、予算超過はほぼゼロになっています
（`build/Bad-Apple` 実行時の `output.txt` ログで確認可能）。

## 3. `CellFrameRing` の詳細

該当実装: [main.cpp](../main.cpp)

`SpscFrameRing`（旧: `std::vector<std::string>` 保持）を `ScreenCell` 配列版に置き換えたものです。
Single Producer / Single Consumer 専用の固定長リングバッファである点は変わりません。

- `buf_`: `std::vector<std::vector<ScreenCell>>` でフレーム（フラット配列, row-major）を保持
- `try_push()` / `try_pop()`: `swap()` でコピーを避けて移動
- `head_`/`tail_`: `alignas(64)` でキャッシュライン競合を軽減、`release/acquire` でロックレスに成立

### 3.1 サイズ自己修復に関する注意（重要な既知の罠）

producer 側の `output` と consumer 側の `cur_frame`/`dropped_frame` はリング経由で
中身を swap し合うため、**producer の `output` が consumer 側の空ベクタと入れ替わって
サイズ0に戻ることがあります**。これを放置すると `modifyCells()` がヌルポインタ相当の
領域へ書き込みクラッシュします。対策として `modifyCells()` の先頭で毎回サイズを
チェック・修復しています（v3.2 の `modify()` が `output.resize()` で自己修復していたのと同じ理由）。

## 4. グリッドサイズの決定

該当: [main.cpp](../main.cpp)

物理端末が存在しないため `ioctl(TIOCGWINSZ)` は使わず、`GRID_WIDTH`（品質ノブ）と
動画アスペクト比から算出します。

```
out_rows = round(GRID_WIDTH / (video_aspect * FONT_CORRECTION))
target_w = GRID_WIDTH
target_h = out_rows * 2   // ハーフブロックなので1グリッド行=2ピクセル行
```

`speed_terminal` の viewer（`--shared`）は表示アスペクト比を
`width / (height * g_font_correction)` として計算するため、`FONT_CORRECTION` の値を
両プロジェクトで一致させる必要があります（デフォルト共に `2.76`）。

## 5. `ScreenCell` への変換

該当: [main.cpp](../main.cpp), [SharedBuffer.hpp](../SharedBuffer.hpp)

- `codepoint` は常に `223`（VGA フォントの上半分ブロック文字に対応する CP437 インデックス）固定
  - viewer 側シェーダーが `codepoint == 223` を特別扱いし、上半分=前景色・下半分=背景色として描画する
  - 上下ピクセルが同色でも均一な色として自然に描画されるため、v3.2 にあった
    「上下同色ならスペース+背景色のみ」という分岐は不要になった
- `fg_rgba`/`bg_rgba` はそれぞれ元画像の上段/下段ピクセルの RGB をそのまま
  （量子化なし）パック。ANSI方式で行っていた5刻み量子化は、エスケープシーケンスの
  種類を減らして転送量を圧縮するためのものだったが、バイナリ直書きでは不要なため廃止し、
  むしろ色精度が上がっている

### 5.1 なぜ行並列化 (OpenMP) をやめたか

v3.2 の `modify()` は `#pragma omp parallel for` で行を並列変換していましたが、
v4 で計測したところ **12スレッド並列で6.0ms、シングルスレッドで2.9ms** と、
並列化する方が遅いという結果になりました。理由は次の2点です。

1. セルあたりの処理が軽すぎて、スレッド起動/結合コストが並列化の利益を上回る
2. decode スレッド（`vidObj.read()`）や publish スレッドと同じ論理コアを奪い合い、
   結果として全体のスループットが落ちる（CPU オーバーサブスクリプション）

そのため `modifyCells()` はあえてシングルスレッドのままにしています。

## 6. 音声同期

該当: [main.cpp](../main.cpp)

publish 側は `SFML::Music` 開始時刻との差から「今公開すべきフレーム番号」を推定し、
遅延時には古いフレームをリングから読み捨てて追従します。

$$
\text{expectedFrame} = \lfloor \text{elapsedSeconds} \times \text{fps} \rfloor
$$

この仕組み自体は v3.2 から変更していません（対象が「ターミナルへの描画」から
「共有メモリへの公開」に変わっただけ）。

## 7. 主要チューニング項目

- `speed`: 再生速度。`fps` と音声ピッチ両方に影響
- `FONT_CORRECTION`: `speed_terminal` 側の値と必ず一致させる
- `GRID_WIDTH`: 高すぎると共有メモリサイズ・GPU側の転送量が増える
- `FRAME_RING_CAPACITY`: decode の先読み用。publish 側が十分速いため v3.2 (512) より
  大幅に小さい値 (64) で足りる。大きくしすぎると起動直後のメモリ確保コストが増える

## 8. 制約と注意点

- `speed_terminal` の viewer が `--shared` で先にどこかのタイミングで接続してくる前提
  （writer は接続を待たずに音声再生を開始するため、viewer 起動が遅れると頭出しがずれる）
- `ffmpeg` コマンド実行は `system()` 依存
- `SharedBuffer.hpp` は `speed_terminal` 側と構造体レイアウトを完全に一致させる必要がある
  （どちらか一方だけ変更すると即クラッシュする）

## 9. v3.2までの旧方式（ANSI/PTY方式）の記録

- ANSI 24bit truecolor エスケープシーケンスを行単位で差分生成し、PTY 経由でターミナルへ `writev`
- `IntToStr` LUT で `snprintf` を排除
- 5刻み量子化でエスケープシーケンスの種類を削減
- 差分描画（前フレームとの行比較）で再送量を削減

いずれも「テキストプロトコルとPTYを使う」という前提の中での最適化であり、
v4 ではプロトコルそのものをバイナリ直結にすることでこれらの最適化ごと不要にしています。

## 10. 改善候補

1. `ffmpeg` 実行失敗時のハンドリング強化
2. decode 側のマルチスレッド化（MJPEGは各フレーム独立デコード可能なため、
   `libjpeg-turbo` 等で複数フレームを並列デコードする余地がある）
3. `CMakeLists.txt` の重複設定整理
4. writer/viewer 起動順序を自動化するラッパースクリプト
