/*
 * Copyright(c) 2026-2030, VIATECH & UZONE All rights reserved
 * Des: ImageSubscriber — API2a ZMQ SUB for image ingest from data server
 * Date: 20260614
 * Modification:
 */
#pragma once

#include "ai_vision/common/Types.h"
#include "ai_vision/data/ConfigManager.h"
#include <string>
#include <atomic>
#include <thread>
#include <functional>

namespace ai_vision {

class ImageSubscriber {
public:
    using ImageCallback = std::function<void(std::shared_ptr<ImageData>)>;

    ImageSubscriber();
    ~ImageSubscriber();

    bool connect(const Api2aConfig& config);
    void disconnect();

    void set_callback(ImageCallback cb);

private:
    void subscribe_thread_func(const std::string& channel, const std::string& endpoint);

    void* zmq_context_ = nullptr;
    std::atomic<bool> connected_{false};
    std::atomic<bool> running_{false};
    std::vector<std::thread> subscribe_threads_;
    ImageCallback callback_;
};

} // namespace ai_vision
