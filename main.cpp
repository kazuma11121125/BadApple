#include <iostream>
#include <vector>
#include <string>
#include <unistd.h>
#include <ctime>
#include <opencv2/opencv.hpp>
#include <SFML/Audio.hpp>
#include <thread>

const std::vector<std::string> ASCII_CHARS = {"⣿", "⣾", "⣫", "⣪", "⣩", "⡶", "⠶", "⠖", "⠆", "⠄", "⠀"};
const int HEIGHT = 40;
const float volume = 50.0f;
const float speed = 0.970f;
const std::string FILENAME = "aaa.webm"; // 動画ファイル名

cv::Mat resize(const cv::Mat& image, int new_height = HEIGHT) {
    int old_width = image.cols;
    int old_height = image.rows;
    float aspect_ratio = static_cast<float>(old_width) / static_cast<float>(old_height);
    int new_width = static_cast<int>(aspect_ratio * new_height * 2);
    cv::Mat resized_image;
    cv::resize(image, resized_image, cv::Size(new_width, new_height));
    return resized_image;
}

cv::Mat grayscalify(const cv::Mat& image) {
    cv::Mat gray_image;
    cv::cvtColor(image, gray_image, cv::COLOR_BGR2GRAY);
    return gray_image;
}

std::string modify(const cv::Mat& image, int buckets = 25) {
    std::string new_pixels;
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
        std::cerr << "error: Not open file" << std::endl;
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
    std::cout << "All set...Press Enter to start the video" << std::endl;
    std::cin.get();
    music.setVolume(volume);
    music.play();
    int i = 0;
    for (const auto& frame : frames) {
        system("clear");
        std::cout << "フレーム数: " << i << std::endl;
        std::cout << frame << std::endl;
        i++;
        usleep(1000000 / fps);
    }
    
    std::cout << "Video completed" << std::endl;
    music.stop();
    
    return 0;
}
