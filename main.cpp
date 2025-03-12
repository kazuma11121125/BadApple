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
constexpr int HEIGHT = 135;
constexpr int fps_value = 1;

cv::Mat resize(const cv::Mat& image, int new_height = HEIGHT) {
    int old_width = image.cols;
    int old_height = image.rows;
    float aspect_ratio = static_cast<float>(old_width) / static_cast<float>(old_height);
    int new_width = static_cast<int>(aspect_ratio * new_height * 2.65);
    cv::Mat resized_image;
    cv::resize(image, resized_image, cv::Size(new_width, new_height));
    return resized_image;
}

cv::Mat grayscalify(const cv::Mat& image, double alpha = 1.75, int beta = 0) {
    /*
    alpha: contrast control [1.0-3.0]
    beta: brightness control [0-100]
    */
    cv::Mat gray_image;
    cv::cvtColor(image, gray_image, cv::COLOR_BGR2GRAY);
    cv::Mat adjusted_image;
    gray_image.convertTo(adjusted_image, -1, alpha, beta);
    return adjusted_image;
}

std::string modify(const cv::Mat& image, int buckets = 25) {
    std::ostringstream oss;
    oss << "\033[H";\
    for (int i = 0; i < image.rows; ++i) {
        for (int j = 0; j < image.cols; ++j) {
            int pixel_value = image.at<uchar>(i, j);
            oss << ASCII_CHARS[pixel_value / buckets];
        }
        oss << '\n';
    }
    return oss.str();
}

std::string doProcess(const cv::Mat& image) {
    cv::Mat resized_image = resize(image, HEIGHT);
    cv::Mat gray_image = grayscalify(resized_image);
    return modify(gray_image);
}

int main() {
    //camera
    cv::VideoCapture vidObj(0);
    std::vector<std::string> frames;
    std::mutex frames_mutex;
    cv::Mat image;
    FILE *fp;
    fp = fopen("output.txt", "w");
    std::thread cv_thred([&frames, &vidObj, &image, &frames_mutex, &fp](){
        int i = 0;
        while(true) {
            auto start_time = std::chrono::high_resolution_clock::now();
            if (vidObj.read(image)){
                std::string frame = doProcess(image);
                if (!frame.empty()) {
                    std::lock_guard<std::mutex> lock(frames_mutex);
                    frames.push_back(frame);
                }
                auto end_time = std::chrono::high_resolution_clock::now();
                std::chrono::duration<double> elapsed_time = end_time - start_time;
                fprintf(fp, "frame = %d, elapsed_time = %f,frame_size = %ld\n", i, elapsed_time.count(), frame.size());
                i++;
            }
        }
        vidObj.release();
    });
    system("clear");
    float fps = vidObj.get(cv::CAP_PROP_FPS) / fps_value;
    system("clear");
    auto start_time = std::chrono::high_resolution_clock::now();
    std::thread display_thread([&frames, &start_time, &frames_mutex, fps, &fp]() {
        int i = 0;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        while (true) {
            auto current_time = std::chrono::high_resolution_clock::now();
            std::chrono::duration<double> elapsed_time = current_time - start_time;
            int expected_frame_index = static_cast<int>(elapsed_time.count() * fps);
            auto frame_start_time = std::chrono::high_resolution_clock::now();
            {
                if (i < frames.size() && !frames[i].empty()) {
                    write(STDOUT_FILENO, frames[i].c_str(), frames[i].size());


                } else {
                    fprintf(fp, "frame = %d, frames.size() = %ld\n", i, frames.size());
                    i--;
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
                std::this_thread::sleep_for(std::chrono::duration<double>(sleep_time));
            }
            fprintf(fp, "display_frame = %d, processing_time = %f, sleep_time = %f, frames.size - i = %ld\n", i, processing_time.count(), sleep_time, frames.size() - i);
            i++;
        }
    });
    display_thread.join();
    cv_thred.join();
    system("clear");
    printf("end_display\n");
    fprintf(fp, "end\n");
    fclose(fp);
    return 0;
}