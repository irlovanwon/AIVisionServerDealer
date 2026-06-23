/*
 * Copyright(c) 2026-2030, VIATECH & UZONE All rights reserved
 * Des: ResultPublisher — API2b ZMQ PUB for publishing detection results
 * Date: 20260614
 * Modification:
 */
#pragma once

#include "ai_vision/common/Types.h"
#include "ai_vision/data/ConfigManager.h"
#include <string>
#include <atomic>

namespace ai_vision {

class ResultPublisher {
public:
    ResultPublisher();
    ~ResultPublisher();

    bool bind(const std::string& endpoint);
    void unbind();

    void publish_result(const std::string& transaction_id,
                        const std::string& dealer_id,
                        const nlohmann::json& result_json);

private:
    void* zmq_context_ = nullptr;
    void* zmq_socket_ = nullptr;
    std::string endpoint_;
    std::atomic<bool> bound_{false};
};

} // namespace ai_vision
