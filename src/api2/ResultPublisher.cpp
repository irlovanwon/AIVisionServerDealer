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

bool ResultPublisher::bind(const std::string& endpoint) {
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
    int sndhwm = 10;
    zmq_setsockopt(zmq_socket_, ZMQ_SNDHWM, &sndhwm, sizeof(sndhwm));

    if (zmq_bind(zmq_socket_, endpoint.c_str()) != 0) {
        Logger::instance().error("ResultPublisher",
            "ZMQ bind failed: " + std::string(zmq_strerror(zmq_errno())));
        zmq_close(zmq_socket_);
        zmq_ctx_destroy(zmq_context_);
        zmq_socket_ = nullptr;
        zmq_context_ = nullptr;
        return false;
    }

    endpoint_ = endpoint;
    bound_.store(true);
    Logger::instance().info("ResultPublisher", "PUB bound to " + endpoint);
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

    auto* hdr_str = new std::string(header.dump());
    zmq_msg_t msg1;
    zmq_msg_init_data(&msg1, hdr_str->data(), hdr_str->size(),
        [](void*, void* hint) { delete static_cast<std::string*>(hint); }, hdr_str);

    int rc = zmq_msg_send(&msg1, zmq_socket_, ZMQ_SNDMORE | ZMQ_DONTWAIT);
    if (rc < 0) {
        delete hdr_str;
        return;
    }

    auto* res_str = new std::string(result_json.dump());
    zmq_msg_t msg2;
    zmq_msg_init_data(&msg2, res_str->data(), res_str->size(),
        [](void*, void* hint) { delete static_cast<std::string*>(hint); }, res_str);

    rc = zmq_msg_send(&msg2, zmq_socket_, ZMQ_DONTWAIT);
    if (rc < 0) {
        delete res_str;
    }
}

} // namespace ai_vision
