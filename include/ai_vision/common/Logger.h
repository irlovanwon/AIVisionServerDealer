/*
 * Copyright(c) 2026-2030, VIATECH & UZONE All rights reserved
 * Des: Thread-safe singleton logger
 * Date: 20260614
 * Modification:
 */
#pragma once

#include <string>
#include <mutex>

namespace ai_vision {

enum class LogLevel { Debug, Info, Warn, Error };

class Logger {
public:
    static Logger& instance();

    void set_level(LogLevel level);
    void set_level_from_string(const std::string& level);
    void log(LogLevel level, const std::string& tag, const std::string& msg);

    void debug(const std::string& tag, const std::string& msg);
    void info(const std::string& tag, const std::string& msg);
    void warn(const std::string& tag, const std::string& msg);
    void error(const std::string& tag, const std::string& msg);

private:
    Logger() = default;
    LogLevel level_ = LogLevel::Info;
    std::mutex mutex_;
};

} // namespace ai_vision
