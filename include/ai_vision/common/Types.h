/*
 * Copyright(c) 2026-2030, VIATECH & UZONE All rights reserved
 * Des: Common types for AIVisionServerDealer
 * Date: 20260614
 * Modification:
 */
#pragma once

#include <string>
#include <cstdint>
#include <vector>
#include <chrono>
#include <memory>
#include <nlohmann/json.hpp>

namespace ai_vision {

struct Timestamp {
    int64_t sec;
    int64_t nsec;

    static Timestamp now() {
        auto tp = std::chrono::system_clock::now();
        auto dur = tp.time_since_epoch();
        auto s = std::chrono::duration_cast<std::chrono::seconds>(dur);
        auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(dur - s);
        return {s.count(), ns.count()};
    }

    std::string to_string() const;
    static Timestamp parse(const std::string& s);
};

struct Payload {
    const uint8_t* data = nullptr;
    size_t size = 0;
    std::shared_ptr<void> owner;

    bool empty() const { return size == 0; }
};
using PayloadPtr = std::shared_ptr<Payload>;

struct ImageData {
    std::string transaction_id;
    Timestamp timestamp;
    uint64_t pair_id = 0;
    std::string channel;
    std::string part;
    std::string resolution;
    std::string format;
    PayloadPtr payload;
};

struct SensorData {
    Timestamp timestamp;
    std::string type;
    nlohmann::json readings;
};

struct ImageRef {
    std::string uri;
    std::string file_name;
    std::string resolution;
    std::string format;
};

struct DetectionRequest {
    std::string transaction_id;
    std::string mode = "Binary";
    std::string dealer_id;
    std::vector<ImageRef> data;
    std::string timestamp;

    nlohmann::json to_json() const;
};

struct DetectionResultItem {
    std::string label_id;
    std::string confidence;
    std::string file_name;
    std::string coordinates;
    std::string ocr;
    std::string timestamp_start;
    std::string timestamp_end;
};

struct DetectionResponse {
    std::string transaction_id;
    std::string dealer_id;
    std::vector<DetectionResultItem> results;
    std::string timestamp_received;
    std::string timestamp_replied;

    static DetectionResponse from_json(const nlohmann::json& j);
    nlohmann::json to_json() const;
};

enum class ResponseCode {
    Success     = 0,
    Error       = 1,
    NotReady    = 2,
    AlreadyInit = 3,
    InvalidParam = 4,
    Unavailable = 5,
};

struct Response {
    ResponseCode code;
    std::string message;
    nlohmann::json detail;
};

Response make_response(ResponseCode code, const std::string& message = "");
Response make_response(ResponseCode code, const std::string& message, nlohmann::json detail);

struct AdminParameter {
    std::string id;
    std::string value;

    nlohmann::json to_json() const { return {{"ID", id}, {"Value", value}}; }
    static AdminParameter from_json(const nlohmann::json& j) {
        return {j.value("ID", ""), j.value("Value", "")};
    }
};

} // namespace ai_vision
