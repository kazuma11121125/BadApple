#include <iostream>
#include <vector>
#include <string>
#include <unistd.h>
#include <ctime>
#include <opencv2/opencv.hpp>
#include <SFML/Audio.hpp>
#include <thread>
#include <chrono>

const std::vector<std::string> ASCII_CHARS = {"⣿", "⣾", "⣫", "⣪", "⣩", "⡶", "⠶", "⠖", "⠆", "⠄", "⠀"};
const int HEIGHT = 40;
const float volume = 80.0f;
const float speed = 1.0f;
const std::string FILENAME = "kkk.webm"; // 動画ファイル名

cv::Mat resize(const cv::Mat& image, int new_height = HEIGHT) {
    int old_width = image.cols;
    int old_height = image.rows;
    float aspect_ratio = static_cast<float>(old_width) / static_cast<float>(old_height);
    int new_width = static_cast<int>(aspect_ratio * new_height * 2);
    cv::Mat resized_image;
    cv::resize(image, resized_image, cv::Size(new_width, new_height));
    return resized_image;
}

std::string modify(const cv::Mat& image, int buckets = 25) {
    std::string new_pixels;
    new_pixels.reserve(image.rows * image.cols * 13); // 文字列の容量を事前に確保
    for (int i = 0; i < image.rows; ++i) {
        for (int j = 0; j < image.cols; ++j) {
            cv::Vec3b pixel = image.at<cv::Vec3b>(i, j);
            int blue = pixel[0];
            int green = pixel[1];
            int red = pixel[2];
            int gray = (red + green + blue) / 3;
            int color_index = gray / buckets;
            new_pixels += "\033[38;2;" + std::to_string(red) + ";" + std::to_string(green) + ";" + std::to_string(blue) + "m" + ASCII_CHARS[color_index];
        }
        new_pixels += "\n";
    }
    return new_pixels + "\033[0m"; // フレームの最後に色をリセット
}

std::string doProcess(const cv::Mat& image) {
    cv::Mat resized_image = resize(image, HEIGHT);
    return modify(resized_image);
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
    
    for (int i = 0; i < frame_count; ++i) {
        if (!vidObj.read(image)) break;
        std::string frame = doProcess(image);
        if (!frame.empty()) frames.push_back(frame);
        std::cout << "Frame " << i << " Completed" << std::endl;
    }
    t.join();
    float fps = vidObj.get(cv::CAP_PROP_FPS);
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
    for (int i = 0; i < frames.size(); ++i) {
        auto current_time = std::chrono::high_resolution_clock::now();// 現在の時刻を取得
        std::chrono::duration<double> total_elapsed_time = current_time - start_time;// 総経過時間を計算
        int expected_frame_index = static_cast<int>(total_elapsed_time.count() * fps);// 期待されるフレームインデックスを計算
        
        if (i < expected_frame_index) {
            continue; // 期待されるフレームインデックスに遅れている場合、フレームをスキップ
        }
        
        auto frame_start_time = std::chrono::high_resolution_clock::now();// フレーム処理の開始時刻
        
        system("clear");
        std::cout << "Frame: " << i << std::endl;
        //背景を黒に設定
        std::cout << "\033[48;2;0;0;0m";
        std::cout << frames[i] << std::flush; // std::endlの代わりにstd::flushを使用して出力バッファをフラッシュ
        
        auto frame_end_time = std::chrono::high_resolution_clock::now();// フレーム処理の終了時刻
        std::chrono::duration<double> processing_time = frame_end_time - frame_start_time;// フレーム処理時間を計算
        double sleep_time = (1.0 / fps) - processing_time.count();// スリープ時間を計算
        if (sleep_time > 0) {// スリープ時間が正の場合、スリープ
            usleep(static_cast<int>(sleep_time * 1000000));// スリープ時間をマイクロ秒に変換
        }
    }
    
    std::cout << "Video completed" << std::endl;
    music.stop();
    
    return 0;
}
