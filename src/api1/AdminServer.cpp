/*
 * Copyright(c) 2026-2030, VIATECH & UZONE All rights reserved
 * Des: AdminServer — API1b HTTPS server for admin commands (thread pool)
 * Date: 20260614
 * Modification:
 */
#include "ai_vision/api1/AdminServer.h"
#include "ai_vision/common/Logger.h"
#include <nlohmann/json.hpp>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <sstream>
#include <cstring>
#include <atomic>

namespace ai_vision {

struct Connection {
    int fd;
    SSL* ssl;
    std::string read_buffer;
};

struct AdminServer::Impl {
    Api1bConfig config;
    AdminHandler handler;
    int server_fd = -1;
    SSL_CTX* ssl_ctx = nullptr;
    std::atomic<bool> running{false};
    std::thread accept_thread;
    std::vector<std::thread> worker_threads;

    std::mutex task_mutex;
    std::condition_variable task_cv;
    std::deque<Connection*> task_queue;

    void start() {
        ssl_ctx = SSL_CTX_new(TLS_server_method());
        if (!ssl_ctx) {
            Logger::instance().error("AdminServer", "Failed to create SSL context");
            return;
        }
        if (SSL_CTX_use_certificate_file(ssl_ctx, config.cert_path.c_str(), SSL_FILETYPE_PEM) <= 0 ||
            SSL_CTX_use_PrivateKey_file(ssl_ctx, config.key_path.c_str(), SSL_FILETYPE_PEM) <= 0) {
            Logger::instance().error("AdminServer", "Failed to load cert/key");
            SSL_CTX_free(ssl_ctx);
            ssl_ctx = nullptr;
            return;
        }

        server_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (server_fd < 0) {
            Logger::instance().error("AdminServer", "Failed to create socket");
            return;
        }
        int opt = 1;
        setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        struct sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(config.port);
        addr.sin_addr.s_addr = INADDR_ANY;

        if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            Logger::instance().error("AdminServer",
                "Failed to bind on port " + std::to_string(config.port));
            return;
        }
        if (listen(server_fd, 32) < 0) {
            Logger::instance().error("AdminServer", "Failed to listen");
            return;
        }

        running.store(true);

        for (int i = 0; i < config.worker_threads; ++i) {
            worker_threads.emplace_back([this]() { worker_func(); });
        }

        accept_thread = std::thread([this]() { accept_func(); });

        Logger::instance().info("AdminServer",
            "Listening on " + config.host + ":" + std::to_string(config.port) +
            " (" + std::to_string(config.worker_threads) + " workers)");
    }

    void accept_func() {
        while (running.load()) {
            struct sockaddr_in client_addr{};
            socklen_t client_len = sizeof(client_addr);
            int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
            if (client_fd < 0) {
                if (!running.load()) break;
                continue;
            }

            auto* conn = new Connection();
            conn->fd = client_fd;
            conn->ssl = SSL_new(ssl_ctx);
            SSL_set_fd(conn->ssl, client_fd);

            if (SSL_accept(conn->ssl) <= 0) {
                Logger::instance().warn("AdminServer", "TLS handshake failed");
                SSL_free(conn->ssl);
                close(client_fd);
                delete conn;
                continue;
            }

            std::lock_guard<std::mutex> lock(task_mutex);
            task_queue.push_back(conn);
            task_cv.notify_one();
        }
    }

    void worker_func() {
        while (running.load()) {
            Connection* conn = nullptr;
            {
                std::unique_lock<std::mutex> lock(task_mutex);
                task_cv.wait(lock, [this]() { return !task_queue.empty() || !running.load(); });
                if (!running.load() && task_queue.empty()) break;
                if (task_queue.empty()) continue;
                conn = task_queue.front();
                task_queue.pop_front();
            }
            handle_connection(conn);
        }
    }

    void handle_connection(Connection* conn) {
        char buf[4096];
        std::string request_str;
        int bytes = 0;
        while ((bytes = SSL_read(conn->ssl, buf, sizeof(buf))) > 0) {
            request_str.append(buf, bytes);
            if (request_str.find("\r\n\r\n") != std::string::npos) {
                size_t header_end = request_str.find("\r\n\r\n");
                std::string headers = request_str.substr(0, header_end);
                size_t content_length = 0;
                size_t cl_pos = headers.find("Content-Length:");
                if (cl_pos != std::string::npos) {
                    content_length = std::stoul(headers.substr(cl_pos + 16));
                }
                std::string body = request_str.substr(header_end + 4);
                if (body.size() >= content_length) break;
            }
        }

        size_t body_start = request_str.find("\r\n\r\n");
        std::string body = (body_start != std::string::npos)
            ? request_str.substr(body_start + 4) : "";

        std::string response_body = process_request(body);
        std::string response_str =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/json\r\n"
            "Content-Length: " + std::to_string(response_body.size()) + "\r\n"
            "Connection: close\r\n"
            "\r\n" + response_body;

        SSL_write(conn->ssl, response_str.data(), response_str.size());

        SSL_shutdown(conn->ssl);
        SSL_free(conn->ssl);
        close(conn->fd);
        delete conn;
    }

    std::string process_request(const std::string& body) {
        if (body.empty()) {
            return make_error_response("Empty request body");
        }

        nlohmann::json req;
        try {
            req = nlohmann::json::parse(body);
        } catch (const std::exception& e) {
            return make_error_response(std::string("Parse error: ") + e.what());
        }

        std::string action = req.value("Action", "");
        std::string transaction_id = req.value("TransactionID", "");
        std::string client_dealer_id = req.value("DealerID", "");
        std::string timestamp = req.value("Timestamp", "");

        std::vector<AdminParameter> params;
        if (req.contains("Parameter") && req["Parameter"].is_array()) {
            for (const auto& p : req["Parameter"]) {
                params.push_back(AdminParameter::from_json(p));
            }
        }

        Response resp;
        if (handler) {
            resp = handler(action, params);
        } else {
            resp = make_response(ResponseCode::Error, "No handler registered");
        }

        nlohmann::json result = nlohmann::json::array();
        for (const auto& p : params) {
            result.push_back(p.to_json());
        }

        nlohmann::json resp_json;
        resp_json["TransactionID"] = transaction_id;
        resp_json["DealerID"] = dealer_id;
        resp_json["Status"] = (resp.code == ResponseCode::Success) ? "1" : "0";
        resp_json["Parameter"] = result;
        resp_json["TimestampReceived"] = timestamp;
        resp_json["TimestampReplied"] = Timestamp::now().to_string();

        return resp_json.dump();
    }

    std::string dealer_id = "Edge001";

    static std::string make_error_response(const std::string& msg) {
        nlohmann::json j;
        j["TransactionID"] = "";
        j["DealerID"] = "";
        j["Status"] = "0";
        j["Parameter"] = nlohmann::json::array();
        j["TimestampReceived"] = "";
        j["TimestampReplied"] = Timestamp::now().to_string();
        j["Error"] = msg;
        return j.dump();
    }

    void stop() {
        if (!running.load()) return;
        running.store(false);
        if (server_fd >= 0) {
            shutdown(server_fd, SHUT_RDWR);
            close(server_fd);
            server_fd = -1;
        }
        task_cv.notify_all();
        if (accept_thread.joinable()) accept_thread.join();
        for (auto& t : worker_threads) {
            if (t.joinable()) t.join();
        }
        if (ssl_ctx) { SSL_CTX_free(ssl_ctx); ssl_ctx = nullptr; }
        Logger::instance().info("AdminServer", "Stopped");
    }
};

AdminServer::AdminServer(const Api1bConfig& config)
    : impl_(std::make_unique<Impl>()) {
    impl_->config = config;
}

AdminServer::~AdminServer() {
    stop();
}

void AdminServer::set_handler(AdminHandler handler) {
    impl_->handler = handler;
}

void AdminServer::set_dealer_id(const std::string& id) {
    impl_->dealer_id = id;
}

void AdminServer::start() {
    impl_->start();
}

void AdminServer::stop() {
    impl_->stop();
}

} // namespace ai_vision
