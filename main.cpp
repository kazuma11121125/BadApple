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

const float volume = 70.0f;
const float speed = 1.0f;
const int fps_value = 1;
const int HEIGHT = 300; // 画像の高さ
const std::string FILENAME = "tadakimi.mp4"; // 動画ファイル名

cv::Mat resize(const cv::Mat& image, int new_height = HEIGHT) {
    int old_width = image.cols;
    int old_height = image.rows;
    float aspect_ratio = static_cast<float>(old_width) / static_cast<float>(old_height);
    int new_width = static_cast<int>(aspect_ratio * new_height * 2.5);
    cv::Mat resized_image;
    cv::resize(image, resized_image, cv::Size(new_width, new_height));
    return resized_image;
}

std::string modify(const cv::Mat& image) {
    std::ostringstream oss;
    oss << "\033[H"; // カーソルをトップに移動
    int prev_red = -1, prev_green = -1, prev_blue = -1;

    for (int i = 0; i < image.rows; ++i) {
        for (int j = 0; j < image.cols; ++j) {
            cv::Vec3b pixel = image.at<cv::Vec3b>(i, j);
            int quantized_red = (pixel[2] / 3) * 3;
            int quantized_green = (pixel[1] / 3) * 3;
            int quantized_blue = (pixel[0] / 3) * 3;

            if (quantized_red != prev_red || quantized_green != prev_green || quantized_blue != prev_blue) {
                oss << "\033[48;2;" << quantized_red << ";" << quantized_green << ";" << quantized_blue << "m";
                prev_red = quantized_red;
                prev_green = quantized_green;
                prev_blue = quantized_blue;
            }
            oss << " ";
        }
        oss << "\n";
    }
    oss << "\033[0m";
    return oss.str();
}

std::string doProcess(const cv::Mat& image) {
    cv::Mat resized_image = resize(image, HEIGHT);
    return modify(resized_image);
}

int main() {
    cv::VideoCapture vidObj(FILENAME);
    std::ios_base::sync_with_stdio(false);
    std::cin.tie(nullptr);
    if (!vidObj.isOpened()) {
        std::cerr << "Error: Could not open file" << std::endl;
        return -1;
    }
    std::string commands = "ffmpeg -y -i " + FILENAME + " -vn output.ogg";
    std::thread t([&commands](){
        system(commands.c_str());
    });
    std::vector<std::string> frames;
    std::mutex frames_mutex;
    int frame_count = static_cast<int>(vidObj.get(cv::CAP_PROP_FRAME_COUNT));
    cv::Mat image;
    for (size_t i = 0; i < frame_count; i += fps_value) {
        if (!vidObj.read(image)) break;
        std::string frame = doProcess(image);
        if (!frame.empty()) {
            std::lock_guard<std::mutex> lock(frames_mutex);
            frames.push_back(frame);
        }
        for (int j = 1; j < fps_value; j++) {
            if (!vidObj.grab()) break;
        }
        std::cout << "Processing frame " << i << " of " << frame_count << std::endl;
    }
    t.join();
    float fps = vidObj.get(cv::CAP_PROP_FPS) / fps_value * speed;
    sf::Music music;
    if (!music.openFromFile("output.ogg")) {
        std::cerr << "Error loading audio file" << std::endl;
        return -1;
    }
    music.setPitch(speed);
    system("clear");
    std::cout << "Adjusted FPS: " << fps << std::endl;
    std::cout << "Frame count: " << frames.size() << std::endl;
    std::cout << "All set...Press Enter to start the video" << std::endl;
    std::cin.get();
    music.setVolume(volume);
    music.play();
    auto start_time = std::chrono::high_resolution_clock::now();
    std::thread display_thread([&frames, &start_time, &frames_mutex, fps]() {
        for (size_t i = 0; i < frames.size(); ++i) {
            auto current_time = std::chrono::high_resolution_clock::now();
            std::chrono::duration<double> elapsed_time = current_time - start_time;
            int expected_frame_index = static_cast<int>(elapsed_time.count() * fps);
            while (i < expected_frame_index && i < frames.size()) {
                ++i;
            }
            auto frame_start_time = std::chrono::high_resolution_clock::now();
            {
                std::lock_guard<std::mutex> lock(frames_mutex);
                write(STDOUT_FILENO, frames[i].c_str(), frames[i].size());
            }
            auto frame_end_time = std::chrono::high_resolution_clock::now();
            std::chrono::duration<double> processing_time = frame_end_time - frame_start_time;
            double sleep_time = (1.0 / fps) - processing_time.count();
            if (sleep_time > 0) {
                std::this_thread::sleep_for(std::chrono::duration<double>(sleep_time));
            }
        }
    });

    display_thread.join();
    music.stop();
    std::cout << "Video completed" << std::endl;
    return 0;
}
