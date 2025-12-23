// Default/CLI logger implementation
// Uses stderr for output (fallback for non-web, non-Apple platforms)

#if !defined(__EMSCRIPTEN__) && !defined(__APPLE__)

#include "core/log/include/Logger.h"

#include <cstdio>

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

#endif  // !__EMSCRIPTEN__ && !__APPLE__
