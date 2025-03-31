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

constexpr float volume = 30.0f;
constexpr float speed = 1.0f;
constexpr int HEIGHT = 251; // 画像の高さ
// constexpr int HEIGHT = 123; // 画像の高さ
constexpr float sleep_value = -1; //待機時間
const std::string FILENAME = "over.webm"; // 動画ファイル名

cv::Mat resize(const cv::Mat& image, int new_height = HEIGHT) {
    int old_width = image.cols;
    int old_height = image.rows;
    float aspect_ratio = static_cast<float>(old_width) / static_cast<float>(old_height);
    int new_width = static_cast<int>(aspect_ratio * new_height * 2.76);//2.76
    cv::Mat resized_image;
    cv::resize(image, resized_image, cv::Size(new_width, new_height));
    return resized_image;
}

void processRow(const cv::Mat& image, int row, std::vector<std::string>& output, std::mutex& mutex) {
    std::ostringstream oss;
    const cv::Vec3b* row_ptr = image.ptr<cv::Vec3b>(row);
    int prev_red = -1, prev_green = -1, prev_blue = -1;
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
    
    std::lock_guard<std::mutex> lock(mutex);
    output[row] = oss.str();
}

std::string modify(const cv::Mat& image) {
    std::vector<std::string> output(image.rows);
    std::vector<std::thread> threads;
    std::mutex mutex;
    
    for (int i = 0; i < image.rows; ++i) {
        threads.emplace_back(processRow, std::cref(image), i, std::ref(output), std::ref(mutex));
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    std::ostringstream final_output;
    final_output << "\033[H";
    for (const auto& line : output) {
        final_output << line;
    }
    final_output << "\033[0m";
    return final_output.str();
}

int main() {
    std::string commands = "ffmpeg -y -i " + FILENAME + " -vn output.wav";
    std::thread t([&commands](){
        system(commands.c_str());
    });
    cv::VideoCapture vidObj(FILENAME);
    std::ios_base::sync_with_stdio(false);
    std::cin.tie(nullptr);
    if (!vidObj.isOpened()) {
        std::cerr << "Error: Could not open file" << std::endl;
        return -1;
    }
    std::vector<std::string> frames;    
    std::mutex frames_mutex;
    int frame_count = static_cast<int>(vidObj.get(cv::CAP_PROP_FRAME_COUNT));
    cv::Mat image;
    FILE *fp;
    fp = fopen("output.txt", "w");
    std::thread cv_thred([&frame_count, &frames, &vidObj, &image, &frames_mutex, &fp](){
        for (size_t i = 0; i < frame_count; ++i) {
            auto start_time = std::chrono::high_resolution_clock::now();
            if (!vidObj.read(image)) break;
            cv::Mat resized_image = resize(image);
            std::string frame = modify(resized_image);
            if (!frame.empty()) {
                std::lock_guard<std::mutex> lock(frames_mutex);
                frames.push_back(frame);
            }
            auto end_time = std::chrono::high_resolution_clock::now();
            std::chrono::duration<double> elapsed_time = end_time - start_time;
            fprintf(fp, "frame = %ld, elapsed_time = %f,frame_size = %ld\n", i, elapsed_time.count(),frame.size());
        }
        vidObj.release();
        fprintf(fp, "end_cv2\n");
    });

    t.join();
    if (sleep_value > 0) {
        while (frames.size() < frame_count / sleep_value) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    system("clear");
    float fps = vidObj.get(cv::CAP_PROP_FPS) * speed;
    sf::Music music;
    if (!music.openFromFile("output.wav")) {
        std::cerr << "Error loading audio file" << std::endl;
        return -1;
    }
    music.setPitch(speed);
    music.setVolume(volume);
    music.play();
    auto start_time = std::chrono::high_resolution_clock::now();
    std::thread display_thread([&frames, &start_time, &frames_mutex, fps, frame_count, &fp]() {
        int max_frame = frame_count - 2;
        double sleep = 1.0 / fps;
        for (size_t i = 0; i < max_frame; ++i) {
            auto frame_start_time = std::chrono::high_resolution_clock::now();
            auto current_time = std::chrono::high_resolution_clock::now();
            std::chrono::duration<double> elapsed_time = current_time - start_time;
            int expected_frame_index = static_cast<int>(elapsed_time.count() * fps);
            while (i < expected_frame_index && i < frame_count && i < frames.size()) {
                ++i;
            }
            {
                if (i < frames.size() && !frames[i].empty()) {
                    write(STDOUT_FILENO, frames[i].c_str(), frames[i].size());

                } else {
                    fprintf(fp, "frame = %ld, frames.size() = %ld\n", i, frames.size());
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
                fprintf(fp, "display_frame = %ld, processing_time = %f, sleep_time = %f, frames.size - i = %ld\n", i, processing_time.count(), sleep_time, frames.size() - i);
                std::this_thread::sleep_for(std::chrono::duration<double>(sleep_time));
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
