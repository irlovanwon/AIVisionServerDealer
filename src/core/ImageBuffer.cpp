/*
 * Copyright(c) 2026-2030, VIATECH & UZONE All rights reserved
 * Des: Lock-free SPSC ring buffer — drop-newest policy
 * Date: 20260623
 * Modification:
 */
#include "ai_vision/core/ImageBuffer.h"

namespace ai_vision {

ImageBuffer::ImageBuffer(size_t max_size)
    : capacity_(max_size < 1 ? 1 : max_size), buffer_(max_size < 1 ? 1 : max_size) {}

bool ImageBuffer::push(std::shared_ptr<ImageData> img) {
    size_t wp = write_pos_.load(std::memory_order_relaxed);
    size_t rp = read_pos_.load(std::memory_order_acquire);
    if (wp - rp >= capacity_) {
        dropped_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    buffer_[wp % capacity_] = std::move(img);
    write_pos_.store(wp + 1, std::memory_order_release);
    {
        std::lock_guard<std::mutex> lk(cv_mutex_);
    }
    cv_.notify_one();
    return true;
}

std::shared_ptr<ImageData> ImageBuffer::pop(int timeout_ms) {
    size_t rp = read_pos_.load(std::memory_order_relaxed);
    size_t wp = write_pos_.load(std::memory_order_acquire);
    if (rp < wp) {
        auto img = std::move(buffer_[rp % capacity_]);
        read_pos_.store(rp + 1, std::memory_order_release);
        return img;
    }
    std::unique_lock<std::mutex> lk(cv_mutex_);
    cv_.wait_for(lk, std::chrono::milliseconds(timeout_ms), [&] {
        wp = write_pos_.load(std::memory_order_acquire);
        return rp < wp;
    });
    if (rp < wp) {
        lk.unlock();
        auto img = std::move(buffer_[rp % capacity_]);
        read_pos_.store(rp + 1, std::memory_order_release);
        return img;
    }
    return nullptr;
}

std::shared_ptr<ImageData> ImageBuffer::try_pop() {
    size_t rp = read_pos_.load(std::memory_order_relaxed);
    size_t wp = write_pos_.load(std::memory_order_acquire);
    if (rp >= wp) return nullptr;
    auto img = std::move(buffer_[rp % capacity_]);
    read_pos_.store(rp + 1, std::memory_order_release);
    return img;
}

size_t ImageBuffer::size() {
    size_t wp = write_pos_.load(std::memory_order_acquire);
    size_t rp = read_pos_.load(std::memory_order_acquire);
    return wp - rp;
}

} // namespace ai_vision
