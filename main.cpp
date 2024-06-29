#include <iostream>
#include <vector>
#include <string>
#include <opencv2/opencv.hpp>
#include <sys/stat.h>
#include <unistd.h>
#include <ctime>
#include <cmath>

const int CLIP_FRAMES = 6571;// 6571 frames in total
const std::vector<std::string> ASCII_CHARS = {"⣿", "⣾", "⣫", "⣪", "⣩", "⡶", "⠶", "⠖", "⠆", "⠄", "⠀"};// ASCII characters
const int WIDTH = 100;// 100 columns

cv::Mat resize(const cv::Mat& image, int new_width = WIDTH) {
    int old_width = image.cols;
    int old_height = image.rows;
    float aspect_ratio = static_cast<float>(old_height) / static_cast<float>(old_width);
    int new_height = static_cast<int>((aspect_ratio * new_width) / 2);
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

std::string doProcess(const cv::Mat& image, int new_width = WIDTH) {
    cv::Mat resized_image = resize(image, new_width);
    cv::Mat gray_image = grayscalify(resized_image);
    return modify(gray_image);
}

void framecapture() {
    cv::VideoCapture vidObj("bad_apple.mp4");
    int count = 0;
    cv::Mat image;
    while (true) {
        bool success = vidObj.read(image);
        if (!success) break;
        cv::imwrite("frames/frame" + std::to_string(count) + ".jpg", image);
        count++;
    }
}

bool fileExists(const std::string& path) {
    struct stat buffer;
    return (stat(path.c_str(), &buffer) == 0);
}

std::string runner(const std::string& path) {
    if (!fileExists(path)) {
        std::cout << "Start frame capture." << std::endl;
        framecapture();
    } else {
        std::cout << "Image found in " << path << "." << std::endl;
        cv::Mat image = cv::imread(path);
        if (image.empty()) {
            std::cout << "Image not found in " << path << "." << std::endl;
            return "";
        }
        return doProcess(image);
    }
    return "";
}

int main() {
    std::vector<std::string> frames;

    for (int i = 0; i <= CLIP_FRAMES / 4; ++i) {
        try {
            std::string path = "frames/frame" + std::to_string(i * 4) + ".jpg";
            std::string frame = runner(path);
            if (!frame.empty()) frames.push_back(frame);
        } catch (...) {
            // Exception handling
        }
    }

    int i = 0;

    while (i < frames.size() - 1)
    {
        system("clear");
        std::cout << frames[i] << std::endl;
        i++;
        usleep(130000);// 130000 microseconds = 0.13 seconds per frame (approximately) 

    }
    return 0;
}
