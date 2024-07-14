#include <iostream>
#include <vector>
#include <string>
#include <unistd.h>
#include <opencv2/opencv.hpp>

const std::vector<std::string> GRADIENT_CHARS = {"█", "▓", "▒", "░", " "};
const int HEIGHT = 130; // 画像の高さ

cv::Mat resize(const cv::Mat& image, int new_height = HEIGHT) {
    int old_width = image.cols;
    int old_height = image.rows;
    float aspect_ratio = static_cast<float>(old_width) / static_cast<float>(old_height);
    int new_width = static_cast<int>(aspect_ratio * new_height * 4);
    cv::Mat resized_image;
    cv::resize(image, resized_image, cv::Size(new_width, new_height));
    return resized_image;
}


std::string modify(const cv::Mat& image, int buckets = 25) {
    std::string new_pixels;
    new_pixels += "\033[H\033[J";
    new_pixels.reserve(image.rows * image.cols * 13); // Pre-allocate memory
    for (int i = 0; i < image.rows; i++) {
        for (int j = 0; j < image.cols; j++) {
            cv::Vec3b pixel = image.at<cv::Vec3b>(i, j);
            int blue = pixel[0];
            int green = pixel[1];
            int red = pixel[2];
            int gray = (red + green + blue) / 3;
            int color_index = gray / buckets;
            if (color_index >= GRADIENT_CHARS.size()) {
                color_index = GRADIENT_CHARS.size() - 1; // Ensure the index is within bounds
            }            
            new_pixels += "\033[38;2;" + std::to_string(red) + ";" + std::to_string(green) + ";" + std::to_string(blue) + "m" +
                          "\033[48;2;" + std::to_string(red) + ";" + std::to_string(green) + ";" + std::to_string(blue) + "m" +
                          GRADIENT_CHARS[color_index];
        }
        new_pixels += "\n";
    }
    return new_pixels + "\033[0m"; // Reset color at the end
}

std::string doProcess(const cv::Mat& image) {
    cv::Mat resized_image = resize(image, HEIGHT);
    return modify(resized_image);
}

int main() {
    const std::string FILENAME = "image.JPG"; // 画像ファイル名
    cv::Mat image = cv::imread(FILENAME);

    if (image.empty()) {
        std::cerr << "Error: Could not open or find the image" << std::endl;
        return -1;
    }

    std::string frame = doProcess(image);
    system("clear");
    write(STDOUT_FILENO, frame.c_str(), frame.size());
    std::cout << "Image displayed" << std::endl;
    
    return 0;
}
