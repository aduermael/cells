// Default/CLI logger implementation
// Uses stderr for output (all non-web platforms)
// Note: Apple-specific NSLog implementation exists in apple/Logger.mm but
// requires rules_apple. This default implementation works fine on macOS.

#if !defined(__EMSCRIPTEN__)

#include <cstdio>

#include "core/log/include/Logger.h"

namespace cells::log {

class DefaultLogger : public Logger {
protected:
    void _log(Level level, const std::string& message) override {
        const char* levelStr = nullptr;

        switch (level) {
            case Level::DEBUG:
                levelStr = "DEBUG";
                break;
            case Level::INFO:
                levelStr = "INFO";
                break;
            case Level::WARN:
                levelStr = "WARN";
                break;
            case Level::ERROR:
                levelStr = "ERROR";
                break;
        }

        fprintf(stderr, "[cells:%s] %s\n", levelStr, message.c_str());
    }
};

// Singleton instance
Logger& Logger::instance() {
    static DefaultLogger logger;
    return logger;
}

}  // namespace cells::log

#endif  // !__EMSCRIPTEN__
