/*
 * Copyright(c) 2026-2030, VIATECH & UZONE All rights reserved
 * Des: CoreModule — orchestrates ingest pipeline, detection, result forwarding
 *      Raw data mode (Stream), processing_paused_ flow control
 * Date: 20260623
 * Modification:
 */
#include "ai_vision/core/CoreModule.h"
#include "ai_vision/core/ImageBuffer.h"
#include "ai_vision/api1/DetectionDealer.h"
#include "ai_vision/api2/ResultPublisher.h"
#include "ai_vision/common/Logger.h"
#include <fstream>

namespace ai_vision {

CoreModule::CoreModule() = default;

CoreModule::~CoreModule() {
    stop();
}

void CoreModule::set_config(const Config& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    config_ = config;
}

void CoreModule::set_image_buffer(std::shared_ptr<ImageBuffer> buffer) {
    buffer_ = buffer;
}

void CoreModule::set_detection_dealer(std::shared_ptr<DetectionDealer> dealer) {
    dealer_ = dealer;
}

void CoreModule::set_result_publisher(std::shared_ptr<ResultPublisher> publisher) {
    publisher_ = publisher;
}

std::string CoreModule::generate_transaction_id() {
    std::lock_guard<std::mutex> lock(mutex_);
    transaction_counter_++;
    return Timestamp::now().to_string() + "-" + std::to_string(transaction_counter_);
}

void CoreModule::enqueue_result(const std::string& txn, const std::string& dealer_id,
                                 const nlohmann::json& json) {
    {
        std::lock_guard<std::mutex> lk(result_mutex_);
        if (result_queue_.size() >= max_pending_results_) {
            result_queue_.pop();
            Logger::instance().warn("CoreModule",
                "Result queue full — dropping oldest result");
        }
        result_queue_.push({txn, dealer_id, json});
    }
    result_cv_.notify_one();
}

void CoreModule::ack_results(size_t count) {
    (void)count;
}

bool CoreModule::start() {
    if (running_.load()) return true;
    running_.store(true);

    if (dealer_) {
        dealer_->set_response_callback([this](const DetectionResponse& resp) {
            pending_results_.fetch_sub(1, std::memory_order_relaxed);
            Logger::instance().info("CoreModule",
                "Detection response received: " + resp.transaction_id +
                " (" + std::to_string(resp.results.size()) + " results)");
            auto j = resp.to_json();
            j["Status"] = "1";
            enqueue_result(resp.transaction_id, resp.dealer_id, j);
        });
    }

    pipeline_thread_ = std::thread([this]() { pipeline_thread_func(); });
    result_thread_ = std::thread([this]() { result_thread_func(); });

    Logger::instance().info("CoreModule", "Started");
    return true;
}

void CoreModule::stop() {
    if (!running_.load()) return;
    running_.store(false);
    result_cv_.notify_all();
    if (pipeline_thread_.joinable()) pipeline_thread_.join();
    if (result_thread_.joinable()) result_thread_.join();
    Logger::instance().info("CoreModule", "Stopped");
}

void CoreModule::pipeline_thread_func() {
    Logger::instance().info("CoreModule", "Pipeline thread started");
    while (running_.load()) {
        if (!buffer_ || !dealer_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        if (processing_paused_.load()) {
            if (pending_results_.load() < max_pending_results_) {
                processing_paused_.store(false);
                Logger::instance().info("CoreModule", "Processing resumed — pending below limit");
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                continue;
            }
        }

        auto img = buffer_->pop(500);
        if (!img) continue;

        if (!dealer_->is_client_connected()) {
            auto now = std::chrono::steady_clock::now();
            if (now - last_no_client_publish_ > std::chrono::seconds(5)) {
                last_no_client_publish_ = now;
                std::string txn = generate_transaction_id();
                nlohmann::json status_result;
                status_result["TransactionID"] = txn;
                status_result["DealerID"] = config_.dealer_id;
                status_result["Status"] = "0";
                status_result["Reason"] = "No AI client connected";
                status_result["Result"] = nlohmann::json::array();
                status_result["TimestampReplied"] = Timestamp::now().to_string();
                enqueue_result(txn, config_.dealer_id, status_result);
                Logger::instance().warn("CoreModule",
                    "No AI client connected — skipping detection, status published: " + txn);
            }
            continue;
        }

        DetectionRequest req;
        req.transaction_id = generate_transaction_id();
        req.dealer_id = config_.dealer_id;
        req.mode = config_.mode;
        req.timestamp = img->timestamp.to_string();

        ImageRef ref;
        ref.file_name = "IMG-" + req.transaction_id +
                        (img->part.empty() ? "" : "-" + img->part) + ".jpg";
        ref.resolution = img->resolution;
        ref.format = img->format;

        if (config_.mode == "File") {
            ref.uri = image_save_path_ + "/" + ref.file_name;
        } else if (config_.mode == "Http") {
            ref.uri = http_base_url_ + "/" + ref.file_name;
        } else {
            ref.uri = "";
        }
        req.data.push_back(ref);

        Logger::instance().debug("CoreModule",
            "Sending detection request: " + req.transaction_id +
            " mode=" + req.mode +
            " (" + std::to_string(req.data.size()) + " images)");

        bool send_ok = false;
        if (config_.mode == "Binary" && img->payload && !img->payload->empty()) {
            send_ok = dealer_->send_request_with_data(req, img->payload);
        } else {
            if ((config_.mode == "File" || config_.mode == "Http") &&
                img->payload && !img->payload->empty()) {
                std::string save_path = image_save_path_ + "/" + ref.file_name;
                std::ofstream ofs(save_path, std::ios::binary);
                if (ofs.is_open()) {
                    ofs.write(reinterpret_cast<const char*>(img->payload->data),
                              img->payload->size);
                    ofs.close();
                    Logger::instance().debug("CoreModule",
                        "Image saved: " + save_path);
                }
            }
            send_ok = dealer_->send_request(req);
        }

        if (!send_ok) {
            Logger::instance().debug("CoreModule",
                "Send failed (HWM full), silently dropping: " + req.transaction_id);
            continue;
        }

        pending_results_.fetch_add(1, std::memory_order_relaxed);
        if (pending_results_.load() >= max_pending_results_) {
            processing_paused_.store(true);
            Logger::instance().warn("CoreModule",
                "Processing paused — in-flight requests at limit (" +
                std::to_string(pending_results_.load()) + ")");
        }
    }
    Logger::instance().info("CoreModule", "Pipeline thread stopped");
}

void CoreModule::result_thread_func() {
    Logger::instance().info("CoreModule", "Result thread started");
    while (running_.load()) {
        ResultEntry entry;
        {
            std::unique_lock<std::mutex> lk(result_mutex_);
            result_cv_.wait_for(lk, std::chrono::milliseconds(500), [&] {
                return !result_queue_.empty() || !running_.load();
            });
            if (!running_.load() && result_queue_.empty()) break;
            if (result_queue_.empty()) continue;
            entry = std::move(result_queue_.front());
            result_queue_.pop();
        }
        if (publisher_) {
            publisher_->publish_result(entry.transaction_id, entry.dealer_id, entry.result_json);
        }
    }
    Logger::instance().info("CoreModule", "Result thread stopped");
}

} // namespace ai_vision
