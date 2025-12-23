// Web platform logger implementation
// Uses EM_ASM to call browser console methods directly

#ifdef __EMSCRIPTEN__

#include "core/log/include/Logger.h"

#include <emscripten.h>

namespace cells::log {

class WebLogger : public Logger {
protected:
    void _log(Level level, const std::string& message) override {
        const char* msg = message.c_str();

        switch (level) {
            case Level::DEBUG:
                EM_ASM(
                    { console.debug(UTF8ToString($0)); },
                    msg);
                break;

            case Level::INFO:
                EM_ASM(
                    { console.info(UTF8ToString($0)); },
                    msg);
                break;

            case Level::WARN:
                EM_ASM(
                    { console.warn(UTF8ToString($0)); },
                    msg);
                break;

            case Level::ERROR:
                EM_ASM(
                    { console.error(UTF8ToString($0)); },
                    msg);
                break;
        }
    }
};

// Singleton instance
Logger& Logger::instance() {
    static WebLogger logger;
    return logger;
}

}  // namespace cells::log

#endif  // __EMSCRIPTEN__
