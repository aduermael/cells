// Cross-platform logging abstraction
// Web: console.log/warn/error via EM_ASM
// Apple: NSLog or os_log
// CLI: stderr

#ifndef CELLS_LOG_LOGGER_H
#define CELLS_LOG_LOGGER_H

#include <cstdarg>
#include <cstdint>

#include <string>

namespace cells::log {

// Log levels matching browser console methods
// Note: Using kX names to avoid conflicts with system macros (DEBUG, ERROR)
enum class Level : std::uint8_t {
    kDebug,  // console.debug - verbose debugging info
    kInfo,   // console.info  - general information
    kWarn,   // console.warn  - warnings
    kError   // console.error - errors
};

// Abstract logger interface - platform implementations override _log()
class Logger {
public:
    virtual ~Logger() = default;

    // Factory method - returns platform-specific singleton
    static Logger& instance();

    // Log with printf-style formatting
    void debug(const char* fmt, ...);
    void info(const char* fmt, ...);
    void warn(const char* fmt, ...);
    void error(const char* fmt, ...);

    // Log with level
    void log(Level level, const char* fmt, ...);
    void logv(Level level, const char* fmt, va_list args);

    // Enable/disable logging (default: enabled)
    void setEnabled(bool enabled) { _enabled = enabled; }
    [[nodiscard]] bool isEnabled() const { return _enabled; }

    // Set minimum log level (default: kDebug - log everything)
    void setMinLevel(Level level) { _minLevel = level; }
    [[nodiscard]] Level getMinLevel() const { return _minLevel; }

protected:
    Logger() = default;

    // Platform-specific log output - must be implemented per platform
    virtual void _log(Level level, const std::string& message) = 0;

    bool _enabled = true;
    Level _minLevel = Level::kDebug;
};

// Convenience macros for common use
#define LOG_DEBUG(...) ::cells::log::Logger::instance().debug(__VA_ARGS__)
#define LOG_INFO(...) ::cells::log::Logger::instance().info(__VA_ARGS__)
#define LOG_WARN(...) ::cells::log::Logger::instance().warn(__VA_ARGS__)
#define LOG_ERROR(...) ::cells::log::Logger::instance().error(__VA_ARGS__)

}  // namespace cells::log

#endif  // CELLS_LOG_LOGGER_H
