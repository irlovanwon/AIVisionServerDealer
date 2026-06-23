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

static std::atomic<bool> g_running{true};

static void signal_handler(int) {
    g_running.store(false);
}

static auto make_admin_handler(std::shared_ptr<ai_vision::CoreModule> core,
                               const ai_vision::Config& config) {
    return [&core, &config](const std::string& action,
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

    auto buffer = std::make_shared<ai_vision::ImageBuffer>(20);

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

    publisher->bind("ipc:///tmp/ai_vision_dealer_result");

    core->set_config(config);
    core->set_image_buffer(buffer);
    core->set_detection_dealer(dealer);
    core->set_result_publisher(publisher);
    core->set_image_save_path("/tmp/ai_vision_images");

    if (!core->start()) {
        ai_vision::Logger::instance().error("Main", "Failed to start CoreModule");
        return 1;
    }

    auto admin_handler = make_admin_handler(core, config);

    ai_vision::AdminServer admin_server(config.api1b);
    admin_server.set_handler(admin_handler);
    admin_server.set_dealer_id(config.dealer_id);
    admin_server.start();

    ai_vision::AdminServer api2b_server(config.api2b);
    api2b_server.set_handler(admin_handler);
    api2b_server.set_dealer_id(config.dealer_id);
    api2b_server.start();

    ai_vision::Logger::instance().info("Main",
        "AIVisionServerDealer ready (API1b: " + std::to_string(config.api1b.port) +
        ", API2b: " + std::to_string(config.api2b.port) + "). Press Ctrl+C to stop.");

    while (g_running.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    ai_vision::Logger::instance().info("Main", "Shutting down...");
    api2b_server.stop();
    admin_server.stop();
    core->stop();
    publisher->unbind();
    dealer->disconnect();
    subscriber->disconnect();
    ai_vision::Logger::instance().info("Main", "Shutdown complete");
    return 0;
}
