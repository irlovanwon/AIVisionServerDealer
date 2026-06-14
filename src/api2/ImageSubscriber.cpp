/*
 * Copyright(c) 2026-2030, VIATECH & UZONE All rights reserved
 * Des: ImageSubscriber — API2a ZMQ SUB for image ingest from data server
 * Date: 20260614
 * Modification:
 */
#include "ai_vision/api2/ImageSubscriber.h"
#include "ai_vision/common/Logger.h"
#include <zmq.h>
#include <cstring>

namespace ai_vision {

ImageSubscriber::ImageSubscriber() = default;

ImageSubscriber::~ImageSubscriber() {
    disconnect();
}

bool ImageSubscriber::connect(const Api2aConfig& config) {
    zmq_context_ = zmq_ctx_new();
    if (!zmq_context_) {
        Logger::instance().error("ImageSubscriber", "Failed to create ZMQ context");
        return false;
    }

    running_.store(true);
    connected_.store(true);

    std::vector<std::string> channel_names;
    channel_names.push_back(config.default_channel);
    if (config.stereo_mode && config.default_channel != "stereo_image") {
        channel_names.push_back("stereo_image");
    }

    for (const auto& ch : channel_names) {
        std::string endpoint = config.channel_endpoint(ch);
        if (endpoint.empty()) {
            Logger::instance().warn("ImageSubscriber",
                "No endpoint for channel: " + ch);
            continue;
        }

        subscribe_threads_.emplace_back(
            [this, ch, endpoint, config]() {
                subscribe_thread_func(ch, endpoint);
            });
    }

    Logger::instance().info("ImageSubscriber",
        "SUB connected (" + std::to_string(channel_names.size()) + " channels, stereo=" +
        (config.stereo_mode ? "on" : "off") + ")");
    return true;
}

void ImageSubscriber::disconnect() {
    if (!connected_.load()) return;
    running_.store(false);
    connected_.store(false);
    for (auto& t : subscribe_threads_) {
        if (t.joinable()) t.join();
    }
    subscribe_threads_.clear();
    if (zmq_context_) {
        zmq_ctx_destroy(zmq_context_);
        zmq_context_ = nullptr;
    }
    Logger::instance().info("ImageSubscriber", "Disconnected");
}

void ImageSubscriber::set_callback(ImageCallback cb) {
    callback_ = cb;
}

void ImageSubscriber::subscribe_thread_func(const std::string& channel,
                                             const std::string& endpoint) {
    void* sock = zmq_socket(zmq_context_, ZMQ_SUB);
    if (!sock) {
        Logger::instance().error("ImageSubscriber",
            "Failed to create SUB socket for " + channel);
        return;
    }

    zmq_setsockopt(sock, ZMQ_SUBSCRIBE, "", 0);
    int linger = 0;
    zmq_setsockopt(sock, ZMQ_LINGER, &linger, sizeof(linger));

    if (zmq_connect(sock, endpoint.c_str()) != 0) {
        Logger::instance().error("ImageSubscriber",
            "Connect failed for " + channel + ": " + std::string(zmq_strerror(zmq_errno())));
        zmq_close(sock);
        return;
    }

    Logger::instance().info("ImageSubscriber",
        "SUB " + channel + " connected to " + endpoint);

    while (running_.load()) {
        zmq_pollitem_t items[1];
        items[0].socket = sock;
        items[0].events = ZMQ_POLLIN;

        int rc = zmq_poll(items, 1, 500);
        if (rc <= 0) continue;
        if (!(items[0].revents & ZMQ_POLLIN)) continue;

        zmq_msg_t header_msg;
        zmq_msg_init(&header_msg);
        rc = zmq_msg_recv(&header_msg, sock, ZMQ_DONTWAIT);
        if (rc < 0) {
            zmq_msg_close(&header_msg);
            continue;
        }

        nlohmann::json header;
        bool header_ok = false;
        try {
            std::string header_str(static_cast<char*>(zmq_msg_data(&header_msg)),
                                   zmq_msg_size(&header_msg));
            header = nlohmann::json::parse(header_str);
            header_ok = true;
        } catch (const std::exception& e) {
            Logger::instance().warn("ImageSubscriber",
                "Header parse error: " + std::string(e.what()));
        }
        zmq_msg_close(&header_msg);

        if (!header_ok) continue;

        zmq_msg_t payload_msg;
        zmq_msg_init(&payload_msg);
        rc = zmq_msg_recv(&payload_msg, sock, ZMQ_DONTWAIT);
        if (rc < 0) {
            zmq_msg_close(&payload_msg);
            continue;
        }

        auto img = std::make_shared<ImageData>();
        img->channel = channel;
        img->timestamp = {
            header.value("ts_sec", (int64_t)0),
            header.value("ts_nsec", (int64_t)0)
        };
        img->pair_id = header.value("pair_id", (uint64_t)0);
        img->part = header.value("part", "");
        img->resolution = header.value("resolution", "");
        img->format = header.value("format", "Mono");

        auto payload = std::make_shared<Payload>();
        if (zmq_msg_size(&payload_msg) > 0) {
            auto* heap_msg = new zmq_msg_t;
            zmq_msg_init(heap_msg);
            zmq_msg_move(heap_msg, &payload_msg);
            payload->data = static_cast<const uint8_t*>(zmq_msg_data(heap_msg));
            payload->size = zmq_msg_size(heap_msg);
            payload->owner = std::shared_ptr<void>(heap_msg, [](void* p) {
                auto* msg = static_cast<zmq_msg_t*>(p);
                zmq_msg_close(msg);
                delete msg;
            });
        }
        zmq_msg_close(&payload_msg);
        img->payload = payload;

        if (callback_) callback_(img);
    }

    zmq_close(sock);
    Logger::instance().info("ImageSubscriber",
        "SUB " + channel + " thread stopped");
}

} // namespace ai_vision
