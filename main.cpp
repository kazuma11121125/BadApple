#include <iostream>
#include <vector>
#include <string>
#include <unistd.h>
#include <ctime>
#include <opencv2/opencv.hpp>
#include <SFML/Audio.hpp>
#include <thread>
#include <chrono>
#include <omp.h>
#include <fmt/format.h>
#include <cstring>
#include <sys/ioctl.h>
#include <sys/uio.h>

// ブレイユ文字を維持（見た目の品質）
const std::string BRAILLE_CHARS[] = {"⣿", "⣾", "⣫", "⣪", "⣩", "⡶", "⠶", "⠖", "⠆", "⠄", " "};
constexpr int BRAILLE_SIZE = 11;
constexpr float volume = 80.0f;
constexpr float speed = 1.0f;
constexpr int fps_value = 1;
constexpr int sleep_value = -1;
const std::string FILENAME = "bad_apple.mp4"; // 動画ファイル名
const bool is_debug = false; // デバッグモード

// ターミナルサイズを取得して、それに合わせた解像度を使う
// → 不要な行・列を一切生成しない = 最小データ量
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

cv::Mat resize(const cv::Mat& image, int new_height, int new_width) {
    cv::Mat resized_image;
    cv::resize(image, resized_image, cv::Size(new_width, new_height));
    return resized_image;
}

cv::Mat grayscalify(const cv::Mat& image, double alpha = 1.0, int beta = 1, bool reverse = true) {
    /*
    alpha: contrast control [1.0-3.0] // ある程度コントラストを強調
    beta: brightness control [0-100]　// 明るさの調整
    reverse: trueなら白黒反転
    */
    cv::Mat gray_image;
    cv::Mat adjusted_image;
    cv::cvtColor(image, gray_image, cv::COLOR_BGR2GRAY);
    if (reverse) {
        gray_image = 255 - gray_image;
        gray_image.convertTo(adjusted_image, -1, alpha, beta);
        return adjusted_image;
    }else{
    gray_image.convertTo(adjusted_image, -1, alpha, beta);
        return adjusted_image;
    }
    return gray_image;
}

// ダブルバッファ用の固定バッファ
struct FrameBuffer {
    std::vector<std::string> lines;
    std::string header;  // \033[H
    int rows = 0;
    int cols = 0;
    
    void init(int r, int c) {
        rows = r;
        cols = c;
        header = "\033[H";
        lines.resize(r);
        // 各行を事前に確保（ブレイユ文字3バイト * cols + 改行）
        for (auto& line : lines) {
            line.reserve(c * 3 + 1);
        }
    }
};

// フレームバッファに直接書き込む（メモリ確保なし）
void modifyInto(FrameBuffer& buf, const cv::Mat& image) {
    const int rows = buf.rows;
    const int cols = buf.cols;
    
    #pragma omp parallel for schedule(static)
    for (int i = 0; i < rows; ++i) {
        buf.lines[i].clear();
        const uchar* row_ptr = image.ptr<uchar>(i);
        for (int j = 0; j < cols; ++j) {
            int idx = row_ptr[j] / 25;
            if (idx >= BRAILLE_SIZE) idx = BRAILLE_SIZE - 1;
            buf.lines[i].append(BRAILLE_CHARS[idx]);
        }
        // 最終行は改行なし（スクロール/画面揺れ防止）
        if (i < rows - 1) {
            buf.lines[i].push_back('\n');
        }
    }
}

// writev() で scatter/gather I/O（バッファ結合なし）
void writeFrame(const FrameBuffer& buf) {
    // iovec配列: header + 各行
    const int count = 1 + buf.rows;
    std::vector<struct iovec> iov(count);
    
    iov[0].iov_base = const_cast<char*>(buf.header.c_str());
    iov[0].iov_len = buf.header.size();
    
    for (int i = 0; i < buf.rows; ++i) {
        iov[1 + i].iov_base = const_cast<char*>(buf.lines[i].c_str());
        iov[1 + i].iov_len = buf.lines[i].size();
    }
    
    // writev は IOV_MAX (通常1024) まで一度に送れる
    size_t offset = 0;
    size_t remaining = count;
    while (remaining > 0) {
        size_t chunk = remaining > 1024 ? 1024 : remaining;
        ssize_t written = writev(STDOUT_FILENO, &iov[offset], chunk);
        if (written < 0) break;
        offset += chunk;
        remaining -= chunk;
    }
}

// プリレンダリング用：文字列として結合して返す
std::string renderToString(FrameBuffer& buf, const cv::Mat& image) {
    modifyInto(buf, image);
    
    // 総バイト数を計算
    size_t total = buf.header.size();
    for (int i = 0; i < buf.rows; ++i) {
        total += buf.lines[i].size();
    }
    
    std::string result;
    result.reserve(total);
    result.append(buf.header);
    for (int i = 0; i < buf.rows; ++i) {
        result.append(buf.lines[i]);
    }
    return result;
}

std::string doProcess(FrameBuffer& buf, const cv::Mat& image, int target_h, int target_w) {
    cv::Mat resized_image = resize(image, target_h, target_w);
    cv::Mat gray_image = grayscalify(resized_image);
    return renderToString(buf, gray_image);
}

int main() {
    cv::VideoCapture vidObj(FILENAME);
    if (!vidObj.isOpened()) {
        std::cerr << "Error: Could not open file" << std::endl;
        return -1;
    }
    std::ios_base::sync_with_stdio(false);
    
    // ターミナルサイズに合わせた解像度を計算
    const int term_h = getTermHeight() - 2;  // 最終行を使わない（スクロール防止）
    const int term_w = getTermWidth();    
    const float video_aspect = static_cast<float>(vidObj.get(cv::CAP_PROP_FRAME_WIDTH)) 
                             / static_cast<float>(vidObj.get(cv::CAP_PROP_FRAME_HEIGHT));
    constexpr float FONT_CORRECTION = 2.65f;    
    int target_h, target_w;
    target_h = term_h;
    target_w = static_cast<int>(video_aspect * target_h * FONT_CORRECTION);
    if (target_w > term_w) {
        target_w = term_w;
        target_h = static_cast<int>(target_w / (video_aspect * FONT_CORRECTION));
    }
    
    fprintf(stderr, "Terminal: %dx%d, Render: %dx%d, Bytes/frame: ~%d\n", 
            term_w, term_h, target_w, target_h, target_w * target_h * 3);
    
    std::string commands = "ffmpeg -y -i " + FILENAME + " -vn output.wav";
    std::thread t([&commands](){
        system(commands.c_str());
    });
    
    std::vector<std::string> frames;
    std::mutex frames_mutex;
    int frame_count = static_cast<int>(vidObj.get(cv::CAP_PROP_FRAME_COUNT));
    cv::Mat image;
    FILE *fp;
    fp = fopen("output.txt", "w");
    float fps = vidObj.get(cv::CAP_PROP_FPS) / fps_value * speed;
    
    fprintf(fp, "[CONFIG] term=%dx%d render=%dx%d bytes/frame=%d fps=%.1f\n",
            term_w, term_h, target_w, target_h, target_w * target_h * 3, fps);
    
    std::thread cv_thred([&frame_count, &frames, &vidObj, &image, &frames_mutex, &fp, fps, target_h, target_w](){
        // 生成スレッド用のFrameBufferを1つ確保（使い回し）
        FrameBuffer gen_buf;
        gen_buf.init(target_h, target_w);
        
        const double sleep = 1.0 / (fps * 1.25);
        const int pass_time_count = 100;
        for (size_t i = 0; i < frame_count; i+=fps_value) {
            auto start_time = std::chrono::high_resolution_clock::now();
            if (!vidObj.read(image)) break;
            std::string frame = doProcess(gen_buf, image, target_h, target_w);
            if (!frame.empty()) {
                std::lock_guard<std::mutex> lock(frames_mutex);
                frames.push_back(std::move(frame));
            }
            for (int j = 1; j < fps_value; ++j) {
                if (!vidObj.grab()) break;
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
        while (frames.size() < (frame_count / fps_value) / sleep_value) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    system("clear");
    printf("\033[?25l");    
    printf("\033[1;%dr", target_h);  // スクロール領域を1〜target_h行に制限
    printf("\033[?7l");
    fflush(stdout);
    sf::Music music;
    if (!music.openFromFile("output.wav")) {
        std::cerr << "Error loading audio file" << std::endl;
        return -1;
    }
    music.setPitch(speed);
    system("clear");
    music.setVolume(volume);
    music.play();
    auto start_time = std::chrono::high_resolution_clock::now();
    std::thread display_thread([&frames, &start_time, &frames_mutex, fps, frame_count, &fp]() {
        for (size_t i = 0; i < ((frame_count / fps_value) -2); ++i) {
            auto current_time = std::chrono::high_resolution_clock::now();
            std::chrono::duration<double> elapsed_time = current_time - start_time;
            int expected_frame_index = static_cast<int>(elapsed_time.count() * fps);
            while (i < expected_frame_index && i < (frame_count / fps_value) && i < frames.size()) {
                ++i;
            }
            auto frame_start_time = std::chrono::high_resolution_clock::now();
            {
                if (i < frames.size() && !frames[i].empty()) {
                    write(STDOUT_FILENO, frames[i].c_str(), frames[i].size());

                } else {
                    fprintf(fp, "frame = %ld, frames.size() = %ld\n", i, frames.size());
                }
            }
            auto frame_end_time = std::chrono::high_resolution_clock::now();
            std::chrono::duration<double> processing_time = frame_end_time - frame_start_time;
            double sleep_time = (1.0 / fps) - processing_time.count();
            if (sleep_time > 0) {
                std::lock_guard<std::mutex> lock(frames_mutex);
                auto frame_clear_start = std::chrono::high_resolution_clock::now();
                frames[i].clear();
                frames[i].shrink_to_fit();
                auto frame_clear_end_time = std::chrono::high_resolution_clock::now();
                std::chrono::duration<double> frame_clear_time = frame_clear_end_time - frame_clear_start;
                sleep_time -= frame_clear_time.count();
                std::this_thread::sleep_for(std::chrono::duration<double>(sleep_time));
            }else{
                fprintf(fp, "[WARNING] display_frame = %ld, processing_time = %f, sleep_time = %f, frames.size - i = %ld\n", i, processing_time.count(), sleep_time, frames.size() - i);
            }
        }
    });
    display_thread.join();
    cv_thred.join();
    music.stop();
    printf("\033[?25h");  
    printf("\033[r");  
    printf("\033[?7h");
    fflush(stdout);
    system("clear");
    printf("end_display\n");
    fprintf(fp, "end\n");
    fclose(fp);
    return 0;
}