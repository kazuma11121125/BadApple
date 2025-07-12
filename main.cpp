#include <iostream>
#include <vector>
#include <string>
#include <unistd.h>
#include <ctime>
#include <opencv2/opencv.hpp>
#include <SFML/Audio.hpp>
#include <thread>
#include <chrono>
#include <sstream>
#include <mutex>
#include <omp.h>
#include <atomic>

constexpr float volume = 30.0f;
constexpr float speed = 1.0f;
// constexpr int HEIGHT = 251; // 画像の高さ
constexpr int HEIGHT = 303; // 画像の高さ
// constexpr int HEIGHT = 123; // 画像の高さ
constexpr float sleep_value = -1;//待機時間
const std::string FILENAME = "yo.mp4"; // 動画ファイル名

const bool is_debug = false; // デバッグモード

inline cv::Mat resize(const cv::Mat& image, int new_height = HEIGHT) {
    const float scale    = static_cast<float>(new_height) / image.rows;
    const int   new_width = static_cast<int>(image.cols * scale * 2.56f); // 2.76f
    cv::Mat resized_image;
    resized_image.create(new_height, new_width, image.type());
    cv::resize(image,resized_image,resized_image.size(),0, 0,cv::INTER_NEAREST);
    return resized_image;
}

inline constexpr std::array<uint8_t, 256> make_quant_table() {
    std::array<uint8_t, 256> table{};
    for (int i = 0; i < 256; ++i) {
        table[i] = static_cast<uint8_t>(i - (i % 5));
    }
    return table;
}

void processRow(const cv::Mat& image, int row, std::vector<std::string>& output) {
    static const std::array<uint8_t, 256> quant_table = make_quant_table();
    const cv::Vec3b* row_ptr = image.ptr<cv::Vec3b>(row);
    std::string line;
    line.reserve(image.cols * 8);  // ANSI コードと空白を含めた概算
    int prev_r = -1, prev_g = -1, prev_b = -1;
    char buf[32];
    for (int j = 0; j < image.cols; ++j) {
        const auto& px = row_ptr[j];
        int r = quant_table[px[2]];
        int g = quant_table[px[1]];
        int b = quant_table[px[0]];
        if (r != prev_r || g != prev_g || b != prev_b) {
            int len = std::snprintf(buf, sizeof(buf), "\033[48;2;%d;%d;%dm", r, g, b);
            line.append(buf, len);
            prev_r = r; prev_g = g; prev_b = b;
        }
        line.push_back(' ');
    }
    line.push_back('\n');
    output[row] = std::move(line);
}

std::string modify(const cv::Mat& image) {
    std::vector<std::string> output(image.rows);
    #pragma omp parallel for
    for (int i = 0; i < image.rows; ++i) {
        processRow(std::cref(image), i, std::ref(output));
    }
    std::ostringstream final_output;
    final_output << "\033[H";
    for (const auto& line : output) {
        final_output << line;
    }
    return final_output.str();
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
    std::vector<std::string> frames;
    frames.reserve(static_cast<size_t>(vidObj.get(cv::CAP_PROP_FRAME_COUNT)));    
    std::mutex frames_mutex;
    int frame_count = static_cast<int>(vidObj.get(cv::CAP_PROP_FRAME_COUNT));
    cv::Mat image;
    FILE *fp;
    fp = fopen("output.txt", "w");
    std::thread cv_thred([&frame_count, &frames, &vidObj, &image, &frames_mutex, &fp](){
        for (size_t i = 0; i < frame_count; ++i) {
            auto start_time = std::chrono::high_resolution_clock::now();
            if (!vidObj.read(image)) break;
            cv::Mat resized_image = resize(image);
            std::string frame = modify(resized_image);
            if (!frame.empty()) {
                std::lock_guard<std::mutex> lock(frames_mutex);
                frames.push_back(frame);
            }
            auto end_time = std::chrono::high_resolution_clock::now();
            std::chrono::duration<double> elapsed_time = end_time - start_time;
            if (is_debug){
                fprintf(fp, "frame = %ld, elapsed_time = %f,frame_size = %ld\n", i, elapsed_time.count(),frame.size());
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
    system("clear");
    float fps = vidObj.get(cv::CAP_PROP_FPS) * speed;
    sf::Music music;
    if (!music.openFromFile("output.wav")) {
        std::cerr << "Error loading audio file" << std::endl;
        return -1;
    }
    music.setPitch(speed);
    music.setVolume(volume);
    music.play();
    auto start_time = std::chrono::high_resolution_clock::now();
    
    // フレーム同期用の変数
    std::atomic<size_t> current_frame_index{0};
    std::mutex sync_mutex;
    
    // フレーム同期スレッド
    std::thread sync_thread([&current_frame_index, &start_time, fps, frame_count, &fp]() {
        while (current_frame_index.load() < static_cast<size_t>(frame_count - 2)) {
            auto current_time = std::chrono::high_resolution_clock::now();
            std::chrono::duration<double> elapsed_time = current_time - start_time;
            size_t expected_frame_index = static_cast<size_t>(elapsed_time.count() * fps);
            size_t current_val = current_frame_index.load();
            if (expected_frame_index > current_val) {
                current_frame_index.store(std::min(expected_frame_index, static_cast<size_t>(frame_count - 2)));
            }
        }
    });
    
    std::thread display_thread([&frames, &current_frame_index, &frames_mutex, fps, frame_count, &fp]() {
        int max_frame = frame_count - 2;
        double sleep = 1.0 / fps;
        size_t i = 0;
        
        for (; i < max_frame; ++i) {
            auto frame_start_time = std::chrono::high_resolution_clock::now();            
            size_t target_frame = current_frame_index.load();
            if (i < target_frame && target_frame < frames.size()) {
                i = target_frame;
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
                auto frame_clear_start = std::chrono::high_resolution_clock::now();
                int frame_size = frames[i].size();
                frames[i].clear();
                frames[i].shrink_to_fit();
                auto frame_clear_end_time = std::chrono::high_resolution_clock::now();
                std::chrono::duration<double> frame_clear_time = frame_clear_end_time - frame_clear_start;
                sleep_time -= frame_clear_time.count();
                if (is_debug){
                    fprintf(fp, "display_frame = %ld, processing_time = %f, sleep_time = %f, frames.size - i = %ld, frame_size() = %d\n", i, processing_time.count(), sleep_time, frames.size() - i, frame_size);
                }
                std::this_thread::sleep_for(std::chrono::duration<double>(sleep_time));
            }else{
                fprintf(fp, "[WARNING] display_frame = %ld, processing_time = %f, sleep_time = %f, frames.size - i = %ld, frame_size() = %d\n", i, processing_time.count(), sleep_time, frames.size() - i, frames[i].size());
            }
        }
    });

    display_thread.join();
    sync_thread.join();
    cv_thred.join();
    music.stop();
    system("clear");
    printf("end_display\n");
    fprintf(fp, "end\n");
    fclose(fp);
    return 0;
}