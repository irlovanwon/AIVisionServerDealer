/*
 * Copyright(c) 2026-2030, VIATECH & UZONE All rights reserved
 * Des: Thread-safe singleton logger
 * Date: 20260614
 * Modification:
 */
#include "ai_vision/common/Logger.h"
#include <iostream>
#include <ctime>

namespace ai_vision {

Logger& Logger::instance() {
    static Logger logger;
    return logger;
}

void Logger::set_level(LogLevel level) {
    level_ = level;
}

void Logger::set_level_from_string(const std::string& level) {
    if (level == "debug") level_ = LogLevel::Debug;
    else if (level == "info") level_ = LogLevel::Info;
    else if (level == "warn") level_ = LogLevel::Warn;
    else if (level == "error") level_ = LogLevel::Error;
}

static const char* level_str(LogLevel l) {
    switch (l) {
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info:  return "INFO";
        case LogLevel::Warn:  return "WARN";
        case LogLevel::Error: return "ERROR";
    }
    return "UNKNOWN";
}

void Logger::log(LogLevel level, const std::string& tag, const std::string& msg) {
    if (level < level_) return;
    std::lock_guard<std::mutex> lock(mutex_);
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", std::localtime(&t));
    std::cout << "[" << buf << "] [" << level_str(level) << "] [" << tag << "] " << msg << std::endl;
}

void Logger::debug(const std::string& tag, const std::string& msg) { log(LogLevel::Debug, tag, msg); }
void Logger::info(const std::string& tag, const std::string& msg)  { log(LogLevel::Info, tag, msg); }
void Logger::warn(const std::string& tag, const std::string& msg)  { log(LogLevel::Warn, tag, msg); }
void Logger::error(const std::string& tag, const std::string& msg) { log(LogLevel::Error, tag, msg); }

} // namespace ai_vision
