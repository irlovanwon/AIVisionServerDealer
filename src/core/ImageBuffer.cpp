/*
 * Copyright(c) 2026-2030, VIATECH & UZONE All rights reserved
 * Des: Thread-safe image buffer — bounded queue with drop-oldest
 * Date: 20260614
 * Modification:
 */
#include "ai_vision/core/ImageBuffer.h"
#include "ai_vision/common/Logger.h"

namespace ai_vision {

ImageBuffer::ImageBuffer(size_t max_size) : max_size_(max_size) {}

void ImageBuffer::push(std::shared_ptr<ImageData> img) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (queue_.size() >= max_size_) {
        queue_.pop_front();
        dropped_++;
    }
    queue_.push_back(img);
    cv_.notify_one();
}

std::shared_ptr<ImageData> ImageBuffer::pop(int timeout_ms) {
    std::unique_lock<std::mutex> lock(mutex_);
    if (queue_.empty()) {
        cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                     [this]() { return !queue_.empty(); });
    }
    if (queue_.empty()) return nullptr;
    auto img = queue_.front();
    queue_.pop_front();
    return img;
}

std::shared_ptr<ImageData> ImageBuffer::try_pop() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (queue_.empty()) return nullptr;
    auto img = queue_.front();
    queue_.pop_front();
    return img;
}

size_t ImageBuffer::size() {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size();
}

} // namespace ai_vision
