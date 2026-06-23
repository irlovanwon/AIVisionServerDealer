/*
 * Copyright(c) 2026-2030, VIATECH & UZONE All rights reserved
 * Des: JSON configuration manager — loads/saves config/default.json
 * Date: 20260614
 * Modification: 20260623 — 3-mode system, API2b HTTPS, expanded API2a channels
 */
#pragma once

#include <string>
#include <mutex>
#include <nlohmann/json.hpp>

namespace ai_vision {

struct Api1aConfig {
    std::string transport = "ipc";
    std::string endpoint_local = "ipc:///tmp/ai_vision_dealer_detection";
    std::string endpoint_remote = "tcp://localhost:5555";
    std::string identity = "ai_vision_server_dealer";
    int sndhwm = 10;
    int rcvhwm = 10;
    int poll_timeout_ms = 100;

    std::string endpoint() const {
        return (transport == "tcp") ? endpoint_remote : endpoint_local;
    }
};

struct Api1bConfig {
    std::string host = "0.0.0.0";
    int port = 8445;
    std::string cert_path = "certs/server.crt";
    std::string key_path = "certs/server.key";
    int worker_threads = 4;
};

struct Api2aConfig {
    std::string transport = "ipc";
    nlohmann::json channels;
    std::string default_channel = "image";
    int rcvhwm = 10;
    std::string result_endpoint = "ipc:///tmp/ai_vision_dealer_result";

    std::string channel_endpoint(const std::string& name) const {
        if (channels.contains(name)) return channels[name].get<std::string>();
        return "";
    }
};

struct Api2bConfig {
    std::string host = "0.0.0.0";
    int port = 8446;
    std::string cert_path = "certs/server.crt";
    std::string key_path = "certs/server.key";
    int worker_threads = 4;

    operator Api1bConfig() const {
        return {host, port, cert_path, key_path, worker_threads};
    }
};

struct Config {
    std::string version = "1.0";
    std::string dealer_id = "Edge001";
    std::string mode = "Binary";
    int jpeg_quality = 85;
    int http_file_server_port = 8089;
    Api1aConfig api1a;
    Api1bConfig api1b;
    Api2aConfig api2a;
    Api2bConfig api2b;
    std::string log_level = "info";
};

class ConfigManager {
public:
    static Config load(const std::string& path);
    static void save(const std::string& path, const Config& config);

private:
    static Config from_json(const nlohmann::json& j);
    static nlohmann::json to_json(const Config& config);
};

} // namespace ai_vision
