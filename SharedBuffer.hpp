#pragma once
// speed_terminal (/home/kazuma1112/speed_terminal/SharedBuffer.hpp) と
// バイナリレイアウトを完全一致させること。改変する場合は両リポジトリで同期する。

#include <cstdint>
#include <atomic>
#include <string>
#include <stdexcept>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

// Define 16-byte aligned ScreenCell as requested
struct alignas(16) ScreenCell {
    uint32_t fg_rgba;   // Foreground (RGBA 8-bit x 4)
    uint32_t bg_rgba;   // Background (RGBA 8-bit x 4)
    uint32_t codepoint; // Unicode codepoint (UTF-8 compatible)
    uint32_t reserved;  // 16-byte boundary padding
};

// Double-buffering/Triple-buffering header
struct alignas(64) SharedHeader {
    uint32_t width;
    uint32_t height;
    std::atomic<int> latest_buffer;  // Index of latest complete buffer (0, 1, or 2)
    std::atomic<uint64_t> sequence;   // Frame sequence number (incremented on publish)
    std::atomic<bool> is_writer_active;
    std::atomic<bool> is_viewer_active;
};

// RAII wrapper for POSIX Shared Memory
class SharedMemoryBuffer {
private:
    std::string shm_name_;
    size_t size_;
    int fd_;
    void* mmapped_addr_;
    bool is_creator_;

public:
    // Constructor for Creator (e.g. TestWriter)
    SharedMemoryBuffer(const std::string& name, uint32_t width, uint32_t height)
        : shm_name_(name), fd_(-1), mmapped_addr_(nullptr), is_creator_(true) {

        size_ = sizeof(SharedHeader) + 3 * width * height * sizeof(ScreenCell);

        // Remove old shm if it exists
        shm_unlink(shm_name_.c_str());

        fd_ = shm_open(shm_name_.c_str(), O_RDWR | O_CREAT | O_EXCL, 0666);
        if (fd_ < 0) {
            throw std::runtime_error("Failed to create shared memory: shm_open failed");
        }

        if (ftruncate(fd_, size_) < 0) {
            close(fd_);
            shm_unlink(shm_name_.c_str());
            throw std::runtime_error("Failed to resize shared memory: ftruncate failed");
        }

        mmapped_addr_ = mmap(nullptr, size_, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
        if (mmapped_addr_ == MAP_FAILED) {
            close(fd_);
            shm_unlink(shm_name_.c_str());
            throw std::runtime_error("Failed to mmap shared memory");
        }

        // Initialize header
        SharedHeader* header = get_header();
        header->width = width;
        header->height = height;
        header->latest_buffer.store(0, std::memory_order_relaxed);
        header->sequence.store(0, std::memory_order_relaxed);
        header->is_writer_active.store(true, std::memory_order_release);
        header->is_viewer_active.store(false, std::memory_order_release);
    }

    // Constructor for Reader (e.g. Viewer) - Auto-detects size
    SharedMemoryBuffer(const std::string& name)
        : shm_name_(name), fd_(-1), mmapped_addr_(nullptr), is_creator_(false) {

        fd_ = shm_open(shm_name_.c_str(), O_RDWR, 0666);
        if (fd_ < 0) {
            throw std::runtime_error("Failed to open shared memory: shm_open failed. Is the writer running?");
        }

        // Map just the header first to read dimensions
        void* temp_header = mmap(nullptr, sizeof(SharedHeader), PROT_READ, MAP_SHARED, fd_, 0);
        if (temp_header == MAP_FAILED) {
            close(fd_);
            throw std::runtime_error("Failed to map shared memory header for reading size");
        }

        SharedHeader* sh = reinterpret_cast<SharedHeader*>(temp_header);
        uint32_t w = sh->width;
        uint32_t h = sh->height;
        munmap(temp_header, sizeof(SharedHeader));

        size_ = sizeof(SharedHeader) + 3 * w * h * sizeof(ScreenCell);
        mmapped_addr_ = mmap(nullptr, size_, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
        if (mmapped_addr_ == MAP_FAILED) {
            close(fd_);
            throw std::runtime_error("Failed to mmap full shared memory");
        }
    }

    ~SharedMemoryBuffer() {
        if (mmapped_addr_ && mmapped_addr_ != MAP_FAILED) {
            munmap(mmapped_addr_, size_);
        }
        if (fd_ >= 0) {
            close(fd_);
        }
        if (is_creator_) {
            shm_unlink(shm_name_.c_str());
        }
    }

    // Disable copy constructors to prevent double-free
    SharedMemoryBuffer(const SharedMemoryBuffer&) = delete;
    SharedMemoryBuffer& operator=(const SharedMemoryBuffer&) = delete;

    SharedHeader* get_header() {
        return reinterpret_cast<SharedHeader*>(mmapped_addr_);
    }

    ScreenCell* get_buffer(int idx) {
        if (idx < 0 || idx >= 3) return nullptr;
        SharedHeader* h = get_header();
        uint8_t* base = reinterpret_cast<uint8_t*>(mmapped_addr_) + sizeof(SharedHeader);
        size_t buf_size = h->width * h->height * sizeof(ScreenCell);
        return reinterpret_cast<ScreenCell*>(base + idx * buf_size);
    }

    size_t get_size() const { return size_; }
};
