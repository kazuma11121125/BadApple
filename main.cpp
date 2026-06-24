#define _GNU_SOURCE
#include <iostream>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <vector>
#include <string>
#include <unistd.h>
#include <ctime>
#include <opencv2/opencv.hpp>
#include <SFML/Audio.hpp>
#include <thread>
#include <chrono>
#include <emmintrin.h>
#include <omp.h>
#include <atomic>
#include <fmt/core.h>
#include <sys/ioctl.h>
#include <sys/uio.h>
#include <fcntl.h>
#include <unistd.h>

constexpr float volume = 30.0f;
constexpr float speed = 1.0f;
constexpr float sleep_value = -1;//待機時間
const std::string FILENAME = "tadakimi.mp4"; // 動画ファイル名
const std::string FILENAME_MJPEG = "tadakimi_mjpeg.avi"; // MJPEGキャッシュファイル名
constexpr float FONT_CORRECTION = 2.76f; // フォントアスペクト比補正
constexpr int MAX_RENDER_WIDTH = 4000;   // 描画幅上限（ターミナル描画速度の限界）
constexpr size_t FRAME_RING_CAPACITY = 512; // SPSCリングバッファ容量

const bool is_debug = true; // デバッグモード

// ─── SPSC リングバッファ（vector<string> 行配列を直接転送、連結なし） ────────
class SpscFrameRing {
public:
    explicit SpscFrameRing(size_t capacity)
        : buf_(capacity), cap_(capacity) {}

    // 各スロットの各行バッファを事前確保
    void reserve_all(size_t row_count, size_t bytes_per_row) {
        for (auto& rows : buf_) {
            rows.resize(row_count);
            for (auto& s : rows) s.reserve(bytes_per_row);
        }
    }

    // item の中身をリングの空きスロットと swap（コピーなし）
    bool try_push(std::vector<std::string>& item) {
        const size_t head = head_.load(std::memory_order_relaxed);
        const size_t next = inc(head);
        if (next == tail_.load(std::memory_order_acquire)) {
            return false; // full
        }
        buf_[head].swap(item);
        head_.store(next, std::memory_order_release);
        return true;
    }

    // out とリングの先頭スロットを swap（コピーなし）
    bool try_pop(std::vector<std::string>& out) {
        const size_t tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire)) {
            return false; // empty
        }
        out.swap(buf_[tail]);
        tail_.store(inc(tail), std::memory_order_release);
        return true;
    }

    size_t size_approx() const {
        const size_t head = head_.load(std::memory_order_acquire);
        const size_t tail = tail_.load(std::memory_order_acquire);
        return (head >= tail) ? (head - tail) : (cap_ - tail + head);
    }

private:
    size_t inc(size_t idx) const {
        ++idx;
        if (idx == cap_) idx = 0;
        return idx;
    }

    std::vector<std::vector<std::string>> buf_;
    const size_t cap_;
    alignas(64) std::atomic<size_t> head_{0};
    alignas(64) std::atomic<size_t> tail_{0};
};

// ターミナルサイズを取得
int getTermHeight() {
    struct winsize w;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
    return w.ws_row > 0 ? w.ws_row : 60;
}

int getTermWidth() {
    struct winsize w;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
    return w.ws_col > 0 ? w.ws_col : 200;
}

inline void resize(const cv::Mat& src, cv::Mat& dst, int new_height, int new_width) {
    cv::resize(src, dst, cv::Size(new_width, new_height), 0, 0, cv::INTER_NEAREST);
}

inline uint8_t quantize_floor5_scalar(uint8_t x) {
    return static_cast<uint8_t>(((x * 205u) >> 10) * 5u); // floor(x/5)*5 を除算なしで計算
}

// SIMDで1行分を5刻みに量子化（SSE2）
inline void quantize_row_floor5_simd(const uint8_t* src, uint8_t* dst, size_t count) {
    size_t i = 0;

#if defined(__SSE2__)
    const __m128i zero = _mm_setzero_si128();
    const __m128i mul = _mm_set1_epi16(205);

    for (; i + 16 <= count; i += 16) {
        const __m128i x = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src + i));
        __m128i lo = _mm_unpacklo_epi8(x, zero);
        __m128i hi = _mm_unpackhi_epi8(x, zero);

        lo = _mm_srli_epi16(_mm_mullo_epi16(lo, mul), 10);
        hi = _mm_srli_epi16(_mm_mullo_epi16(hi, mul), 10);

        lo = _mm_add_epi16(_mm_slli_epi16(lo, 2), lo); // *5
        hi = _mm_add_epi16(_mm_slli_epi16(hi, 2), hi); // *5

        const __m128i packed = _mm_packus_epi16(lo, hi);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(dst + i), packed);
    }
#endif

    for (; i < count; ++i) {
        dst[i] = quantize_floor5_scalar(src[i]);
    }
}

// 0〜255の整数を文字列化したルックアップテーブル（snprintf完全排除）
struct IntToStr {
    char data[256][4]; // 最大"255" + null
    uint8_t len[256];
    constexpr IntToStr() : data{}, len{} {
        for (int i = 0; i < 256; ++i) {
            int pos = 0;
            if (i >= 100) {
                data[i][pos++] = '0' + (i / 100);
                data[i][pos++] = '0' + ((i / 10) % 10);
                data[i][pos++] = '0' + (i % 10);
            } else if (i >= 10) {
                data[i][pos++] = '0' + (i / 10);
                data[i][pos++] = '0' + (i % 10);
            } else {
                data[i][pos++] = '0' + i;
            }
            data[i][pos] = '\0';
            len[i] = pos;
        }
    }
};

static constexpr IntToStr INT_STR{};

inline void appendCursorMove(std::string& out, int row1based) {
    char buf[32];
    char* ptr = buf;
    std::memcpy(ptr, "\033[", 2);
    ptr += 2;
    if (row1based >= 0 && row1based <= 255) {
        std::memcpy(ptr, INT_STR.data[row1based], INT_STR.len[row1based]);
        ptr += INT_STR.len[row1based];
    } else {
        std::string s = std::to_string(row1based);
        std::memcpy(ptr, s.data(), s.size());
        ptr += s.size();
    }
    std::memcpy(ptr, ";1H", 3);
    ptr += 3;
    out.append(buf, ptr - buf);
}

// エスケープシーケンスを高速に組み立てる（snprintf不使用、スタックバッファによる最適化）
inline void appendFgBg(std::string& line, int fg_r, int fg_g, int fg_b, int bg_r, int bg_g, int bg_b) {
    char buf[64];
    char* ptr = buf;
    std::memcpy(ptr, "\033[38;2;", 7);
    ptr += 7;
    std::memcpy(ptr, INT_STR.data[fg_r], INT_STR.len[fg_r]);
    ptr += INT_STR.len[fg_r];
    *ptr++ = ';';
    std::memcpy(ptr, INT_STR.data[fg_g], INT_STR.len[fg_g]);
    ptr += INT_STR.len[fg_g];
    *ptr++ = ';';
    std::memcpy(ptr, INT_STR.data[fg_b], INT_STR.len[fg_b]);
    ptr += INT_STR.len[fg_b];
    std::memcpy(ptr, ";48;2;", 6);
    ptr += 6;
    std::memcpy(ptr, INT_STR.data[bg_r], INT_STR.len[bg_r]);
    ptr += INT_STR.len[bg_r];
    *ptr++ = ';';
    std::memcpy(ptr, INT_STR.data[bg_g], INT_STR.len[bg_g]);
    ptr += INT_STR.len[bg_g];
    *ptr++ = ';';
    std::memcpy(ptr, INT_STR.data[bg_b], INT_STR.len[bg_b]);
    ptr += INT_STR.len[bg_b];
    *ptr++ = 'm';
    line.append(buf, ptr - buf);
}

inline void appendFg(std::string& line, int r, int g, int b) {
    char buf[32];
    char* ptr = buf;
    std::memcpy(ptr, "\033[38;2;", 7);
    ptr += 7;
    std::memcpy(ptr, INT_STR.data[r], INT_STR.len[r]);
    ptr += INT_STR.len[r];
    *ptr++ = ';';
    std::memcpy(ptr, INT_STR.data[g], INT_STR.len[g]);
    ptr += INT_STR.len[g];
    *ptr++ = ';';
    std::memcpy(ptr, INT_STR.data[b], INT_STR.len[b]);
    ptr += INT_STR.len[b];
    *ptr++ = 'm';
    line.append(buf, ptr - buf);
}

inline void appendBg(std::string& line, int r, int g, int b) {
    char buf[32];
    char* ptr = buf;
    std::memcpy(ptr, "\033[48;2;", 7);
    ptr += 7;
    std::memcpy(ptr, INT_STR.data[r], INT_STR.len[r]);
    ptr += INT_STR.len[r];
    *ptr++ = ';';
    std::memcpy(ptr, INT_STR.data[g], INT_STR.len[g]);
    ptr += INT_STR.len[g];
    *ptr++ = ';';
    std::memcpy(ptr, INT_STR.data[b], INT_STR.len[b]);
    ptr += INT_STR.len[b];
    *ptr++ = 'm';
    line.append(buf, ptr - buf);
}

// ハーフブロック方式: 1セルで縦2ピクセルを表現
// ▀ (U+2580) の前景色=上ピクセル、背景色=下ピクセル
static const char HALF_BLOCK[] = "\xe2\x96\x80"; // UTF-8 for ▀ (3 bytes)

void processHalfBlockRow(const cv::Mat& image, int out_row, int total_out_rows,
                         std::vector<std::string>& output) {
    const int src_row_top = out_row * 2;
    const int src_row_bot = src_row_top + 1;
    const bool has_bot = src_row_bot < image.rows;
    
    const uint8_t* top_raw = image.ptr<uint8_t>(src_row_top);
    const uint8_t* bot_raw = has_bot ? image.ptr<uint8_t>(src_row_bot) : nullptr;

    std::string& line = output[out_row];
    line.clear();
    const size_t desired_capacity = static_cast<size_t>(image.cols) * 20;
    if (line.capacity() < desired_capacity) {
        line.reserve(desired_capacity);
    }
    
    uint32_t prev_fg = 0xFFFFFFFF; // 初期値（存在しない色）
    uint32_t prev_bg = 0xFFFFFFFF;
    
    for (int j = 0; j < image.cols; ++j) {
        const int idx = j * 3;
        uint32_t fg_b = quantize_floor5_scalar(top_raw[idx + 0]);
        uint32_t fg_g = quantize_floor5_scalar(top_raw[idx + 1]);
        uint32_t fg_r = quantize_floor5_scalar(top_raw[idx + 2]);
        uint32_t fg = (fg_r << 16) | (fg_g << 8) | fg_b;
        
        uint32_t bg;
        if (has_bot) {
            uint32_t bg_b = quantize_floor5_scalar(bot_raw[idx + 0]);
            uint32_t bg_g = quantize_floor5_scalar(bot_raw[idx + 1]);
            uint32_t bg_r = quantize_floor5_scalar(bot_raw[idx + 2]);
            bg = (bg_r << 16) | (bg_g << 8) | bg_b;
        } else {
            bg = 0;
        }
        
        // 上下同色の場合: スペース+背景色のみ（前景色不要）
        if (fg == bg) {
            if (bg != prev_bg) {
                appendBg(line, (bg >> 16) & 0xFF, (bg >> 8) & 0xFF, bg & 0xFF);
                prev_bg = bg;
            }
            line.push_back(' ');
            prev_fg = 0xFFFFFFFF;
            continue;
        }
        
        bool fg_changed = (fg != prev_fg);
        bool bg_changed = (bg != prev_bg);
        
        if (fg_changed && bg_changed) {
            appendFgBg(line, fg_r, fg_g, fg_b, (bg >> 16) & 0xFF, (bg >> 8) & 0xFF, bg & 0xFF);
            prev_fg = fg;
            prev_bg = bg;
        } else if (fg_changed) {
            appendFg(line, fg_r, fg_g, fg_b);
            prev_fg = fg;
        } else if (bg_changed) {
            appendBg(line, (bg >> 16) & 0xFF, (bg >> 8) & 0xFF, bg & 0xFF);
            prev_bg = bg;
        }
        
        line.append(HALF_BLOCK, 3);
    }
    // 最終行以外は改行不要（display_thread で行単位に直接処理するため）
    (void)total_out_rows;
}

// modify: 各行を output[] に直接書き込む。連結は行わない（ゼロコピー渡し）
void modify(const cv::Mat& image, std::vector<std::string>& output) {
    const int out_rows = (image.rows + 1) / 2;
    if (static_cast<int>(output.size()) != out_rows) {
        output.resize(out_rows);
    }
    
    #pragma omp parallel for schedule(static)
    for (int i = 0; i < out_rows; ++i) {
        processHalfBlockRow(image, i, out_rows, output);
    }
}

int main() {
    // stdoutがパイプの場合のみバッファ容量を増やす (TTY接続時はエラーを無視)
    fcntl(STDOUT_FILENO, F_SETPIPE_SZ, 1048576);

    // WAV抽出 と MJPEGトランスコードを並列実行
    std::thread t_wav;
    std::thread t_mjpeg;

    if (access("output.wav", F_OK) != 0) {
        std::string cmd = fmt::format("ffmpeg -y -i '{}' -vn output.wav -loglevel quiet", FILENAME);
        t_wav = std::thread([cmd]() { system(cmd.c_str()); });
        fmt::print(stderr, "[INFO] WAV extraction started...\n");
    }

    if (access(FILENAME_MJPEG.c_str(), F_OK) != 0) {
        // -q:v 3 = 高品質MJPEG（1が最高、31が最低）
        std::string cmd = fmt::format(
            "ffmpeg -y -i '{}' -vcodec mjpeg -q:v 3 -an '{}' -loglevel quiet",
            FILENAME, FILENAME_MJPEG);
        t_mjpeg = std::thread([cmd]() { system(cmd.c_str()); });
        fmt::print(stderr, "[INFO] MJPEG transcoding started (this may take a moment)...\n");
    }

    // 両方の変換が完了するまで待機
    if (t_wav.joinable())   t_wav.join();
    if (t_mjpeg.joinable()) {
        t_mjpeg.join();
        fmt::print(stderr, "[INFO] MJPEG transcoding done.\n");
    }

    // MJPEGファイルが存在する場合はそちらを使用（H264より高速デコード）
    const std::string play_file = (access(FILENAME_MJPEG.c_str(), F_OK) == 0)
                                  ? FILENAME_MJPEG : FILENAME;
    fmt::print(stderr, "[INFO] Using: {}\n", play_file);

    cv::VideoCapture vidObj(play_file);
    std::ios_base::sync_with_stdio(false);
    std::cin.tie(nullptr);
    if (!vidObj.isOpened()) {
        fmt::print(stderr, "Error: Could not open file\n");
        return -1;
    }
    
    // ターミナルサイズに合わせた解像度を計算
    const int term_h = getTermHeight() - 1;  // 最終行を使わない（スクロール防止）
    const int term_w = getTermWidth();
    
    // 右端の描画破綻を避けるため、1カラム分の安全マージンを確保
    const int safe_term_w = std::max(1, term_w - 1);
    // 描画幅上限を適用（ターミナル幅との小さい方）
    const int effective_w = std::min(safe_term_w, MAX_RENDER_WIDTH);
    
    // 動画のアスペクト比からターミナルにフィットするサイズを決定
    const float video_aspect = static_cast<float>(vidObj.get(cv::CAP_PROP_FRAME_WIDTH))
                             / static_cast<float>(vidObj.get(cv::CAP_PROP_FRAME_HEIGHT));
    
    int target_h, target_w;
    // ハーフブロック: 1ターミナル行=2ピクセル行なので画像高さはterm_h*2
    target_h = term_h * 2;
    target_w = static_cast<int>(video_aspect * term_h * FONT_CORRECTION);
    // 描画幅上限に収まらなければ幅基準に切り替え
    if (target_w > effective_w) {
        target_w = effective_w;
        int display_rows = static_cast<int>(target_w / (video_aspect * FONT_CORRECTION));
        target_h = display_rows * 2;
    }
    
    fmt::print(stderr, "Terminal: {}x{}, Render: {}x{} (display: {}x{})\n",
               term_w, term_h + 1, target_w, target_h, target_w, (target_h + 1) / 2);
    
    const int display_rows_count = (target_h + 1) / 2;
    const size_t bytes_per_row = static_cast<size_t>(target_w) * 20; // 最大見積もり

    SpscFrameRing frame_ring(FRAME_RING_CAPACITY);
    frame_ring.reserve_all(static_cast<size_t>(display_rows_count), bytes_per_row);

    std::atomic<bool> producer_done{false};
    int frame_count = static_cast<int>(vidObj.get(cv::CAP_PROP_FRAME_COUNT));
    cv::Mat image;
    FILE *fp;
    fp = fopen("output.txt", "w");
    float fps = vidObj.get(cv::CAP_PROP_FPS) * speed;
    
    fmt::print(fp, "[CONFIG] term={}x{} render={}x{} display_rows={} fps={:.1f}\n",
               term_w, term_h + 1, target_w, target_h, display_rows_count, fps);
    
    std::thread cv_thred([&frame_count, &frame_ring, &vidObj, &image, &producer_done, &fp, fps, target_h, target_w](){
        const double sleep = 1.0 / (fps * 1.25);
        const int pass_time_count = 100;
        // output はリングのスロットと swap するので、ここで行バッファを持つ
        std::vector<std::string> output;
        cv::Mat resized_image;
        for (size_t i = 0; i < static_cast<size_t>(frame_count); ++i) {
            auto start_time = std::chrono::high_resolution_clock::now();
            if (!vidObj.read(image)) break;
            resize(image, resized_image, target_h, target_w);
            // 行ベクタに直接書き込む（連結なし）
            modify(resized_image, output);
            if (!output.empty()) {
                while (!frame_ring.try_push(output)) {
                    std::this_thread::sleep_for(std::chrono::microseconds(200));
                }
                // try_push後、output はリングから戻ってきた空きスロットになる
                // （行バッファは再利用される）
            }
            auto end_time = std::chrono::high_resolution_clock::now();
            std::chrono::duration<double> elapsed_time = end_time - start_time;
            double sleep_time = sleep - elapsed_time.count();
            if (sleep_time > 0 && i > static_cast<size_t>(pass_time_count)) {
                std::this_thread::sleep_for(std::chrono::duration<double>(sleep_time));
                if (is_debug){
                    fmt::print(fp, "[INFO] process_frame = {}, elapsed_time = {:.6f}, sleep_time = {:.6f}\n", i, elapsed_time.count(), sleep_time);
                }
            } else {
                if(i > static_cast<size_t>(pass_time_count)){
                    fmt::print(fp, "[WARNING] process_frame = {}, elapsed_time = {:.6f}, sleep_time = {:.6f}\n", i, elapsed_time.count(), sleep_time);
                }
            }
        }
        vidObj.release();
        producer_done.store(true, std::memory_order_release);
        if (is_debug){
            fmt::print(fp, "end_cv2\n");
        }
    });

    // wav/mjpeg スレッドは起動直後に join 済みなので不要
    if (sleep_value > 0) {
        const size_t prebuffer = std::min<size_t>(120, static_cast<size_t>(frame_count / sleep_value));
        while (frame_ring.size_approx() < prebuffer && !producer_done.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    // カーソル非表示 & 画面クリア & スクロール防止
    system("clear");
    int display_rows = display_rows_count;
    printf("\033[?25l");              // カーソル非表示
    printf("\033[1;%dr", display_rows); // スクロール領域を表示行数に制限
    printf("\033[?7l");               // 自動折り返し無効
    fflush(stdout);
    sf::Music music;
    if (!music.openFromFile("output.wav")) {
        fmt::print(stderr, "Error loading audio file\n");
        return -1;
    }
    music.setPitch(speed);
    music.setVolume(volume);
    auto start_time = std::chrono::high_resolution_clock::now();
    music.play();
    
    std::thread display_thread([&frame_ring, &producer_done, fps, frame_count, display_rows, &fp, &start_time]() {
        const int max_frame = frame_count - 2;
        double sleep = 1.0 / fps;
        size_t displayed = 0;

        // ring から受け取る行ベクタ（swap で使い回し）
        std::vector<std::string> cur_frame;
        std::vector<std::string> dropped_frame;

        // 前フレームの行内容（差分検出用）
        std::vector<std::string> prev_rows(static_cast<size_t>(display_rows));

        std::string patch;
        patch.reserve(static_cast<size_t>(display_rows) * 20 * 1653); // 概算

        static const char SYNC_BEGIN[] = "\033[?2026h";
        static const char SYNC_END[]   = "\033[?2026l";

        while (displayed < static_cast<size_t>(max_frame)) {
            auto frame_start_time = std::chrono::high_resolution_clock::now();
            auto current_time = std::chrono::high_resolution_clock::now();
            std::chrono::duration<double> elapsed_time = current_time - start_time;
            const size_t expected_frame_index = static_cast<size_t>(std::max(0, static_cast<int>(elapsed_time.count() * fps)));

            size_t queue_depth_after_pop = 0;
            // 遅延フレームをスキップ
            while (displayed < expected_frame_index && frame_ring.size_approx() > 1) {
                if (!frame_ring.try_pop(dropped_frame)) {
                    break;
                }
                ++displayed;
            }

            // 次フレームを取得
            while (!frame_ring.try_pop(cur_frame)) {
                if (producer_done.load(std::memory_order_acquire)) {
                    break;
                }
                std::this_thread::sleep_for(std::chrono::microseconds(200));
            }

            if (cur_frame.empty() && producer_done.load(std::memory_order_acquire)) {
                break;
            }

            queue_depth_after_pop = frame_ring.size_approx();

            if (!cur_frame.empty()) {
                patch.clear();

                const size_t render_rows = static_cast<size_t>(display_rows);
                const size_t n = std::min(render_rows, cur_frame.size());

                // 行ベクタを直接使用（splitFrameRows不要）
                for (size_t r = 0; r < n; ++r) {
                    const std::string& cur = cur_frame[r];
                    std::string& prev = prev_rows[r];
                    const bool changed =
                        (cur.size() != prev.size()) ||
                        (cur.size() > 0 && std::memcmp(cur.data(), prev.data(), cur.size()) != 0);

                    if (changed) {
                        appendCursorMove(patch, static_cast<int>(r + 1));
                        patch.append(cur.data(), cur.size());
                        // 行の長さが縮んだ場合のみ EOL まで消去
                        if (cur.size() < prev.size()) {
                            patch.append("\033[0m\033[K", 7);
                        }
                        prev = cur; // string の代入（行サイズは安定しているので再確保は稀）
                    }
                }

                for (size_t r = n; r < render_rows; ++r) {
                    if (!prev_rows[r].empty()) {
                        appendCursorMove(patch, static_cast<int>(r + 1));
                        patch.append("\033[K", 3);
                        prev_rows[r].clear();
                    }
                }

                if (!patch.empty()) {
                    // writev でゼロコピー送信
                    struct iovec iov[3];
                    iov[0].iov_base = const_cast<char*>(SYNC_BEGIN);
                    iov[0].iov_len  = 8;
                    iov[1].iov_base = const_cast<char*>(patch.c_str());
                    iov[1].iov_len  = patch.size();
                    iov[2].iov_base = const_cast<char*>(SYNC_END);
                    iov[2].iov_len  = 8;
                    writev(STDOUT_FILENO, iov, 3);
                }
            } else {
                fmt::print(fp, "[WARNING] empty frame, queue_depth = {}\n", queue_depth_after_pop);
            }

            ++displayed;
            auto frame_end_time = std::chrono::high_resolution_clock::now();
            std::chrono::duration<double> processing_time = frame_end_time - frame_start_time;
            double sleep_time = sleep - processing_time.count();
            if (sleep_time > 0) {
                if (is_debug){
                    fmt::print(fp, "[INFO] display_frame = {}, processing_time = {:.6f}, sleep_time = {:.6f}, queue_depth = {}\n", displayed, processing_time.count(), sleep_time, queue_depth_after_pop);
                }
                std::this_thread::sleep_for(std::chrono::duration<double>(sleep_time));
            } else {
                fmt::print(fp, "[WARNING] display_frame = {}, processing_time = {:.6f}, sleep_time = {:.6f}, queue_depth = {}\n", displayed, processing_time.count(), sleep_time, queue_depth_after_pop);
            }
        }
    });

    display_thread.join();
    cv_thred.join();
    music.stop();
    printf("\033[?25h");  // カーソル再表示
    printf("\033[r");     // スクロール領域リセット
    printf("\033[?7h");   // 自動折り返し復帰
    printf("\033[0m");    // 色リセット
    fflush(stdout);
    system("clear");
    fmt::print("end_display\n");
    fmt::print(fp, "end\n");
    fclose(fp);
    return 0;
}