/*
 * Copyright(c) 2026-2030, VIATECH & UZONE All rights reserved
 * Des: CoreModule — orchestrates ingest pipeline, detection, result forwarding
 *      processing_paused_ flow control, dedicated result thread
 * Date: 20260623
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
#include <condition_variable>
#include <queue>

namespace ai_vision {

class ImageBuffer;
class ImageSubscriber;
class DetectionDealer;
class ResultPublisher;

struct ResultEntry {
    std::string transaction_id;
    std::string dealer_id;
    nlohmann::json result_json;
};

class CoreModule {
public:
    CoreModule();
    ~CoreModule();

    void set_config(const Config& config);
    void set_image_buffer(std::shared_ptr<ImageBuffer> buffer);
    void set_detection_dealer(std::shared_ptr<DetectionDealer> dealer);
    void set_result_publisher(std::shared_ptr<ResultPublisher> publisher);
    void set_image_save_path(const std::string& path) { image_save_path_ = path; }
    void set_http_base_url(const std::string& url) { http_base_url_ = url; }

    bool start();
    void stop();

    std::string generate_transaction_id();

    void set_paused(bool paused) { processing_paused_.store(paused); }
    void ack_results(size_t count);
    bool is_paused() const { return processing_paused_.load(); }
    size_t pending_results() const { return pending_results_.load(); }

private:
    void pipeline_thread_func();
    void result_thread_func();
    void enqueue_result(const std::string& txn, const std::string& dealer_id,
                        const nlohmann::json& json);

    Config config_;
    std::shared_ptr<ImageBuffer> buffer_;
    std::shared_ptr<DetectionDealer> dealer_;
    std::shared_ptr<ResultPublisher> publisher_;

    std::atomic<bool> running_{false};
    std::thread pipeline_thread_;
    std::thread result_thread_;
    std::mutex mutex_;
    uint64_t transaction_counter_ = 0;
    std::chrono::steady_clock::time_point last_no_client_publish_;

    std::atomic<bool> processing_paused_{false};
    std::atomic<size_t> pending_results_{0};
    size_t max_pending_results_ = 10;

    std::string image_save_path_ = "/tmp/ai_vision_images";
    std::string http_base_url_;

    std::queue<ResultEntry> result_queue_;
    std::mutex result_mutex_;
    std::condition_variable result_cv_;
};

} // namespace ai_vision
