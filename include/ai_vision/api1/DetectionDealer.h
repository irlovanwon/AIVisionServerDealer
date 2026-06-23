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
    bool send_request_with_data(const DetectionRequest& request, const PayloadPtr& payload);
    bool is_client_connected() const { return client_connected_.load(); }

private:
    void poll_thread_func();
    void monitor_thread_func(const std::string& monitor_addr);

    void* zmq_context_ = nullptr;
    void* zmq_socket_ = nullptr;
    std::atomic<bool> connected_{false};
    std::atomic<bool> running_{false};
    std::atomic<bool> client_connected_{false};
    std::thread poll_thread_;
    std::thread monitor_thread_;
    ResponseCallback callback_;
    std::string monitor_addr_;
    int poll_timeout_ms_ = 100;
};

} // namespace ai_vision
