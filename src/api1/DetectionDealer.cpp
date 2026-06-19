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
    int immediate = 1;
    zmq_setsockopt(zmq_socket_, ZMQ_IMMEDIATE, &immediate, sizeof(immediate));
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

    monitor_addr_ = "inproc://detection_dealer_monitor";
    zmq_socket_monitor(zmq_socket_, monitor_addr_.c_str(),
                       ZMQ_EVENT_CONNECTED | ZMQ_EVENT_DISCONNECTED);

    poll_timeout_ms_ = config.poll_timeout_ms;
    connected_.store(true);
    running_.store(true);
    poll_thread_ = std::thread([this]() { poll_thread_func(); });
    monitor_thread_ = std::thread([this]() { monitor_thread_func(monitor_addr_); });

    Logger::instance().info("DetectionDealer",
        "DEALER connected to " + endpoint + " (identity: " + config.identity + ")");
    return true;
}

void DetectionDealer::disconnect() {
    if (!connected_.load()) return;
    running_.store(false);
    connected_.store(false);
    if (poll_thread_.joinable()) poll_thread_.join();
    if (monitor_thread_.joinable()) monitor_thread_.join();
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
    int rc = zmq_send(zmq_socket_, json_str.data(), json_str.size(), ZMQ_DONTWAIT);
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

void DetectionDealer::monitor_thread_func(const std::string& monitor_addr) {
    void* monitor_sock = zmq_socket(zmq_context_, ZMQ_PAIR);
    if (!monitor_sock) {
        Logger::instance().error("DetectionDealer", "Failed to create monitor socket");
        return;
    }
    if (zmq_connect(monitor_sock, monitor_addr.c_str()) != 0) {
        Logger::instance().error("DetectionDealer", "Failed to connect monitor socket");
        zmq_close(monitor_sock);
        return;
    }

    Logger::instance().info("DetectionDealer", "Monitor thread started");
    while (running_.load()) {
        zmq_msg_t msg1;
        zmq_msg_init(&msg1);
        int rc = zmq_msg_recv(&msg1, monitor_sock, 0);
        if (rc < 0) {
            zmq_msg_close(&msg1);
            if (!running_.load()) break;
            continue;
        }

        uint16_t event = 0;
        if (zmq_msg_size(&msg1) >= 2) {
            std::memcpy(&event, zmq_msg_data(&msg1), sizeof(uint16_t));
        }
        zmq_msg_close(&msg1);

        zmq_msg_t msg2;
        zmq_msg_init(&msg2);
        zmq_msg_recv(&msg2, monitor_sock, 0);
        zmq_msg_close(&msg2);

        if (event == ZMQ_EVENT_CONNECTED) {
            client_connected_.store(true);
            Logger::instance().info("DetectionDealer", "AI client connected");
        } else if (event == ZMQ_EVENT_DISCONNECTED) {
            client_connected_.store(false);
            Logger::instance().warn("DetectionDealer", "AI client disconnected");
        }
    }

    zmq_close(monitor_sock);
    Logger::instance().info("DetectionDealer", "Monitor thread stopped");
}

} // namespace ai_vision
