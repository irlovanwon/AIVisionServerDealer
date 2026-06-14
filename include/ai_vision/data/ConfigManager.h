/*
 * Copyright(c) 2026-2030, VIATECH & UZONE All rights reserved
 * Des: JSON configuration manager — loads/saves config/default.json
 * Date: 20260614
 * Modification:
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
    int port = 8443;
    std::string cert_path = "certs/server.crt";
    std::string key_path = "certs/server.key";
    int worker_threads = 4;
};

struct Api2aConfig {
    std::string transport = "ipc";
    nlohmann::json channels;
    std::string default_channel = "image";
    bool stereo_mode = false;
    int rcvhwm = 10;

    std::string channel_endpoint(const std::string& name) const {
        if (channels.contains(name)) return channels[name].get<std::string>();
        return "";
    }
};

struct Api2bConfig {
    std::string transport = "ipc";
    std::string endpoint_local = "ipc:///tmp/ai_vision_dealer_result";
    std::string endpoint_remote = "tcp://*:5556";
    int sndhwm = 10;

    std::string endpoint() const {
        return (transport == "tcp") ? endpoint_remote : endpoint_local;
    }
};

struct Config {
    std::string dealer_id = "Edge001";
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
