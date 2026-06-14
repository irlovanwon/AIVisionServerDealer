/*
 * Copyright(c) 2026-2030, VIATECH & UZONE All rights reserved
 * Des: ResultPublisher — API2b ZMQ PUB for publishing detection results
 * Date: 20260614
 * Modification:
 */
#include "ai_vision/api2/ResultPublisher.h"
#include "ai_vision/common/Logger.h"
#include <zmq.h>
#include <cstring>

namespace ai_vision {

ResultPublisher::ResultPublisher() = default;

ResultPublisher::~ResultPublisher() {
    unbind();
}

bool ResultPublisher::bind(const Api2bConfig& config) {
    zmq_context_ = zmq_ctx_new();
    if (!zmq_context_) {
        Logger::instance().error("ResultPublisher", "Failed to create ZMQ context");
        return false;
    }

    zmq_socket_ = zmq_socket(zmq_context_, ZMQ_PUB);
    if (!zmq_socket_) {
        Logger::instance().error("ResultPublisher", "Failed to create ZMQ PUB socket");
        zmq_ctx_destroy(zmq_context_);
        zmq_context_ = nullptr;
        return false;
    }

    int linger = 0;
    zmq_setsockopt(zmq_socket_, ZMQ_LINGER, &linger, sizeof(linger));
    int sndhwm = config.sndhwm;
    zmq_setsockopt(zmq_socket_, ZMQ_SNDHWM, &sndhwm, sizeof(sndhwm));

    std::string ep = config.endpoint();
    if (zmq_bind(zmq_socket_, ep.c_str()) != 0) {
        Logger::instance().error("ResultPublisher",
            "ZMQ bind failed: " + std::string(zmq_strerror(zmq_errno())));
        zmq_close(zmq_socket_);
        zmq_ctx_destroy(zmq_context_);
        zmq_socket_ = nullptr;
        zmq_context_ = nullptr;
        return false;
    }

    endpoint_ = ep;
    bound_.store(true);
    Logger::instance().info("ResultPublisher", "PUB bound to " + ep);
    return true;
}

void ResultPublisher::unbind() {
    if (!bound_.load()) return;
    bound_.store(false);
    if (zmq_socket_) { zmq_close(zmq_socket_); zmq_socket_ = nullptr; }
    if (zmq_context_) { zmq_ctx_destroy(zmq_context_); zmq_context_ = nullptr; }
    Logger::instance().info("ResultPublisher", "Unbound from " + endpoint_);
}

void ResultPublisher::publish_result(const std::string& transaction_id,
                                      const std::string& dealer_id,
                                      const nlohmann::json& result_json) {
    if (!bound_.load() || !zmq_socket_) return;

    nlohmann::json header;
    auto ts = Timestamp::now();
    header["ts_sec"] = ts.sec;
    header["ts_nsec"] = ts.nsec;
    header["transaction_id"] = transaction_id;
    header["dealer_id"] = dealer_id;

    std::string header_str = header.dump();
    std::string result_str = result_json.dump();

    int rc = zmq_send(zmq_socket_, header_str.data(), header_str.size(), ZMQ_SNDMORE);
    if (rc < 0) return;
    zmq_send(zmq_socket_, result_str.data(), result_str.size(), 0);
}

} // namespace ai_vision
