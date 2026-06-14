/*
 * Copyright(c) 2026-2030, VIATECH & UZONE All rights reserved
 * Des: DetectionDealer — API1a ZMQ DEALER/DEALER for AI detection
 * Date: 20260614
 * Modification:
 */
#include "ai_vision/api1/DetectionDealer.h"
#include "ai_vision/common/Logger.h"
#include <zmq.h>
#include <cstring>

namespace ai_vision {

DetectionDealer::DetectionDealer() = default;

DetectionDealer::~DetectionDealer() {
    disconnect();
}

bool DetectionDealer::connect(const Api1aConfig& config) {
    zmq_context_ = zmq_ctx_new();
    if (!zmq_context_) {
        Logger::instance().error("DetectionDealer", "Failed to create ZMQ context");
        return false;
    }

    zmq_socket_ = zmq_socket(zmq_context_, ZMQ_DEALER);
    if (!zmq_socket_) {
        Logger::instance().error("DetectionDealer", "Failed to create ZMQ DEALER socket");
        zmq_ctx_destroy(zmq_context_);
        zmq_context_ = nullptr;
        return false;
    }

    if (!config.identity.empty()) {
        zmq_setsockopt(zmq_socket_, ZMQ_IDENTITY,
                       config.identity.c_str(), config.identity.size());
    }

    int linger = 0;
    zmq_setsockopt(zmq_socket_, ZMQ_LINGER, &linger, sizeof(linger));
    int sndhwm = config.sndhwm;
    zmq_setsockopt(zmq_socket_, ZMQ_SNDHWM, &sndhwm, sizeof(sndhwm));
    int rcvhwm = config.rcvhwm;
    zmq_setsockopt(zmq_socket_, ZMQ_RCVHWM, &rcvhwm, sizeof(rcvhwm));

    std::string endpoint = config.endpoint();
    if (zmq_connect(zmq_socket_, endpoint.c_str()) != 0) {
        Logger::instance().error("DetectionDealer",
            "ZMQ connect failed: " + std::string(zmq_strerror(zmq_errno())));
        zmq_close(zmq_socket_);
        zmq_ctx_destroy(zmq_context_);
        zmq_socket_ = nullptr;
        zmq_context_ = nullptr;
        return false;
    }

    poll_timeout_ms_ = config.poll_timeout_ms;
    connected_.store(true);
    running_.store(true);
    poll_thread_ = std::thread([this]() { poll_thread_func(); });

    Logger::instance().info("DetectionDealer",
        "DEALER connected to " + endpoint + " (identity: " + config.identity + ")");
    return true;
}

void DetectionDealer::disconnect() {
    if (!connected_.load()) return;
    running_.store(false);
    connected_.store(false);
    if (poll_thread_.joinable()) poll_thread_.join();
    if (zmq_socket_) { zmq_close(zmq_socket_); zmq_socket_ = nullptr; }
    if (zmq_context_) { zmq_ctx_destroy(zmq_context_); zmq_context_ = nullptr; }
    Logger::instance().info("DetectionDealer", "Disconnected");
}

void DetectionDealer::set_response_callback(ResponseCallback cb) {
    callback_ = cb;
}

bool DetectionDealer::send_request(const DetectionRequest& request) {
    if (!connected_.load() || !zmq_socket_) return false;

    std::string json_str = request.to_json().dump();
    int rc = zmq_send(zmq_socket_, json_str.data(), json_str.size(), 0);
    if (rc < 0) {
        Logger::instance().error("DetectionDealer",
            "Send failed: " + std::string(zmq_strerror(zmq_errno())));
        return false;
    }
    return true;
}

void DetectionDealer::poll_thread_func() {
    Logger::instance().info("DetectionDealer", "Poll thread started");
    while (running_.load()) {
        zmq_pollitem_t items[1];
        items[0].socket = zmq_socket_;
        items[0].events = ZMQ_POLLIN;

        int rc = zmq_poll(items, 1, poll_timeout_ms_);
        if (rc < 0) {
            if (!running_.load()) break;
            continue;
        }
        if (rc == 0) continue;
        if (!(items[0].revents & ZMQ_POLLIN)) continue;

        zmq_msg_t msg;
        zmq_msg_init(&msg);
        rc = zmq_msg_recv(&msg, zmq_socket_, ZMQ_DONTWAIT);
        if (rc >= 0) {
            std::string data(static_cast<char*>(zmq_msg_data(&msg)), zmq_msg_size(&msg));
            zmq_msg_close(&msg);

            try {
                auto j = nlohmann::json::parse(data);
                auto resp = DetectionResponse::from_json(j);
                Logger::instance().debug("DetectionDealer",
                    "Response received for " + resp.transaction_id);
                if (callback_) callback_(resp);
            } catch (const std::exception& e) {
                Logger::instance().error("DetectionDealer",
                    std::string("JSON parse error: ") + e.what());
            }
        } else {
            zmq_msg_close(&msg);
        }
    }
    Logger::instance().info("DetectionDealer", "Poll thread stopped");
}

} // namespace ai_vision
