/*
 * Copyright(c) 2026-2030, VIATECH & UZONE All rights reserved
 * Des: CoreModule — orchestrates ingest pipeline, detection, result forwarding
 * Date: 20260614
 * Modification:
 */
#include "ai_vision/core/CoreModule.h"
#include "ai_vision/core/ImageBuffer.h"
#include "ai_vision/api1/DetectionDealer.h"
#include "ai_vision/api2/ResultPublisher.h"
#include "ai_vision/common/Logger.h"
#include <fstream>
#include <cstdlib>

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

bool CoreModule::start() {
    if (running_.load()) return true;
    running_.store(true);

    if (dealer_) {
        dealer_->set_response_callback([this](const DetectionResponse& resp) {
            Logger::instance().info("CoreModule",
                "Detection response received: " + resp.transaction_id +
                " (" + std::to_string(resp.results.size()) + " results)");
            if (publisher_) {
                publisher_->publish_result(resp.transaction_id, resp.dealer_id, resp.to_json());
            }
        });
    }

    pipeline_thread_ = std::thread([this]() { pipeline_thread_func(); });

    Logger::instance().info("CoreModule", "Started");
    return true;
}

void CoreModule::stop() {
    if (!running_.load()) return;
    running_.store(false);
    if (pipeline_thread_.joinable()) pipeline_thread_.join();
    Logger::instance().info("CoreModule", "Stopped");
}

void CoreModule::pipeline_thread_func() {
    Logger::instance().info("CoreModule", "Pipeline thread started");
    while (running_.load()) {
        if (!buffer_ || !dealer_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        auto img = buffer_->pop(500);
        if (!img) continue;

        DetectionRequest req;
        req.transaction_id = generate_transaction_id();
        req.dealer_id = config_.dealer_id;
        req.mode = "File";
        req.timestamp = img->timestamp.to_string();

        ImageRef ref;
        ref.uri = "/tmp/ai_vision_buffer/" + req.transaction_id + "_" +
                  (img->part.empty() ? "image" : img->part) + ".jpg";
        ref.file_name = "IMG-" + req.transaction_id +
                        (img->part.empty() ? "" : "-" + img->part) + ".jpg";
        ref.resolution = img->resolution;
        ref.format = img->format;
        req.data.push_back(ref);

        if (img->payload && !img->payload->empty()) {
            std::string dir = "/tmp/ai_vision_buffer";
            std::string cmd = "mkdir -p " + dir;
            system(cmd.c_str());
            std::ofstream ofs(ref.uri, std::ios::binary);
            if (ofs.is_open()) {
                ofs.write(reinterpret_cast<const char*>(img->payload->data()),
                          img->payload->size());
            }
        }

        Logger::instance().debug("CoreModule",
            "Sending detection request: " + req.transaction_id +
            " (" + std::to_string(req.data.size()) + " images)");

        if (!dealer_->send_request(req)) {
            Logger::instance().warn("CoreModule",
                "Failed to send detection request: " + req.transaction_id);
        }
    }
    Logger::instance().info("CoreModule", "Pipeline thread stopped");
}

} // namespace ai_vision
