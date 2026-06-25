/*
 * Copyright(c) 2026-2030, VIATECH & UZONE All rights reserved
 * Des: Entry point for AIVisionServerDealer
 * Date: 20260614
 * Modification: 20260623 — 3-mode system, API2b HTTPS command server
 */
#include "ai_vision/common/Logger.h"
#include "ai_vision/common/Types.h"
#include "ai_vision/data/ConfigManager.h"
#include "ai_vision/core/CoreModule.h"
#include "ai_vision/core/ImageBuffer.h"
#include "ai_vision/api1/DetectionDealer.h"
#include "ai_vision/api1/AdminServer.h"
#include "ai_vision/api2/ImageSubscriber.h"
#include "ai_vision/api2/ResultPublisher.h"
#include <csignal>
#include <atomic>
#include <memory>
#include <iostream>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fstream>
#include <sstream>

static std::atomic<bool> g_running{true};

static void signal_handler(int) {
    g_running.store(false);
}

static void http_file_server_thread(int port, const std::string& root_dir) {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) return;
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        ai_vision::Logger::instance().error("HttpFileServer",
            "Failed to bind port " + std::to_string(port));
        close(server_fd);
        return;
    }
    listen(server_fd, 5);
    ai_vision::Logger::instance().info("HttpFileServer",
        "Listening on 0.0.0.0:" + std::to_string(port) + " serving " + root_dir);

    struct timeval tv{1, 0};
    setsockopt(server_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    while (g_running.load()) {
        int client_fd = accept(server_fd, nullptr, nullptr);
        if (client_fd < 0) continue;

        char buf[4096];
        int n = read(client_fd, buf, sizeof(buf) - 1);
        if (n <= 0) { close(client_fd); continue; }
        buf[n] = '\0';

        std::string request(buf);
        size_t p1 = request.find("GET ");
        size_t p2 = request.find(" HTTP/");
        if (p1 == std::string::npos || p2 == std::string::npos) {
            close(client_fd); continue;
        }
        std::string req_path = request.substr(p1 + 5, p2 - p1 - 5);
        if (!req_path.empty() && req_path[0] != '/') req_path = "/" + req_path;

        std::string full_path = root_dir + req_path;
        std::ifstream ifs(full_path, std::ios::binary);
        if (!ifs.is_open()) {
            std::string resp = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
            write(client_fd, resp.c_str(), resp.size());
            close(client_fd);
            continue;
        }
        std::string content((std::istreambuf_iterator<char>(ifs)),
                             std::istreambuf_iterator<char>());
        std::string header = "HTTP/1.1 200 OK\r\n"
            "Content-Type: image/jpeg\r\n"
            "Content-Length: " + std::to_string(content.size()) + "\r\n"
            "Connection: close\r\n\r\n";
        write(client_fd, header.c_str(), header.size());
        write(client_fd, content.data(), content.size());
        close(client_fd);
    }
    close(server_fd);
}

static auto make_admin_handler(std::shared_ptr<ai_vision::CoreModule> core,
                               std::shared_ptr<ai_vision::ImageSubscriber> subscriber,
                               const ai_vision::Config& config) {
    return [core, subscriber, &config](const std::string& action,
                             const std::vector<ai_vision::AdminParameter>& params) -> ai_vision::Response {
        ai_vision::Logger::instance().info("Admin",
            "Action: " + action + " (" + std::to_string(params.size()) + " params)");
        if (action == "SetParameter") {
            for (const auto& p : params) {
                ai_vision::Logger::instance().info("Admin", "Set " + p.id + " = " + p.value);
                if (p.id == "Mode") {
                    const_cast<ai_vision::Config&>(config).mode = p.value;
                }
            }
            return ai_vision::make_response(ai_vision::ResponseCode::Success, "Parameters set");
        } else if (action == "GetParameter") {
            return ai_vision::make_response(ai_vision::ResponseCode::Success, "Parameters retrieved");
        } else if (action == "CheckStatus") {
            return ai_vision::make_response(ai_vision::ResponseCode::Success,
                core->is_paused() ? "Paused" : "OK");
        } else if (action == "Pause") {
            core->set_paused(true);
            return ai_vision::make_response(ai_vision::ResponseCode::Success, "Processing paused");
        } else if (action == "Resume") {
            core->set_paused(false);
            return ai_vision::make_response(ai_vision::ResponseCode::Success, "Processing resumed");
        } else if (action == "AckResult") {
            size_t count = 1;
            for (const auto& p : params) {
                if (p.id == "Count" && !p.value.empty()) count = std::stoul(p.value);
            }
            core->ack_results(count);
            return ai_vision::make_response(ai_vision::ResponseCode::Success,
                "Acknowledged " + std::to_string(count) + " results");
        } else if (action == "Reconnect") {
            ai_vision::Logger::instance().info("Admin", "Reconnect requested -- resetting state and reconnecting SUB");
            core->reset_state();
            std::thread([subscriber, &config]() {
                ai_vision::Logger::instance().info("ImageSubscriber", "Async reconnect: disconnecting...");
                subscriber->disconnect();
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                ai_vision::Logger::instance().info("ImageSubscriber", "Async reconnect: connecting...");
                subscriber->connect(config.api2a);
                ai_vision::Logger::instance().info("ImageSubscriber", "Async reconnect: done");
            }).detach();
            return ai_vision::make_response(ai_vision::ResponseCode::Success, "Reconnect scheduled");
        }
        return ai_vision::make_response(ai_vision::ResponseCode::Error, "Unknown action: " + action);
    };
}

int main(int argc, char* argv[]) {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGPIPE, SIG_IGN);

    std::string config_path = "config/default.json";
    for (int i = 1; i < argc; ++i) {
        std::string arg(argv[i]);
        if ((arg == "--config" || arg == "-c") && i + 1 < argc) {
            config_path = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: ai_vision_server_dealer [options]\n"
                      << "  --config PATH    Configuration file (default: config/default.json)\n"
                      << "  --help           Show this help\n";
            return 0;
        }
    }

    auto config = ai_vision::ConfigManager::load(config_path);
    ai_vision::Logger::instance().set_level_from_string(config.log_level);

    ai_vision::Logger::instance().info("Main",
        "Starting AIVisionServerDealer (dealer_id: " + config.dealer_id +
        ", mode: " + config.mode + ")");

    mkdir("/tmp/ai_vision_images", 0755);

    auto buffer = std::make_shared<ai_vision::ImageBuffer>(200);

    auto subscriber = std::make_shared<ai_vision::ImageSubscriber>();
    auto dealer = std::make_shared<ai_vision::DetectionDealer>();
    auto publisher = std::make_shared<ai_vision::ResultPublisher>();
    auto core = std::make_shared<ai_vision::CoreModule>();

    subscriber->set_callback([buffer](std::shared_ptr<ai_vision::ImageData> img) {
        ai_vision::Logger::instance().debug("Main",
            "Image received: channel=" + img->channel +
            " part=" + img->part +
            " size=" + std::to_string(img->payload ? img->payload->size : 0));
        buffer->push(img);
    });

    if (!subscriber->connect(config.api2a)) {
        ai_vision::Logger::instance().error("Main", "Failed to connect ImageSubscriber");
    }
    if (!dealer->connect(config.api1a)) {
        ai_vision::Logger::instance().error("Main", "Failed to connect DetectionDealer");
    }

    publisher->bind(config.api2a.result_endpoint);

    core->set_config(config);
    core->set_image_buffer(buffer);
    core->set_detection_dealer(dealer);
    core->set_result_publisher(publisher);
    core->set_image_save_path("/tmp/ai_vision_images");
    core->set_http_base_url("http://127.0.0.1:" + std::to_string(config.http_file_server_port));

    // When AI server reconnects (restart), reset pending state
    dealer->set_reconnect_callback([core]() {
        ai_vision::Logger::instance().info("Main", "AI server reconnected — resetting AIVD state");
        core->reset_state();
    });

    if (!core->start()) {
        ai_vision::Logger::instance().error("Main", "Failed to start CoreModule");
        return 1;
    }

    // Notify Core that AIVD has (re)started — Core should clear its pending state
    // Retry 3 times with 2s interval to handle ZMQ slow joiner
    std::thread([core]() {
        for (int i = 0; i < 3; ++i) {
            std::this_thread::sleep_for(std::chrono::seconds(2));
            core->publish_notification("Reconnect");
        }
    }).detach();

    auto admin_handler = make_admin_handler(core, subscriber, config);

    ai_vision::AdminServer admin_server(config.api1b);
    admin_server.set_handler(admin_handler);
    admin_server.set_dealer_id(config.dealer_id);
    admin_server.start();

    ai_vision::AdminServer api2b_server(config.api2b);
    api2b_server.set_handler(admin_handler);
    api2b_server.set_dealer_id(config.dealer_id);
    api2b_server.start();

    std::thread http_fs_thread([&config]() {
        http_file_server_thread(config.http_file_server_port, "/tmp/ai_vision_images");
    });

    ai_vision::Logger::instance().info("Main",
        "AIVisionServerDealer ready (API1b: " + std::to_string(config.api1b.port) +
        ", API2b: " + std::to_string(config.api2b.port) + "). Press Ctrl+C to stop.");

    while (g_running.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    ai_vision::Logger::instance().info("Main", "Shutting down...");
    if (http_fs_thread.joinable()) http_fs_thread.join();
    api2b_server.stop();
    admin_server.stop();
    core->stop();
    publisher->unbind();
    dealer->disconnect();
    subscriber->disconnect();
    ai_vision::Logger::instance().info("Main", "Shutdown complete");
    return 0;
}
