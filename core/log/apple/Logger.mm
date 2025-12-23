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
            case Level::kDebug:
                NSLog(@"[cells:DEBUG] %@", nsMessage);
                break;

            case Level::kInfo:
                NSLog(@"[cells:INFO] %@", nsMessage);
                break;

            case Level::kWarn:
                NSLog(@"[cells:WARN] %@", nsMessage);
                break;

            case Level::kError:
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
