// Apple platform logger implementation (iOS/macOS)
// Uses NSLog for output

#if defined(__APPLE__)

#include "core/log/include/Logger.h"

#import <Foundation/Foundation.h>

namespace cells::log {

class AppleLogger : public Logger {
protected:
    void _log(Level level, const std::string& message) override {
        NSString* nsMessage = [NSString stringWithUTF8String:message.c_str()];

        // Prefix with level for easy filtering in Console.app
        switch (level) {
            case Level::DEBUG:
                NSLog(@"[cells:DEBUG] %@", nsMessage);
                break;

            case Level::INFO:
                NSLog(@"[cells:INFO] %@", nsMessage);
                break;

            case Level::WARN:
                NSLog(@"[cells:WARN] %@", nsMessage);
                break;

            case Level::ERROR:
                NSLog(@"[cells:ERROR] %@", nsMessage);
                break;
        }
    }
};

// Singleton instance
Logger& Logger::instance() {
    static AppleLogger logger;
    return logger;
}

}  // namespace cells::log

#endif  // __APPLE__
