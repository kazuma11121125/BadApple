#define _GNU_SOURCE
#include <iostream>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <vector>
#include <string>
#include <unistd.h>
#include <ctime>
#include <opencv2/opencv.hpp>
#include <SFML/Audio.hpp>
#include <thread>
#include <chrono>
#include <atomic>
#include <fmt/core.h>
#include "SharedBuffer.hpp"

// ─── 共有メモリ直結モード ─────────────────────────────────────────────
// ANSIエスケープシーケンス生成 → PTY → 端末側パースという重い経路を全廃し、
// speed_terminal (Viewer --shared) が直接読む /speed_terminal_shm に
// ScreenCell を直接書き込む。文字列化・差分diff・writeシステムコールが
// 完全に消える。保守性より速度優先。

constexpr float volume = 30.0f;
constexpr float speed = 1.0f;
constexpr float sleep_value = -1; // 待機時間
const std::string FILENAME = "se.mp4"; // 動画ファイル名
const std::string FILENAME_MJPEG = "se_mjpeg.avi"; // MJPEGキャッシュファイル名
constexpr float FONT_CORRECTION = 2.76f; // speed_terminal 側 g_font_correction のデフォルトと一致させること
constexpr int GRID_WIDTH = 1920;         // 出力グリッド幅（品質ノブ。共有メモリサイズに直結）
constexpr size_t FRAME_RING_CAPACITY = 64; // SPSCリングバッファ容量（フレーム単位、~2秒分で十分）
const char* SHM_NAME = "/speed_terminal_shm";

const bool is_debug = true; // デバッグモード

// ─── SPSC リングバッファ（ScreenCellフレームをswapで受け渡し、コピーなし） ──
class CellFrameRing {
public:
    CellFrameRing(size_t capacity, size_t cells_per_frame)
        : buf_(capacity), cap_(capacity) {
        for (auto& f : buf_) f.resize(cells_per_frame);
    }

    bool try_push(std::vector<ScreenCell>& item) {
        const size_t head = head_.load(std::memory_order_relaxed);
        const size_t next = inc(head);
        if (next == tail_.load(std::memory_order_acquire)) {
            return false; // full
        }
        buf_[head].swap(item);
        head_.store(next, std::memory_order_release);
        return true;
    }

    bool try_pop(std::vector<ScreenCell>& out) {
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

    std::vector<std::vector<ScreenCell>> buf_;
    const size_t cap_;
    alignas(64) std::atomic<size_t> head_{0};
    alignas(64) std::atomic<size_t> tail_{0};
};

inline void resize(const cv::Mat& src, cv::Mat& dst, int new_height, int new_width) {
    cv::resize(src, dst, cv::Size(new_width, new_height), 0, 0, cv::INTER_NEAREST);
}

// ハーフブロック方式: 1セルで縦2ピクセルを表現。ScreenCell.codepoint=223固定
// (speed_terminal のシェーダーが 223 を上半分=fg/下半分=bg の特殊ブロックとして解釈する)
inline void processHalfBlockRowCells(const cv::Mat& image, int out_row, ScreenCell* row_out, int cols) {
    const int src_row_top = out_row * 2;
    const int src_row_bot = src_row_top + 1;
    const bool has_bot = src_row_bot < image.rows;

    const uint8_t* top_raw = image.ptr<uint8_t>(src_row_top);
    const uint8_t* bot_raw = has_bot ? image.ptr<uint8_t>(src_row_bot) : nullptr;

    for (int j = 0; j < cols; ++j) {
        const int idx = j * 3;
        // cv::Mat は BGR 順
        const uint32_t fg = static_cast<uint32_t>(top_raw[idx + 2])
                           | (static_cast<uint32_t>(top_raw[idx + 1]) << 8)
                           | (static_cast<uint32_t>(top_raw[idx + 0]) << 16)
                           | (0xFFu << 24);

        uint32_t bg;
        if (has_bot) {
            bg = static_cast<uint32_t>(bot_raw[idx + 2])
               | (static_cast<uint32_t>(bot_raw[idx + 1]) << 8)
               | (static_cast<uint32_t>(bot_raw[idx + 0]) << 16)
               | (0xFFu << 24);
        } else {
            bg = 0xFF000000u;
        }

        ScreenCell& cell = row_out[j];
        cell.fg_rgba = fg;
        cell.bg_rgba = bg;
        cell.codepoint = 223;
        cell.reserved = 0;
    }
}

// 行ベクタではなくフラット配列 (row-major, stride=cols) に直接書き込む
// output はリングバッファとの swap で使い回されるため、swap で戻ってきたスロットの
// サイズが不定になり得る（consumer 側の空ベクタと入れ替わっている可能性がある）。
// 呼び出しごとにサイズを保証してから書き込む。
//
// 注意: この処理はセルあたりの仕事が軽すぎるため、OpenMPで並列化すると
// スレッド起動/結合のオーバーヘッドとデコードスレッドとのコア争いで
// 逆に遅くなる（実測: 12スレッド並列 6.0ms > シングルスレッド 2.9ms）。
// あえて並列化しない。
void modifyCells(const cv::Mat& image, std::vector<ScreenCell>& output, int out_rows, int cols) {
    const size_t needed = static_cast<size_t>(out_rows) * static_cast<size_t>(cols);
    if (output.size() != needed) {
        output.resize(needed);
    }
    for (int i = 0; i < out_rows; ++i) {
        processHalfBlockRowCells(image, i, output.data() + static_cast<size_t>(i) * cols, cols);
    }
}

int main() {
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

    if (t_wav.joinable())   t_wav.join();
    if (t_mjpeg.joinable()) {
        t_mjpeg.join();
        fmt::print(stderr, "[INFO] MJPEG transcoding done.\n");
    }

    const std::string play_file = (access(FILENAME_MJPEG.c_str(), F_OK) == 0)
                                  ? FILENAME_MJPEG : FILENAME;
    fmt::print(stderr, "[INFO] Using: {}\n", play_file);

    cv::VideoCapture vidObj(play_file);
    if (!vidObj.isOpened()) {
        fmt::print(stderr, "Error: Could not open file\n");
        return -1;
    }

    // 動画のアスペクト比から出力グリッドサイズを決定
    // (speed_terminal の Viewer --shared は width/(height*FONT_CORRECTION) を
    //  表示アスペクト比として使うので、そのフォーミュラに合わせて逆算する)
    const float video_aspect = static_cast<float>(vidObj.get(cv::CAP_PROP_FRAME_WIDTH))
                             / static_cast<float>(vidObj.get(cv::CAP_PROP_FRAME_HEIGHT));

    const int target_w = GRID_WIDTH;
    const int out_rows = std::max(1, static_cast<int>(std::round(target_w / (video_aspect * FONT_CORRECTION))));
    const int target_h = out_rows * 2;

    fmt::print(stderr, "Render: {}x{} (grid: {}x{})\n", target_w, target_h, target_w, out_rows);

    const size_t cells_per_frame = static_cast<size_t>(target_w) * static_cast<size_t>(out_rows);

    // 共有メモリを作成（speed_terminal の Viewer が --shared で接続してくる）
    SharedMemoryBuffer shm(SHM_NAME, static_cast<uint32_t>(target_w), static_cast<uint32_t>(out_rows));
    SharedHeader* header = shm.get_header();
    fmt::print(stderr, "[INFO] Shared memory ready: {} ({:.1f} MB). Waiting for viewer (--shared)...\n",
               SHM_NAME, shm.get_size() / 1024.0 / 1024.0);

    CellFrameRing frame_ring(FRAME_RING_CAPACITY, cells_per_frame);

    std::atomic<bool> producer_done{false};
    int frame_count = static_cast<int>(vidObj.get(cv::CAP_PROP_FRAME_COUNT));
    FILE *fp = fopen("output.txt", "w");
    float fps = vidObj.get(cv::CAP_PROP_FPS) * speed;

    fmt::print(fp, "[CONFIG] grid={}x{} fps={:.1f}\n", target_w, out_rows, fps);

    std::thread cv_thred([&frame_count, &frame_ring, &vidObj, &producer_done, &fp, fps, target_h, target_w, out_rows](){
        const double sleep = 1.0 / (fps * 1.25);
        const int pass_time_count = 100;
        std::vector<ScreenCell> output(static_cast<size_t>(out_rows) * static_cast<size_t>(target_w));
        cv::Mat image, resized_image;
        for (size_t i = 0; i < static_cast<size_t>(frame_count); ++i) {
            auto start_time = std::chrono::high_resolution_clock::now();
            if (!vidObj.read(image)) break;
            resize(image, resized_image, target_h, target_w);
            modifyCells(resized_image, output, out_rows, target_w);
            while (!frame_ring.try_push(output)) {
                std::this_thread::sleep_for(std::chrono::microseconds(200));
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

    if (sleep_value > 0) {
        const size_t prebuffer = std::min<size_t>(20, static_cast<size_t>(frame_count / sleep_value));
        while (frame_ring.size_approx() < prebuffer && !producer_done.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    sf::Music music;
    if (!music.openFromFile("output.wav")) {
        fmt::print(stderr, "Error loading audio file\n");
        return -1;
    }
    music.setPitch(speed);
    music.setVolume(volume);
    auto start_time = std::chrono::high_resolution_clock::now();
    music.play();
    header->is_writer_active.store(true, std::memory_order_release);

    std::thread publish_thread([&frame_ring, &producer_done, fps, frame_count, &fp, &start_time, &shm, header, cells_per_frame]() {
        const int max_frame = frame_count - 2;
        double sleep = 1.0 / fps;
        size_t displayed = 0;
        int write_idx = 1; // header->latest_buffer は 0 で初期化されているので 1 から書き始める

        std::vector<ScreenCell> cur_frame;
        std::vector<ScreenCell> dropped_frame;

        while (displayed < static_cast<size_t>(max_frame)) {
            auto frame_start_time = std::chrono::high_resolution_clock::now();
            auto current_time = std::chrono::high_resolution_clock::now();
            std::chrono::duration<double> elapsed_time = current_time - start_time;
            const size_t expected_frame_index = static_cast<size_t>(std::max(0, static_cast<int>(elapsed_time.count() * fps)));

            size_t queue_depth_after_pop = 0;
            // 遅延フレームをスキップ（音声とのシンク維持）
            while (displayed < expected_frame_index && frame_ring.size_approx() > 1) {
                if (!frame_ring.try_pop(dropped_frame)) {
                    break;
                }
                ++displayed;
            }

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
                // 共有メモリのトリプルバッファに直接コピーして公開（差分計算・エスケープ生成・write syscall 一切なし）
                ScreenCell* wbuf = shm.get_buffer(write_idx);
                std::memcpy(wbuf, cur_frame.data(), cells_per_frame * sizeof(ScreenCell));
                write_idx = header->latest_buffer.exchange(write_idx, std::memory_order_acq_rel);
                header->sequence.fetch_add(1, std::memory_order_release);
            } else {
                fmt::print(fp, "[WARNING] empty frame, queue_depth = {}\n", queue_depth_after_pop);
            }

            ++displayed;
            auto frame_end_time = std::chrono::high_resolution_clock::now();
            std::chrono::duration<double> processing_time = frame_end_time - frame_start_time;
            double sleep_time = sleep - processing_time.count();
            if (sleep_time > 0) {
                if (is_debug){
                    fmt::print(fp, "[INFO] publish_frame = {}, processing_time = {:.6f}, sleep_time = {:.6f}, queue_depth = {}\n", displayed, processing_time.count(), sleep_time, queue_depth_after_pop);
                }
                std::this_thread::sleep_for(std::chrono::duration<double>(sleep_time));
            } else {
                fmt::print(fp, "[WARNING] publish_frame = {}, processing_time = {:.6f}, sleep_time = {:.6f}, queue_depth = {}\n", displayed, processing_time.count(), sleep_time, queue_depth_after_pop);
            }
        }
    });

    publish_thread.join();
    cv_thred.join();
    music.stop();
    header->is_writer_active.store(false, std::memory_order_release);
    fmt::print("end_display\n");
    fmt::print(fp, "end\n");
    fclose(fp);
    return 0;
}
