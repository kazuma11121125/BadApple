#include <iostream>
#include <vector>
#include <string>
#include <unistd.h>
#include <ctime>
#include <opencv2/opencv.hpp>
#include <SFML/Audio.hpp>
#include <thread>
#include <chrono>

const std::vector<std::string> ASCII_CHARS = {"⣿", "⣾", "⣫", "⣪", "⣩", "⡶", "⠶", "⠖", "⠆", "⠄", " "};
const float volume = 80.0f;
const float speed = 1.0f;
const int HEIGHT = 70;
const int fps_value = 1;
const std::string FILENAME = "bad_apple.mp4"; // 動画ファイル名

cv::Mat resize(const cv::Mat& image, int new_height = HEIGHT) {
    int old_width = image.cols;
    int old_height = image.rows;
    float aspect_ratio = static_cast<float>(old_width) / static_cast<float>(old_height);
    int new_width = static_cast<int>(aspect_ratio * new_height * 3.5);
    cv::Mat resized_image;
    cv::resize(image, resized_image, cv::Size(new_width, new_height));
    return resized_image;
}

cv::Mat grayscalify(const cv::Mat& image, double alpha = 1.0, int beta = 0) {
    cv::Mat gray_image;
    cv::cvtColor(image, gray_image, cv::COLOR_BGR2GRAY);
    cv::Mat adjusted_image;
    gray_image.convertTo(adjusted_image, -1, alpha, beta);
    return adjusted_image;
}

std::string modify(const cv::Mat& image, int buckets = 25) {
    std::string new_pixels;
    new_pixels += "\033[H";// Move cursor to the top
    for (int i = 0; i < image.rows; ++i) {
        for (int j = 0; j < image.cols; ++j) {
            int pixel_value = image.at<uchar>(i, j);
            new_pixels += ASCII_CHARS[pixel_value / buckets];
        }
        new_pixels += '\n';
    }
    return new_pixels;
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
    
    std::string commands = "ffmpeg -y -i " + FILENAME + " -vn output.ogg";
    std::thread t([&commands](){
        system(commands.c_str());
    });
    
    std::vector<std::string> frames;
    int frame_count = static_cast<int>(vidObj.get(cv::CAP_PROP_FRAME_COUNT));
    std::cout << "Frame count: " << frame_count << std::endl;
    cv::Mat image;
    
    for (int i = 0; i < frame_count; i+=fps_value) {
        if (!vidObj.read(image)) break;
        std::string frame = doProcess(image);
        if (!frame.empty()) frames.push_back(frame);
        std::cout << "Frame " << i << " Completed" << std::endl;
        for (int j = 1; j < fps_value; ++j) {
            if (!vidObj.grab()) break; // 次のフレームをスキップ
        }
    }
    t.join();
    float fps = vidObj.get(cv::CAP_PROP_FPS) / fps_value * speed;
    std::cout << "FPS: " << fps << std::endl;
    sf::Music music;
    if (!music.openFromFile("output.ogg")) {
        std::cerr << "Error loading audio file" << std::endl;
        return -1;
    }
    music.setPitch(speed);
    system("clear");
    std::cout << "All set...Press Enter to start the video" << std::endl;
    std::cin.get();
    music.setVolume(volume);
    music.play();
    auto start_time = std::chrono::high_resolution_clock::now();
    std::thread display_thread([&frames, &start_time, fps](){
        for (int i = 0; i < frames.size(); i++) {
            auto current_time = std::chrono::high_resolution_clock::now();
            std::chrono::duration<double> total_elapsed_time = current_time - start_time;
            int expected_frame_index = static_cast<int>(total_elapsed_time.count() * fps);
            while (i < expected_frame_index) {
                if (i >= frames.size()) break;
                i++; // Skip frames if behind
            }
            auto frame_start_time = std::chrono::high_resolution_clock::now();
            write(STDOUT_FILENO, frames[i].c_str(), strlen(frames[i].c_str()));
            auto frame_end_time = std::chrono::high_resolution_clock::now();
            std::chrono::duration<double> processing_time = frame_end_time - frame_start_time;
            double sleep_time = (1.0 / fps) - processing_time.count();
            if (sleep_time > 0) {
                std::this_thread::sleep_for(std::chrono::duration<double>(sleep_time));
            }
        }
    });
    display_thread.join();
    
    std::cout << "Video completed" << std::endl;
    music.stop();
    
    return 0;
}
