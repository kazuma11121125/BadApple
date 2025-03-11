#include <iostream>
#include <vector>
#include <string>
#include <unistd.h>
#include <ctime>
#include <opencv2/opencv.hpp>
#include <thread>
#include <chrono>
#include <sstream>
#include <mutex>

constexpr float volume = 30.0f;
// constexpr int HEIGHT = 251; // 画像の高さ
constexpr int HEIGHT = 100; // 画像の高さ
constexpr float sleep_value = -1; //待機時間
cv::Mat resize(const cv::Mat& image, int new_height = HEIGHT) {
    int old_width = image.cols;
    int old_height = image.rows;
    float aspect_ratio = static_cast<float>(old_width) / static_cast<float>(old_height);
    int new_width = static_cast<int>(aspect_ratio * new_height * 2);//2.76
    cv::Mat resized_image;
    cv::resize(image, resized_image, cv::Size(new_width, new_height));
    return resized_image;
}

std::string modify(const cv::Mat& image) {
    std::ostringstream oss;
    oss << "\033[H";
    int prev_red = -1, prev_green = -1, prev_blue = -1;
    for (int i = 0; i < image.rows; ++i) {
        const cv::Vec3b* row_ptr = image.ptr<cv::Vec3b>(i);
        for (int j = 0; j < image.cols; ++j) {
            const cv::Vec3b& pixel = row_ptr[j];
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

int main() {
    //cameraをcv2で開く
    cv::VideoCapture vidObj(0);
    double fps = vidObj.get(cv::CAP_PROP_FPS);
    fprintf(stderr, "fps = %f\n", fps);
    if (!vidObj.isOpened()) {
        std::cerr << "Error opening camera" << std::endl;
        return -1;
    }
    std::vector<std::string> frames;    
    std::mutex frames_mutex;
    cv::Mat image;
    FILE *fp;
    fp = fopen("output.txt", "w");
    fprintf(fp, "start\n");
    // カメラから画像を取得してdataにpng形式で保存

    std::thread cv_thred([&frames_mutex,&frames,&vidObj,&image,&fp](){
        while (true) {
            auto start_time = std::chrono::high_resolution_clock::now();
            //dataから画像を読み込む
            vidObj.read(image);
            if (!image.empty()) {
                std::string frame = modify(resize(image));
                if (!frame.empty()) {
                    std::lock_guard<std::mutex> lock(frames_mutex);
                    frames.push_back(frame);
                }
                auto end_time = std::chrono::high_resolution_clock::now();
                std::chrono::duration<double> elapsed_time = end_time - start_time;
                double sleep_time = sleep_value - elapsed_time.count();
                fprintf(fp, "cv_thread = %f\n", elapsed_time.count());
            }
        }
    });
    system("clear");
    printf("start_display\n");
    auto start_time = std::chrono::high_resolution_clock::now();
    // カメラからfpsを取得
    std::thread display_thread([&frames, &start_time, &frames_mutex, &fps,&fp]() {
        //500ms待機
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        double sleep = 1.0 / fps;
        int i = 0;
        while (true) {
            auto frame_start_time = std::chrono::high_resolution_clock::now();
            auto current_time = std::chrono::high_resolution_clock::now();
            std::chrono::duration<double> elapsed_time = current_time - start_time;
            int expected_frame_index = static_cast<int>(elapsed_time.count() * fps);
            {
                if (i < frames.size() && !frames[i].empty()) {
                    write(STDOUT_FILENO, frames[i].c_str(), frames[i].size());

                } else {
                    fprintf(fp, "frame = %d, frames.size() = %ld\n", i, frames.size());
                }
            }
            auto frame_end_time = std::chrono::high_resolution_clock::now();
            std::chrono::duration<double> processing_time = frame_end_time - frame_start_time;
            double sleep_time = sleep - processing_time.count();
            if (sleep_time > 0) {
                std::lock_guard<std::mutex> lock(frames_mutex);
                auto frame_clear_start = std::chrono::high_resolution_clock::now();
                frames[i].clear();
                frames[i].shrink_to_fit();
                auto frame_clear_end_time = std::chrono::high_resolution_clock::now();
                std::chrono::duration<double> frame_clear_time = frame_clear_end_time - frame_clear_start;
                sleep_time -= frame_clear_time.count();
                std::this_thread::sleep_for(std::chrono::duration<double>(sleep_time));
                fprintf(fp, "display_frame = %d, processing_time = %f, sleep_time = %f, frames.size - i = %ld\n", i, processing_time.count(), sleep_time, frames.size() - i);
            }
            i++;
        }
    });
    cv_thred.join();
    display_thread.join();
    system("clear");
    printf("end_display\n");
    fprintf(fp, "end\n");
    fclose(fp);
    return 0;
}
