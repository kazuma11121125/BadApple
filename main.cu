#include <opencv2/opencv.hpp>
#include <opencv2/cudaimgproc.hpp>
#include <opencv2/cudawarping.hpp>
#include <cuda_runtime.h>

constexpr float volume = 70.0f;
constexpr float speed = 1.0f;
constexpr int fps_value = 1;
constexpr int HEIGHT = 240; // 画像の高さ
constexpr float sleep_value = 4; //待機時間

__global__ void quantize_kernel(const cv::cuda::PtrStepSz<uchar3> input_image, int* output_quantized) {
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;

    if (x < input_image.cols && y < input_image.rows) {
        uchar3 pixel = input_image(y, x);
        int quantized_red = (pixel.x / 3) * 3;
        int quantized_green = (pixel.y / 3) * 3;
        int quantized_blue = (pixel.z / 3) * 3;

        int idx = 3 * (y * input_image.cols + x);
        output_quantized[idx + 0] = quantized_red;
        output_quantized[idx + 1] = quantized_green;
        output_quantized[idx + 2] = quantized_blue;
    }
}

cv::cuda::GpuMat resize_cuda(const cv::cuda::GpuMat& d_image) {
    int old_width = d_image.cols;
    int old_height = d_image.rows;
    float aspect_ratio = static_cast<float>(old_width) / static_cast<float>(old_height);
    int new_width = static_cast<int>(aspect_ratio * HEIGHT * 2.5);

    cv::cuda::GpuMat d_resized_image;
    cv::cuda::resize(d_image, d_resized_image, cv::Size(new_width, new_height));

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

int main() {
    cv::Mat image = cv::imread("test_image.jpg");
    if (image.empty()) {
        std::cerr << "画像の読み込みに失敗しました。" << std::endl;
        return -1;
    }
    cv::cuda::GpuMat d_image;
    d_image.upload(image);

    cv::cuda::GpuMat d_resized_image;
    d_resized_image = resize_cuda(d_image);
    std::string result = modify_cuda(d_resized_image);
    std::cout << result << std::endl;

    return 0;
}

