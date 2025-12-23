// Common logger implementation - shared formatting logic
// Platform-specific _log() is in web/Logger_web.cc or apple/Logger.mm

#include "core/log/include/Logger.h"

#include <cstdio>

#include <vector>

namespace cells::log {

void Logger::debug(const char* fmt, ...) {
    va_list args;  // NOLINT(cppcoreguidelines-init-variables)
    va_start(args, fmt);
    logv(Level::kDebug, fmt, args);
    va_end(args);
}

void Logger::info(const char* fmt, ...) {
    va_list args;  // NOLINT(cppcoreguidelines-init-variables)
    va_start(args, fmt);
    logv(Level::kInfo, fmt, args);
    va_end(args);
}

void Logger::warn(const char* fmt, ...) {
    va_list args;  // NOLINT(cppcoreguidelines-init-variables)
    va_start(args, fmt);
    logv(Level::kWarn, fmt, args);
    va_end(args);
}

void Logger::error(const char* fmt, ...) {
    va_list args;  // NOLINT(cppcoreguidelines-init-variables)
    va_start(args, fmt);
    logv(Level::kError, fmt, args);
    va_end(args);
}

void Logger::log(Level level, const char* fmt, ...) {
    va_list args;  // NOLINT(cppcoreguidelines-init-variables)
    va_start(args, fmt);
    logv(level, fmt, args);
    va_end(args);
}

void Logger::logv(Level level, const char* fmt, va_list args) {
    if (!_enabled) {
        return;
    }

    if (static_cast<int>(level) < static_cast<int>(_minLevel)) {
        return;
    }

    // Format the message
    va_list args_copy;  // NOLINT(cppcoreguidelines-init-variables)
    va_copy(args_copy, args);

    // Get required buffer size
    const int size = vsnprintf(nullptr, 0, fmt, args_copy);
    va_end(args_copy);

    if (size < 0) {
        return;
    }

    // Format into buffer
    std::vector<char> buffer(size + 1);
    vsnprintf(buffer.data(), buffer.size(), fmt, args);

    const std::string message(buffer.data());

    // Call platform-specific implementation
    _log(level, message);
}

}  // namespace cells::log
