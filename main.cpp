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

const std::vector<std::string> ASCII_CHARS = {"⣿", "⣾", "⣫", "⣪", "⣩", "⡶", "⠶", "⠖", "⠆", "⠄", " "};
constexpr float volume = 80.0f;
constexpr float speed = 1.0f;
constexpr int HEIGHT = 390;
// constexpr int HEIGHT = 280;
constexpr int fps_value = 1;
constexpr int sleep_value = -1;
const std::string FILENAME = "bell.mp4"; // 動画ファイル名
const bool is_debug = false; // デバッグモード

cv::Mat resize(const cv::Mat& image, int new_height = HEIGHT) {
    int old_width = image.cols;
    int old_height = image.rows;
    float aspect_ratio = static_cast<float>(old_width) / static_cast<float>(old_height);
    int new_width = static_cast<int>(aspect_ratio * new_height * 2.65);
    cv::Mat resized_image;
    cv::resize(image, resized_image, cv::Size(new_width, new_height));
    return resized_image;
}

cv::Mat grayscalify(const cv::Mat& image, double alpha = 1.4, int beta = 1, bool reverse = true) {
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

std::string modify(const cv::Mat& image) {
    std::vector<std::string> output(image.rows);
    
    #pragma omp parallel for
    for (int i = 0; i < image.rows; ++i) {
        fmt::memory_buffer buf;
        const uchar* row_ptr = image.ptr<uchar>(i);
        for (int j = 0; j < image.cols; ++j) {
            int pixel_value = row_ptr[j];
            fmt::format_to(std::back_inserter(buf), "{}", ASCII_CHARS[pixel_value / 25]);
        }
        fmt::format_to(std::back_inserter(buf), "\n");
        output[i] = fmt::to_string(buf);
    }
    
    fmt::memory_buffer final_buf;
    fmt::format_to(std::back_inserter(final_buf), "\033[H");
    for (const auto& line : output) {
        fmt::format_to(std::back_inserter(final_buf), "{}", line);
    }
    fmt::format_to(std::back_inserter(final_buf), "\033[0m");
    return fmt::to_string(final_buf);
}

std::string doProcess(const cv::Mat& image) {
    cv::Mat resized_image = resize(image, HEIGHT);
    cv::Mat gray_image = grayscalify(resized_image);
    return modify(gray_image);
}

int main() {
    cv::VideoCapture vidObj(FILENAME);
    if (!vidObj.isOpened()) {
        std::cerr << "Error: Could not open file" << std::endl;
        return -1;
    }
    std::ios_base::sync_with_stdio(false);
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
    std::thread cv_thred([&frame_count, &frames, &vidObj, &image, &frames_mutex, &fp, fps](){
        const double sleep = 1.0 / (fps * 1.25);
        const int pass_time_count = 100;
        for (size_t i = 0; i < frame_count; i+=fps_value) {
            auto start_time = std::chrono::high_resolution_clock::now();
            if (!vidObj.read(image)) break;
            std::string frame = doProcess(image);
            if (!frame.empty()) {
                std::lock_guard<std::mutex> lock(frames_mutex);
                frames.push_back(frame);
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
                // fprintf(fp, "display_frame = %ld, processing_time = %f, sleep_time = %f, frames.size - i = %ld\n", i, processing_time.count(), sleep_time, frames.size() - i);
                std::this_thread::sleep_for(std::chrono::duration<double>(sleep_time));
            }else{
                fprintf(fp, "[WARNING] display_frame = %ld, processing_time = %f, sleep_time = %f, frames.size - i = %ld\n", i, processing_time.count(), sleep_time, frames.size() - i);
            }
        }
    });
    display_thread.join();
    cv_thred.join();
    music.stop();
    system("clear");
    printf("end_display\n");
    fprintf(fp, "end\n");
    fclose(fp);
    return 0;
}