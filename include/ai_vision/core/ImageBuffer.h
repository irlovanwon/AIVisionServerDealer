/*
 * Copyright(c) 2026-2030, VIATECH & UZONE All rights reserved
 * Des: Lock-free SPSC ring buffer — drop-newest policy, condition_variable for blocking pop
 * Date: 20260623
 * Modification:
 */
#pragma once

#include "ai_vision/common/Types.h"
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <vector>

namespace ai_vision {

class ImageBuffer {
public:
    explicit ImageBuffer(size_t max_size = 20);

    bool push(std::shared_ptr<ImageData> img);
    std::shared_ptr<ImageData> pop(int timeout_ms);
    std::shared_ptr<ImageData> try_pop();
    size_t size();
    size_t dropped() const { return dropped_.load(std::memory_order_relaxed); }

private:
    size_t capacity_;
    std::vector<std::shared_ptr<ImageData>> buffer_;

    alignas(64) std::atomic<size_t> write_pos_{0};
    alignas(64) std::atomic<size_t> read_pos_{0};

    std::mutex cv_mutex_;
    std::condition_variable cv_;
    std::atomic<size_t> dropped_{0};
};

} // namespace ai_vision
