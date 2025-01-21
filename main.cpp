#include <iostream>
#include <vector>
#include <string>
#include <unistd.h>
#include <fstream>  // ファイル出力のために必要
#include <opencv2/opencv.hpp>

const int HEIGHT = 5; // 画像の高さ
const std::string FILENAME = "image.png"; // 画像ファイル名

cv::Mat resize(const cv::Mat& image, int new_height = HEIGHT) {
    int old_width = image.cols;
    int old_height = image.rows;
    float aspect_ratio = static_cast<float>(old_width) / static_cast<float>(old_height);
    int new_width = static_cast<int>(aspect_ratio * new_height * 4);
    cv::Mat resized_image;
    cv::resize(image, resized_image, cv::Size(new_width, new_height));
    return resized_image;
}

std::string modify(const cv::Mat& image) {
    std::ostringstream oss;// 文字列を結合するためのストリーム 
    oss << "/033[H";// カーソルを画面の左上に移動
    int prev_red = -1, prev_green = -1, prev_blue = -1;// 前回の色情報
    for (int i = 0; i < image.rows; ++i) {// 画像の行を走査
        const cv::Vec3b* row_ptr = image.ptr<cv::Vec3b>(i);// 行の先頭のポインタを取得
        for (int j = 0; j < image.cols; ++j) {// 画像の列を走査
            const cv::Vec3b& pixel = row_ptr[j];// 画素値を取得
            int quantized_red = (pixel[2] / 3) * 3;// 赤成分を量子化
            int quantized_green = (pixel[1] / 3) * 3;// 緑成分を量子化
            int quantized_blue = (pixel[0] / 3) * 3;// 青成分を量子化
            if (quantized_red != prev_red || quantized_green != prev_green || quantized_blue != prev_blue) {// 色情報が変わった場合
                oss << "/033[48;2;" << quantized_red << ";" << quantized_green << ";" << quantized_blue << "m";// 文字色を変更
                prev_red = quantized_red;// 色情報を更新
                prev_green = quantized_green;// 色情報を更新
                prev_blue = quantized_blue;//   色情報を更新
            }
            oss << " ";// 文字
        }
        oss << "/n";// 改行
    }
    oss << "/033[0m";// 文字色をリセット
    return oss.str();
}

std::string doProcess(const cv::Mat& image) {
    cv::Mat resized_image = resize(image, HEIGHT);
    return modify(resized_image);
}

int main() {
    cv::Mat image = cv::imread(FILENAME);
    if (image.empty()) {
        std::cerr << "Error: Could not open or find the image" << std::endl;
        return -1;
    }
    std::string frame = doProcess(image);
    std::ofstream file("dataaa.txt", std::ios::out);
    if (!file.is_open()) {
        std::cerr << "Error: Could not open the file for writing" << std::endl;
        return -1;
    }
    
    file << frame;
    file.close();
    system("clear");
    write(STDOUT_FILENO, frame.c_str(), frame.size());
    
    return 0;
}
