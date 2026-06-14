/*
 * Copyright(c) 2026-2030, VIATECH & UZONE All rights reserved
 * Des: JSON configuration manager — loads/saves config/default.json
 * Date: 20260614
 * Modification:
 */
#include "ai_vision/data/ConfigManager.h"
#include "ai_vision/common/Logger.h"
#include <fstream>
#include <sstream>

namespace ai_vision {

Config ConfigManager::from_json(const nlohmann::json& j) {
    Config config;
    config.dealer_id = j.value("dealer_id", "Edge001");
    config.log_level = j.value("log_level", "info");

    if (j.contains("api1a")) {
        const auto& a = j["api1a"];
        config.api1a.transport = a.value("transport", "ipc");
        config.api1a.endpoint_local = a.value("endpoint_local", "ipc:///tmp/ai_vision_dealer_detection");
        config.api1a.endpoint_remote = a.value("endpoint_remote", "tcp://localhost:5555");
        config.api1a.identity = a.value("identity", "ai_vision_server_dealer");
        config.api1a.sndhwm = a.value("sndhwm", 10);
        config.api1a.rcvhwm = a.value("rcvhwm", 10);
        config.api1a.poll_timeout_ms = a.value("poll_timeout_ms", 100);
    }

    if (j.contains("api1b")) {
        const auto& a = j["api1b"];
        config.api1b.host = a.value("host", "0.0.0.0");
        config.api1b.port = a.value("port", 8443);
        config.api1b.cert_path = a.value("cert_path", "certs/server.crt");
        config.api1b.key_path = a.value("key_path", "certs/server.key");
        config.api1b.worker_threads = a.value("worker_threads", 4);
    }

    if (j.contains("api2a")) {
        const auto& a = j["api2a"];
        config.api2a.transport = a.value("transport", "ipc");
        config.api2a.channels = a.value("channels", nlohmann::json::object());
        config.api2a.default_channel = a.value("default_channel", "image");
        config.api2a.stereo_mode = a.value("stereo_mode", false);
        config.api2a.rcvhwm = a.value("rcvhwm", 10);
    }

    if (j.contains("api2b")) {
        const auto& a = j["api2b"];
        config.api2b.transport = a.value("transport", "ipc");
        config.api2b.endpoint_local = a.value("endpoint_local", "ipc:///tmp/ai_vision_dealer_result");
        config.api2b.endpoint_remote = a.value("endpoint_remote", "tcp://*:5556");
        config.api2b.sndhwm = a.value("sndhwm", 10);
    }

    return config;
}

nlohmann::json ConfigManager::to_json(const Config& config) {
    nlohmann::json j;
    j["dealer_id"] = config.dealer_id;
    j["log_level"] = config.log_level;

    j["api1a"] = {
        {"transport", config.api1a.transport},
        {"endpoint_local", config.api1a.endpoint_local},
        {"endpoint_remote", config.api1a.endpoint_remote},
        {"identity", config.api1a.identity},
        {"sndhwm", config.api1a.sndhwm},
        {"rcvhwm", config.api1a.rcvhwm},
        {"poll_timeout_ms", config.api1a.poll_timeout_ms}
    };

    j["api1b"] = {
        {"host", config.api1b.host},
        {"port", config.api1b.port},
        {"cert_path", config.api1b.cert_path},
        {"key_path", config.api1b.key_path},
        {"worker_threads", config.api1b.worker_threads}
    };

    j["api2a"] = {
        {"transport", config.api2a.transport},
        {"channels", config.api2a.channels},
        {"default_channel", config.api2a.default_channel},
        {"stereo_mode", config.api2a.stereo_mode},
        {"rcvhwm", config.api2a.rcvhwm}
    };

    j["api2b"] = {
        {"transport", config.api2b.transport},
        {"endpoint_local", config.api2b.endpoint_local},
        {"endpoint_remote", config.api2b.endpoint_remote},
        {"sndhwm", config.api2b.sndhwm}
    };

    return j;
}

Config ConfigManager::load(const std::string& path) {
    std::ifstream ifs(path);
    if (!ifs.is_open()) {
        Logger::instance().warn("ConfigManager", "Config file not found: " + path + ", using defaults");
        return Config();
    }
    std::stringstream ss;
    ss << ifs.rdbuf();
    try {
        nlohmann::json j = nlohmann::json::parse(ss.str());
        Logger::instance().info("ConfigManager", "Loaded config from " + path);
        return from_json(j);
    } catch (const std::exception& e) {
        Logger::instance().error("ConfigManager", std::string("Parse error: ") + e.what());
        return Config();
    }
}

void ConfigManager::save(const std::string& path, const Config& config) {
    nlohmann::json j = to_json(config);
    std::ofstream ofs(path);
    if (!ofs.is_open()) {
        Logger::instance().error("ConfigManager", "Cannot write config to " + path);
        return;
    }
    ofs << j.dump(4) << std::endl;
    Logger::instance().info("ConfigManager", "Saved config to " + path);
}

} // namespace ai_vision
