#include <iostream>
#include <vector>
#include <string>
#include <unistd.h>
#include <ctime>
#include <opencv2/opencv.hpp>
#include <SFML/Audio.hpp>
#include <thread>
#include <chrono>
#include <mutex>
#include <omp.h>
#include <atomic>
#include <fmt/format.h>
#include <sys/ioctl.h>
#include <sys/uio.h>

constexpr float volume = 30.0f;
constexpr float speed = 1.0f;
constexpr float sleep_value = -1;//待機時間
const std::string FILENAME = "tadakimi.mp4"; // 動画ファイル名
constexpr float FONT_CORRECTION = 2.76f; // フォントアスペクト比補正
constexpr int MAX_RENDER_WIDTH = 2000;   // 描画幅上限（ターミナル描画速度の限界）

const bool is_debug = false; // デバッグモード

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

inline constexpr std::array<uint8_t, 256> make_quant_table() {
    std::array<uint8_t, 256> table{};
    for (int i = 0; i < 256; ++i) {
        table[i] = static_cast<uint8_t>(i - (i % 8)); // 8刻みで量子化
    }
    return table;
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
    static const std::array<uint8_t, 256> quant_table = make_quant_table();
    const int src_row_top = out_row * 2;
    const int src_row_bot = src_row_top + 1;
    const bool has_bot = src_row_bot < image.rows;
    
    const cv::Vec3b* top_ptr = image.ptr<cv::Vec3b>(src_row_top);
    const cv::Vec3b* bot_ptr = has_bot ? image.ptr<cv::Vec3b>(src_row_bot) : nullptr;
    
    std::string line;
    line.reserve(image.cols * 10);
    
    int prev_fg_r = -1, prev_fg_g = -1, prev_fg_b = -1;
    int prev_bg_r = -1, prev_bg_g = -1, prev_bg_b = -1;
    
    for (int j = 0; j < image.cols; ++j) {
        const auto& tp = top_ptr[j];
        int fg_r = quant_table[tp[2]];
        int fg_g = quant_table[tp[1]];
        int fg_b = quant_table[tp[0]];
        
        int bg_r, bg_g, bg_b;
        if (has_bot) {
            const auto& bp = bot_ptr[j];
            bg_r = quant_table[bp[2]];
            bg_g = quant_table[bp[1]];
            bg_b = quant_table[bp[0]];
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

std::string modify(const cv::Mat& image) {
    const int out_rows = (image.rows + 1) / 2;
    std::vector<std::string> output(out_rows);
    
    #pragma omp parallel for
    for (int i = 0; i < out_rows; ++i) {
        processHalfBlockRow(std::cref(image), i, out_rows, std::ref(output));
    }
    
    size_t total = 3; // \033[H
    for (const auto& line : output) total += line.size();
    total += 4; // \033[0m
    
    std::string result;
    result.reserve(total);
    result.append("\033[H");
    for (const auto& line : output) result.append(line);
    result.append("\033[0m");
    return result;
}

int main() {
    std::string commands = "ffmpeg -y -i " + FILENAME + " -vn output.wav";
    std::thread t([&commands](){
        system(commands.c_str());
    });
    cv::VideoCapture vidObj(FILENAME);
    std::ios_base::sync_with_stdio(false);
    std::cin.tie(nullptr);
    if (!vidObj.isOpened()) {
        std::cerr << "Error: Could not open file" << std::endl;
        return -1;
    }
    
    // ターミナルサイズに合わせた解像度を計算
    const int term_h = getTermHeight() - 1;  // 最終行を使わない（スクロール防止）
    const int term_w = getTermWidth();
    
    // 描画幅上限を適用（ターミナル幅との小さい方）
    const int effective_w = std::min(term_w, MAX_RENDER_WIDTH);
    
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
    
    fprintf(stderr, "Terminal: %dx%d, Render: %dx%d (display: %dx%d)\n", 
            term_w, term_h + 1, target_w, target_h, target_w, (target_h + 1) / 2);
    
    std::vector<std::string> frames;
    frames.reserve(static_cast<size_t>(vidObj.get(cv::CAP_PROP_FRAME_COUNT)));    
    std::mutex frames_mutex;
    int frame_count = static_cast<int>(vidObj.get(cv::CAP_PROP_FRAME_COUNT));
    cv::Mat image;
    FILE *fp;
    fp = fopen("output.txt", "w");
    float fps = vidObj.get(cv::CAP_PROP_FPS) * speed;
    
    fprintf(fp, "[CONFIG] term=%dx%d render=%dx%d display_rows=%d fps=%.1f\n",
            term_w, term_h + 1, target_w, target_h, (target_h + 1) / 2, fps);
    
    std::thread cv_thred([&frame_count, &frames, &vidObj, &image, &frames_mutex, &fp, fps, target_h, target_w](){
        const double sleep = 1.0 / (fps * 1.25);
        const int pass_time_count = 100;
        for (size_t i = 0; i < frame_count; ++i) {
            auto start_time = std::chrono::high_resolution_clock::now();
            if (!vidObj.read(image)) break;
            cv::Mat resized_image = resize(image, target_h, target_w);
            std::string frame = modify(resized_image);
            if (!frame.empty()) {
                std::lock_guard<std::mutex> lock(frames_mutex);
                frames.push_back(std::move(frame));
            }
            auto end_time = std::chrono::high_resolution_clock::now();
            std::chrono::duration<double> elapsed_time = end_time - start_time;
            double sleep_time = sleep - elapsed_time.count();
            if (sleep_time > 0 && i > pass_time_count) {
                std::this_thread::sleep_for(std::chrono::duration<double>(sleep_time));
                if (is_debug){
                    fprintf(fp, "[INFO] process_frame = %ld, elapsed_time = %f, sleep_time = %f\n", i, elapsed_time.count(), sleep_time);
                }
            } else {
                if(i > pass_time_count){
                    fprintf(fp, "[WARNING] process_frame = %ld, elapsed_time = %f, sleep_time = %f\n", i, elapsed_time.count(), sleep_time);
                }
            }
        }
        vidObj.release();
        if (is_debug){
            fprintf(fp, "end_cv2\n");
        }
    });

    t.join();
    if (sleep_value > 0) {
        while (frames.size() < frame_count / sleep_value) {
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
        std::cerr << "Error loading audio file" << std::endl;
        return -1;
    }
    music.setPitch(speed);
    music.setVolume(volume);
    auto start_time = std::chrono::high_resolution_clock::now();
    music.play();
    
    std::thread display_thread([&frames, &frames_mutex, fps, frame_count, &fp, &start_time]() {
        int max_frame = frame_count - 2;
        double sleep = 1.0 / fps;
        for (size_t i = 0; i < max_frame; ++i) {
            auto frame_start_time = std::chrono::high_resolution_clock::now();
            auto current_time = std::chrono::high_resolution_clock::now();
            std::chrono::duration<double> elapsed_time = current_time - start_time;
            int expected_frame_index = static_cast<int>(elapsed_time.count() * fps);
            while (i < expected_frame_index && i < frame_count && i < frames.size()) {
                ++i;
            }
            {
                if (i < frames.size() && !frames[i].empty()) {
                    write(STDOUT_FILENO, frames[i].c_str(), frames[i].size());
                } else {
                    fprintf(fp, "[WARNING] frame = %ld, frames.size() = %ld\n", i, frames.size());
                }
            }
            auto frame_end_time = std::chrono::high_resolution_clock::now();
            std::chrono::duration<double> processing_time = frame_end_time - frame_start_time;
            double sleep_time = sleep - processing_time.count();
            if (sleep_time > 0) {
                std::lock_guard<std::mutex> lock(frames_mutex);
                size_t frame_size = frames[i].size();
                frames[i].clear();
                frames[i].shrink_to_fit();
                if (is_debug){
                    fprintf(fp, "[INFO] display_frame = %ld, processing_time = %f, sleep_time = %f, frames.size - i = %ld, frame_size() = %ld\n", i, processing_time.count(), sleep_time, frames.size() - i, frame_size);
                }
                std::this_thread::sleep_for(std::chrono::duration<double>(sleep_time));
            } else {
                fprintf(fp, "[WARNING] display_frame = %ld, processing_time = %f, sleep_time = %f, frames.size - i = %ld, frame_size() = %ld\n", i, processing_time.count(), sleep_time, frames.size() - i, frames[i].size());
                frames[i].clear();
                frames[i].shrink_to_fit();
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
    printf("end_display\n");
    fprintf(fp, "end\n");
    fclose(fp);
    return 0;
}