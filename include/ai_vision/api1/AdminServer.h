/*
 * Copyright(c) 2026-2030, VIATECH & UZONE All rights reserved
 * Des: AdminServer — API1b HTTPS server for admin commands
 * Date: 20260614
 * Modification:
 */
#pragma once

#include "ai_vision/common/Types.h"
#include "ai_vision/data/ConfigManager.h"
#include <string>
#include <cstdint>
#include <memory>
#include <vector>
#include <thread>
#include <functional>

namespace ai_vision {

class AdminServer {
public:
    using AdminHandler = std::function<Response(const std::string& action,
                                                const std::vector<AdminParameter>& params)>;

    AdminServer(const Api1bConfig& config);
    ~AdminServer();

    void set_handler(AdminHandler handler);
    void start();
    void stop();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ai_vision
