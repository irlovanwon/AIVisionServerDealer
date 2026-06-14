/*
 * Copyright(c) 2026-2030, VIATECH & UZONE All rights reserved
 * Des: Response helper functions and Timestamp/Detection JSON serialization
 * Date: 20260614
 * Modification:
 */
#include "ai_vision/common/Response.h"
#include <ctime>
#include <cstring>
#include <iomanip>
#include <sstream>

namespace ai_vision {

const char* response_code_name(ResponseCode code) {
    switch (code) {
        case ResponseCode::Success:     return "Success";
        case ResponseCode::Error:       return "Error";
        case ResponseCode::NotReady:    return "NotReady";
        case ResponseCode::AlreadyInit: return "AlreadyInit";
        case ResponseCode::InvalidParam: return "InvalidParam";
        case ResponseCode::Unavailable: return "Unavailable";
    }
    return "Unknown";
}

Response make_response(ResponseCode code, const std::string& message) {
    return {code, message.empty() ? response_code_name(code) : message, nullptr};
}

Response make_response(ResponseCode code, const std::string& message, nlohmann::json detail) {
    return {code, message.empty() ? response_code_name(code) : message, std::move(detail)};
}

std::string Timestamp::to_string() const {
    std::time_t t = static_cast<std::time_t>(sec);
    std::tm tm = *std::localtime(&t);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y%m%d-%H%M%S");
    oss << "-" << std::setfill('0') << std::setw(3) << (nsec / 1000000);
    return oss.str();
}

Timestamp Timestamp::parse(const std::string& s) {
    Timestamp ts{0, 0};
    if (s.size() >= 14) {
        std::tm tm{};
        tm.tm_year = std::stoi(s.substr(0, 4)) - 1900;
        tm.tm_mon = std::stoi(s.substr(4, 2)) - 1;
        tm.tm_mday = std::stoi(s.substr(6, 2));
        tm.tm_hour = std::stoi(s.substr(8, 2));
        tm.tm_min = std::stoi(s.substr(10, 2));
        tm.tm_sec = std::stoi(s.substr(12, 2));
        ts.sec = static_cast<int64_t>(std::mktime(&tm));
        if (s.size() >= 18) {
            ts.nsec = std::stoi(s.substr(15, 3)) * 1000000;
        }
    }
    return ts;
}

nlohmann::json DetectionRequest::to_json() const {
    nlohmann::json data_array = nlohmann::json::array();
    for (const auto& d : data) {
        data_array.push_back({
            {"URI", d.uri},
            {"FileName", d.file_name},
            {"Resolution", d.resolution},
            {"Format", d.format}
        });
    }
    return {
        {"TransactionID", transaction_id},
        {"Mode", mode},
        {"DealerID", dealer_id},
        {"Data", data_array},
        {"Timestamp", timestamp}
    };
}

DetectionResponse DetectionResponse::from_json(const nlohmann::json& j) {
    DetectionResponse resp;
    resp.transaction_id = j.value("TransactionID", "");
    resp.dealer_id = j.value("DealerID", "");
    resp.timestamp_received = j.value("TimestampReceived", "");
    resp.timestamp_replied = j.value("TimestampReplied", "");
    if (j.contains("Result") && j["Result"].is_array()) {
        for (const auto& r : j["Result"]) {
            DetectionResultItem item;
            item.label_id = r.value("LabelID", "");
            item.confidence = r.value("Confidence", "");
            item.file_name = r.value("FileName", "");
            item.coordinates = r.value("Coordinates", "");
            item.ocr = r.value("OCR", "");
            item.timestamp_start = r.value("TimestampStart", "");
            item.timestamp_end = r.value("TimestampEnd", "");
            resp.results.push_back(item);
        }
    }
    return resp;
}

nlohmann::json DetectionResponse::to_json() const {
    nlohmann::json result_array = nlohmann::json::array();
    for (const auto& r : results) {
        nlohmann::json item = {
            {"LabelID", r.label_id},
            {"Confidence", r.confidence},
            {"FileName", r.file_name},
            {"Coordinates", r.coordinates},
            {"TimestampStart", r.timestamp_start},
            {"TimestampEnd", r.timestamp_end}
        };
        if (!r.ocr.empty()) {
            item["OCR"] = r.ocr;
        }
        result_array.push_back(item);
    }
    return {
        {"TransactionID", transaction_id},
        {"DealerID", dealer_id},
        {"Result", result_array},
        {"TimestampReceived", timestamp_received},
        {"TimestampReplied", timestamp_replied}
    };
}

} // namespace ai_vision
