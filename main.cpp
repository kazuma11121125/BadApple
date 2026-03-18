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

constexpr float volume = 30.0f;
constexpr float speed = 1.0f;
constexpr float sleep_value = 2;//待機時間
const std::string FILENAME = "bad_apple_120.mp4"; // 動画ファイル名
constexpr float FONT_CORRECTION = 2.76f; // フォントアスペクト比補正
constexpr int MAX_RENDER_WIDTH = 2000;   // 描画幅上限（ターミナル描画速度の限界）
constexpr size_t FRAME_RING_CAPACITY = 20000; // SPSCリングバッファ容量

const bool is_debug = true; // デバッグモード

class SpscFrameRing {
public:
    explicit SpscFrameRing(size_t capacity)
        : buf_(capacity), cap_(capacity) {}

    void reserve_all(size_t bytes) {
        for (auto& s : buf_) {
            s.reserve(bytes);
        }
    }

    bool try_push(std::string& item) {
        const size_t head = head_.load(std::memory_order_relaxed);
        const size_t next = inc(head);
        if (next == tail_.load(std::memory_order_acquire)) {
            return false; // full
        }
        buf_[head].swap(item);
        head_.store(next, std::memory_order_release);
        return true;
    }

    bool try_pop(std::string& out) {
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

    std::vector<std::string> buf_;
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

inline cv::Mat resize(const cv::Mat& image, int new_height, int new_width) {
    cv::Mat resized_image;
    resized_image.create(new_height, new_width, image.type());
    cv::resize(image, resized_image, resized_image.size(), 0, 0, cv::INTER_LINEAR);
    return resized_image;
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
    out.append("\033[", 2);
    if (row1based >= 0 && row1based <= 255) {
        out.append(INT_STR.data[row1based], INT_STR.len[row1based]);
    } else {
        out.append(std::to_string(row1based));
    }
    out.append(";1H", 3);
}

inline void splitFrameRows(const std::string& frame, std::vector<std::string_view>& rows) {
    rows.clear();
    if (frame.empty()) return;

    size_t start = 0;
    size_t end = frame.size();

    if (frame.size() >= 3 && frame.compare(0, 3, "\033[H") == 0) {
        start = 3;
    }
    if (end >= 4 && frame.compare(end - 4, 4, "\033[0m") == 0) {
        end -= 4;
    }
    if (start >= end) return;

    size_t line_start = start;
    for (size_t i = start; i < end; ++i) {
        if (frame[i] == '\n') {
            rows.emplace_back(frame.data() + line_start, i - line_start);
            line_start = i + 1;
        }
    }
    if (line_start <= end) {
        rows.emplace_back(frame.data() + line_start, end - line_start);
    }
}

// エスケープシーケンスを高速に組み立てる（snprintf不使用）
inline void appendFgBg(std::string& line, int fg_r, int fg_g, int fg_b, int bg_r, int bg_g, int bg_b) {
    line.append("\033[38;2;", 7);
    line.append(INT_STR.data[fg_r], INT_STR.len[fg_r]); line.push_back(';');
    line.append(INT_STR.data[fg_g], INT_STR.len[fg_g]); line.push_back(';');
    line.append(INT_STR.data[fg_b], INT_STR.len[fg_b]);
    line.append(";48;2;", 6);
    line.append(INT_STR.data[bg_r], INT_STR.len[bg_r]); line.push_back(';');
    line.append(INT_STR.data[bg_g], INT_STR.len[bg_g]); line.push_back(';');
    line.append(INT_STR.data[bg_b], INT_STR.len[bg_b]); line.push_back('m');
}

inline void appendFg(std::string& line, int r, int g, int b) {
    line.append("\033[38;2;", 7);
    line.append(INT_STR.data[r], INT_STR.len[r]); line.push_back(';');
    line.append(INT_STR.data[g], INT_STR.len[g]); line.push_back(';');
    line.append(INT_STR.data[b], INT_STR.len[b]); line.push_back('m');
}

inline void appendBg(std::string& line, int r, int g, int b) {
    line.append("\033[48;2;", 7);
    line.append(INT_STR.data[r], INT_STR.len[r]); line.push_back(';');
    line.append(INT_STR.data[g], INT_STR.len[g]); line.push_back(';');
    line.append(INT_STR.data[b], INT_STR.len[b]); line.push_back('m');
}

// ハーフブロック方式: 1セルで縦2ピクセルを表現
// ▀ (U+2580) の前景色=上ピクセル、背景色=下ピクセル
static const char HALF_BLOCK[] = "\xe2\x96\x80"; // UTF-8 for ▀ (3 bytes)

void processHalfBlockRow(const cv::Mat& image, int out_row, int total_out_rows,
                         std::vector<std::string>& output) {
    const int src_row_top = out_row * 2;
    const int src_row_bot = src_row_top + 1;
    const bool has_bot = src_row_bot < image.rows;
    
    const cv::Vec3b* top_ptr = image.ptr<cv::Vec3b>(src_row_top);
    const cv::Vec3b* bot_ptr = has_bot ? image.ptr<cv::Vec3b>(src_row_bot) : nullptr;

    const uint8_t* top_raw = reinterpret_cast<const uint8_t*>(top_ptr);
    const uint8_t* bot_raw = has_bot ? reinterpret_cast<const uint8_t*>(bot_ptr) : nullptr;
    const size_t row_bytes = static_cast<size_t>(image.cols) * 3;

    thread_local std::vector<uint8_t> top_q;
    thread_local std::vector<uint8_t> bot_q;
    top_q.resize(row_bytes);
    quantize_row_floor5_simd(top_raw, top_q.data(), row_bytes);

    if (has_bot) {
        bot_q.resize(row_bytes);
        quantize_row_floor5_simd(bot_raw, bot_q.data(), row_bytes);
    }
    
    std::string line;
    line.reserve(static_cast<size_t>(image.cols) * 20);
    
    int prev_fg_r = -1, prev_fg_g = -1, prev_fg_b = -1;
    int prev_bg_r = -1, prev_bg_g = -1, prev_bg_b = -1;
    
    for (int j = 0; j < image.cols; ++j) {
        const int idx = j * 3;
        int fg_b = top_q[idx + 0];
        int fg_g = top_q[idx + 1];
        int fg_r = top_q[idx + 2];
        
        int bg_r, bg_g, bg_b;
        if (has_bot) {
            bg_b = bot_q[idx + 0];
            bg_g = bot_q[idx + 1];
            bg_r = bot_q[idx + 2];
        } else {
            bg_r = 0; bg_g = 0; bg_b = 0;
        }
        
        // 上下同色の場合: スペース+背景色のみ（前景色不要）
        if (fg_r == bg_r && fg_g == bg_g && fg_b == bg_b) {
            if (bg_r != prev_bg_r || bg_g != prev_bg_g || bg_b != prev_bg_b) {
                appendBg(line, bg_r, bg_g, bg_b);
                prev_bg_r = bg_r; prev_bg_g = bg_g; prev_bg_b = bg_b;
            }
            line.push_back(' ');
            prev_fg_r = -1; prev_fg_g = -1; prev_fg_b = -1;
            continue;
        }
        
        bool fg_changed = (fg_r != prev_fg_r || fg_g != prev_fg_g || fg_b != prev_fg_b);
        bool bg_changed = (bg_r != prev_bg_r || bg_g != prev_bg_g || bg_b != prev_bg_b);
        
        if (fg_changed && bg_changed) {
            appendFgBg(line, fg_r, fg_g, fg_b, bg_r, bg_g, bg_b);
            prev_fg_r = fg_r; prev_fg_g = fg_g; prev_fg_b = fg_b;
            prev_bg_r = bg_r; prev_bg_g = bg_g; prev_bg_b = bg_b;
        } else if (fg_changed) {
            appendFg(line, fg_r, fg_g, fg_b);
            prev_fg_r = fg_r; prev_fg_g = fg_g; prev_fg_b = fg_b;
        } else if (bg_changed) {
            appendBg(line, bg_r, bg_g, bg_b);
            prev_bg_r = bg_r; prev_bg_g = bg_g; prev_bg_b = bg_b;
        }
        
        line.append(HALF_BLOCK, 3);
    }
    
    if (out_row < total_out_rows - 1) {
        line.push_back('\n');
    }
    output[out_row] = std::move(line);
}

void modify(const cv::Mat& image, std::string& result) {
    const int out_rows = (image.rows + 1) / 2;
    std::vector<std::string> output(out_rows);
    
    #pragma omp parallel for
    for (int i = 0; i < out_rows; ++i) {
        processHalfBlockRow(std::cref(image), i, out_rows, std::ref(output));
    }
    
    size_t total = 3; // \033[H
    for (const auto& line : output) total += line.size();
    total += 4; // \033[0m

    result.clear();
    result.reserve(total);
    result.append("\033[H");
    for (const auto& line : output) result.append(line);
    result.append("\033[0m");
}

int main() {
    std::string commands = fmt::format("ffmpeg -y -i '{}' -vn output.wav", FILENAME);
    std::thread t([&commands](){
        system(commands.c_str());
    });
    cv::VideoCapture vidObj(FILENAME);
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
    // （一部端末で最終カラム描画時にアーティファクトが出ることがある）
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
    
    SpscFrameRing frame_ring(FRAME_RING_CAPACITY);
    const size_t estimated_frame_bytes = static_cast<size_t>(target_w) * static_cast<size_t>((target_h + 1) / 2) * 14 + 16;
    frame_ring.reserve_all(estimated_frame_bytes);
    std::atomic<bool> producer_done{false};
    int frame_count = static_cast<int>(vidObj.get(cv::CAP_PROP_FRAME_COUNT));
    cv::Mat image;
    FILE *fp;
    fp = fopen("output.txt", "w");
    float fps = vidObj.get(cv::CAP_PROP_FPS) * speed;
    
    fmt::print(fp, "[CONFIG] term={}x{} render={}x{} display_rows={} fps={:.1f}\n",
               term_w, term_h + 1, target_w, target_h, (target_h + 1) / 2, fps);
    
    std::thread cv_thred([&frame_count, &frame_ring, &vidObj, &image, &producer_done, &fp, fps, target_h, target_w](){
        const double sleep = 1.0 / (fps * 1.25);
        const int pass_time_count = 100;
        std::string frame;
        frame.reserve(static_cast<size_t>(target_w) * static_cast<size_t>((target_h + 1) / 2) * 14 + 16);
        for (size_t i = 0; i < frame_count; ++i) {
            auto start_time = std::chrono::high_resolution_clock::now();
            if (!vidObj.read(image)) break;
            cv::Mat resized_image = resize(image, target_h, target_w);
            modify(resized_image, frame);
            if (!frame.empty()) {
                while (!frame_ring.try_push(frame)) {
                    std::this_thread::sleep_for(std::chrono::microseconds(200));
                }
            }
            auto end_time = std::chrono::high_resolution_clock::now();
            std::chrono::duration<double> elapsed_time = end_time - start_time;
            double sleep_time = sleep - elapsed_time.count();
            if (sleep_time > 0 && i > pass_time_count) {
                std::this_thread::sleep_for(std::chrono::duration<double>(sleep_time));
                if (is_debug){
                    fmt::print(fp, "[INFO] process_frame = {}, elapsed_time = {:.6f}, sleep_time = {:.6f}\n", i, elapsed_time.count(), sleep_time);
                }
            } else {
                if(i > pass_time_count){
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

    t.join();
    if (sleep_value > 0) {
        const size_t prebuffer = static_cast<size_t>(frame_count / sleep_value);
        while (frame_ring.size_approx() < prebuffer && !producer_done.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    // カーソル非表示 & 画面クリア & スクロール防止
    system("clear");
    int display_rows = (target_h + 1) / 2;
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
    
    std::thread display_thread([&frame_ring, &producer_done, fps, frame_count, display_rows, estimated_frame_bytes, &fp, &start_time]() {
        const int max_frame = frame_count - 2;
        double sleep = 1.0 / fps;
        size_t displayed = 0;
        std::string frame;
        std::string dropped;
        std::vector<std::string> prev_rows(static_cast<size_t>(display_rows));
        std::vector<std::string_view> cur_rows;
        cur_rows.reserve(static_cast<size_t>(display_rows));
        std::string patch;
        patch.reserve(estimated_frame_bytes / 2);
        while (displayed < static_cast<size_t>(max_frame)) {
            auto frame_start_time = std::chrono::high_resolution_clock::now();
            auto current_time = std::chrono::high_resolution_clock::now();
            std::chrono::duration<double> elapsed_time = current_time - start_time;
            const size_t expected_frame_index = static_cast<size_t>(std::max(0, static_cast<int>(elapsed_time.count() * fps)));

            size_t queue_depth_after_pop = 0;
            while (displayed < expected_frame_index && frame_ring.size_approx() > 1) {
                if (!frame_ring.try_pop(dropped)) {
                    break;
                }
                ++displayed;
            }

            while (!frame_ring.try_pop(frame)) {
                if (producer_done.load(std::memory_order_acquire)) {
                    break;
                }
                std::this_thread::sleep_for(std::chrono::microseconds(200));
            }

            if (frame.empty() && producer_done.load(std::memory_order_acquire)) {
                break;
            }

            queue_depth_after_pop = frame_ring.size_approx();

            if (!frame.empty()) {
                splitFrameRows(frame, cur_rows);
                patch.clear();

                const size_t render_rows = static_cast<size_t>(display_rows);
                const size_t n = std::min(render_rows, cur_rows.size());
                for (size_t r = 0; r < n; ++r) {
                    const std::string_view cur = cur_rows[r];
                    const std::string& prev = prev_rows[r];
                    const bool changed =
                        (cur.size() != prev.size()) ||
                        (cur.size() > 0 && std::memcmp(cur.data(), prev.data(), cur.size()) != 0);

                    if (changed) {
                        appendCursorMove(patch, static_cast<int>(r + 1));
                        patch.append(cur.data(), cur.size());
                        // 行末の残像・背景色リーク防止のため毎回EOLまで消去
                        patch.append("\033[0m\033[K", 7);
                        prev_rows[r].assign(cur.data(), cur.size());
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
                    write(STDOUT_FILENO, patch.c_str(), patch.size());
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
                fmt::print(fp, "[WARNING] display_frame = {}, processing_time = {:.6f}, sleep_time = {:.6f}, queue_depth = {}, frame_size = {}\n", displayed, processing_time.count(), sleep_time, queue_depth_after_pop, frame.size());
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