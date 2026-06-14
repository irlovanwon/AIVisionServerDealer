/*
 * Copyright(c) 2026-2030, VIATECH & UZONE All rights reserved
 * Des: DetectionDealer — API1a ZMQ DEALER/DEALER for AI detection
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

class DetectionDealer {
public:
    using ResponseCallback = std::function<void(const DetectionResponse&)>;

    DetectionDealer();
    ~DetectionDealer();

    bool connect(const Api1aConfig& config);
    void disconnect();

    void set_response_callback(ResponseCallback cb);

    bool send_request(const DetectionRequest& request);

private:
    void poll_thread_func();

    void* zmq_context_ = nullptr;
    void* zmq_socket_ = nullptr;
    std::atomic<bool> connected_{false};
    std::atomic<bool> running_{false};
    std::thread poll_thread_;
    ResponseCallback callback_;
    int poll_timeout_ms_ = 100;
};

} // namespace ai_vision
