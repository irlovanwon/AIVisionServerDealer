/*
 * Copyright(c) 2026-2030, VIATECH & UZONE All rights reserved
 * Des: CoreModule — orchestrates ingest pipeline, detection, result forwarding
 * Date: 20260614
 * Modification:
 */
#pragma once

#include "ai_vision/common/Types.h"
#include "ai_vision/data/ConfigManager.h"
#include <memory>
#include <string>
#include <atomic>
#include <thread>
#include <mutex>
#include <unordered_map>

namespace ai_vision {

class ImageBuffer;
class ImageSubscriber;
class DetectionDealer;
class ResultPublisher;

class CoreModule {
public:
    CoreModule();
    ~CoreModule();

    void set_config(const Config& config);
    void set_image_buffer(std::shared_ptr<ImageBuffer> buffer);
    void set_detection_dealer(std::shared_ptr<DetectionDealer> dealer);
    void set_result_publisher(std::shared_ptr<ResultPublisher> publisher);

    bool start();
    void stop();

    std::string generate_transaction_id();

private:
    void pipeline_thread_func();

    Config config_;
    std::shared_ptr<ImageBuffer> buffer_;
    std::shared_ptr<DetectionDealer> dealer_;
    std::shared_ptr<ResultPublisher> publisher_;

    std::atomic<bool> running_{false};
    std::thread pipeline_thread_;
    std::mutex mutex_;
    uint64_t transaction_counter_ = 0;
    std::chrono::steady_clock::time_point last_no_client_publish_;
};

} // namespace ai_vision
