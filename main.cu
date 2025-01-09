#include <opencv2/opencv.hpp>
#include <opencv2/cudaimgproc.hpp>
#include <opencv2/cudawarping.hpp>
#include <cuda_runtime.h>
#include <SFML/Audio.hpp>
#include <thread>
#include <chrono>
#include <sstream>
#include <mutex>
#include <iostream>
#include <vector>
#include <string>
#include <unistd.h>
#include <ctime>

constexpr float volume = 70.0f;
constexpr float speed = 1.0f;
constexpr int fps_value = 1;
constexpr int HEIGHT = 240; // 画像の高さ
constexpr float sleep_value = 2; //待機時間
const std::string FILENAME = "idol.webm";

__global__ void quantize_kernel(const cv::cuda::PtrStepSz<uchar3> input_image, int* output_quantized) {
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;

    if (x < input_image.cols && y < input_image.rows) {
        uchar3 pixel = input_image(y, x);

        int quantized_red = (pixel.x / 3) * 3;
        int quantized_green = (pixel.y / 3) * 3;
        int quantized_blue = (pixel.z / 3) * 3;

        output_quantized[3 * (y * input_image.cols + x) + 0] = quantized_red;
        output_quantized[3 * (y * input_image.cols + x) + 1] = quantized_green;
        output_quantized[3 * (y * input_image.cols + x) + 2] = quantized_blue;
    }
}


cv::cuda::GpuMat resize_cuda(const cv::cuda::GpuMat& d_image) {
    int old_width = d_image.cols;
    int old_height = d_image.rows;
    float aspect_ratio = static_cast<float>(old_width) / static_cast<float>(old_height);
    int new_width = static_cast<int>(aspect_ratio * HEIGHT * 2.5);
    cv::cuda::GpuMat d_resized_image;
    cv::cuda::resize(d_image, d_resized_image, cv::Size(new_width, HEIGHT));
    return d_resized_image;
}

std::string modify_cuda(const cv::cuda::GpuMat& d_image) {
    int* d_quantized;
    cudaMalloc(&d_quantized, d_image.rows * d_image.cols * 3 * sizeof(int));    // 結果を格納するGPUメモリを確保
    dim3 blockSize(16, 16);
    dim3 gridSize((d_image.cols + blockSize.x - 1) / blockSize.x, 
                  (d_image.rows + blockSize.y - 1) / blockSize.y);
    quantize_kernel<<<gridSize, blockSize>>>(d_image, d_quantized);
    int* h_quantized = new int[d_image.rows * d_image.cols * 3];
    cudaMemcpy(h_quantized, d_quantized, d_image.rows * d_image.cols * 3 * sizeof(int), cudaMemcpyDeviceToHost);

    std::ostringstream oss;
    oss << "\033[H";
    int prev_red = -1, prev_green = -1, prev_blue = -1;
    for (int i = 0; i < d_image.rows; ++i) {
        for (int j = 0; j < d_image.cols; ++j) {
            int idx = 3 * (i * d_image.cols + j);
            int quantized_red = h_quantized[idx + 0];
            int quantized_green = h_quantized[idx + 1];
            int quantized_blue = h_quantized[idx + 2];
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
    cudaFree(d_quantized);
    delete[] h_quantized;

    return oss.str();
}

std::string doProcess(const cv::Mat& image) {
    cv::cuda::GpuMat d_image;
    d_image.upload(image);
    cv::cuda::GpuMat d_resized_image;
    d_resized_image = resize_cuda(d_image);
    std::string result = modify_cuda(d_resized_image);
    return result;
}

int main() {
    std::string commands = "ffmpeg -y -hwaccel cuda -i " + FILENAME + " -vn output.wav";
    std::thread th([&commands] {
        system(commands.c_str());
    });
    cv::VideoCapture vidObj(FILENAME);
    std::ios_base::sync_with_stdio(false);
    std::cin.tie(nullptr);
    if (!vidObj.isOpened()) {
        std::cerr << "Error: Video file not opened" << std::endl;
        return 1;
    }
    std::vector<std::string> frames;
    std::mutex frames_mutex;
    cv::Mat image;
    FILE *fp;
    int frame_count = static_cast<int>(vidObj.get(cv::CAP_PROP_FRAME_COUNT));
    fp = fopen("output.txt", "w");
    std::thread cv_thred([&frame_count, &frames, &vidObj, &image, &frames_mutex, &fp](){
        for (size_t i = 0; i < frame_count; i += fps_value){
            fprintf(fp, "frame_count = %ld\n", i);
            if(!vidObj.read(image))break;
            std::string frame = doProcess(image);
            if (!frame.empty()) {
                std::lock_guard<std::mutex> lock(frames_mutex);
                frames.emplace_back(frame);
            }
            for (int j = 1; j < fps_value; j++) {
                if (!vidObj.grab()) break;
            }
            fprintf(fp, "frames.size() = %ld\n",frames.size());
        }
        vidObj.release();
        fprintf(fp, "end_cv2\n");
    });
    th.join();
    while (frames.size() < (frame_count / fps_value) / sleep_value) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    system("clear");
    float fps = vidObj.get(cv::CAP_PROP_FPS) / fps_value * speed;
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
        for (size_t i = 0; i < ((frame_count / fps_value) -2); ++i) {
            auto current_time = std::chrono::high_resolution_clock::now();
            std::chrono::duration<double> elapsed_time = current_time - start_time;
            int expected_frame_index = static_cast<int>(elapsed_time.count() * fps);
            std::lock_guard<std::mutex> lock(frames_mutex);
            while (i < expected_frame_index && i < (frame_count / fps_value) && i < frames.size()) {
                ++i;
            }
            auto frame_start_time = std::chrono::high_resolution_clock::now();
            {
                if (i < frames.size() && !frames[i].empty()) {
                    write(STDOUT_FILENO, frames[i].c_str(), frames[i].size());
                    frames[i].clear();
                    frames[i].shrink_to_fit();
                } else {
                    fprintf(fp, "frame = %ld, frames.size() = %ld\n", i, frames.size());
                }
            }
            auto frame_end_time = std::chrono::high_resolution_clock::now();
            std::chrono::duration<double> processing_time = frame_end_time - frame_start_time;
            double sleep_time = (1.0 / fps) - processing_time.count();
            if (sleep_time > 0) {
                std::this_thread::sleep_for(std::chrono::duration<double>(sleep_time));
            }
            fprintf(fp, "display_frame = %ld, processing_time = %f, sleep_time = %f, frames.size - i = %ld\n", i, processing_time.count(), sleep_time, frames.size() - i);
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