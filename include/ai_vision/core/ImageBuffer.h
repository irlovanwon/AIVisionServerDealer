/*
 * Copyright(c) 2026-2030, VIATECH & UZONE All rights reserved
 * Des: Thread-safe image buffer — latest-frame-wins with shared_ptr
 * Date: 20260614
 * Modification:
 */
#pragma once

#include "ai_vision/common/Types.h"
#include <mutex>
#include <condition_variable>
#include <deque>
#include <atomic>

namespace ai_vision {

class ImageBuffer {
public:
    explicit ImageBuffer(size_t max_size = 20);

    void push(std::shared_ptr<ImageData> img);
    std::shared_ptr<ImageData> pop(int timeout_ms);
    std::shared_ptr<ImageData> try_pop();
    size_t size();

private:
    std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<std::shared_ptr<ImageData>> queue_;
    size_t max_size_;
    std::atomic<size_t> dropped_{0};
};

} // namespace ai_vision
