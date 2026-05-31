#ifndef LOGGER_H
#define LOGGER_H

#include "core/settings.h"

#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#if defined(VIDEOPLAY_HAS_SPDLOG)
namespace spdlog {
class logger;
}
#endif

namespace VideoPlay {

enum class LogLevel {
    Trace,
    Debug,
    Info,
    Warning,
    Error,
    Critical,
    Off
};

struct LoggerBackend;

class Logger {
public:
    static Logger& root();
    static std::shared_ptr<Logger> get(std::string_view name);

    void configure(const LogConfig& config);

    const std::string& name() const;

    void log(LogLevel level, std::string_view message);
    void trace(std::string_view message);
    void debug(std::string_view message);
    void info(std::string_view message);
    void warning(std::string_view message);
    void warn(std::string_view message);
    void error(std::string_view message);
    void critical(std::string_view message);

    template <typename... Args,
              typename = std::enable_if_t<(sizeof...(Args) > 0)>>
    void log(LogLevel level, std::string_view format, Args&&... args) {
        write(level, formatMessage(format, std::forward<Args>(args)...));
    }

    template <typename... Args>
    void trace(std::string_view format, Args&&... args) {
        log(LogLevel::Trace, format, std::forward<Args>(args)...);
    }

    template <typename... Args>
    void debug(std::string_view format, Args&&... args) {
        log(LogLevel::Debug, format, std::forward<Args>(args)...);
    }

    template <typename... Args>
    void info(std::string_view format, Args&&... args) {
        log(LogLevel::Info, format, std::forward<Args>(args)...);
    }

    template <typename... Args>
    void warning(std::string_view format, Args&&... args) {
        log(LogLevel::Warning, format, std::forward<Args>(args)...);
    }

    template <typename... Args>
    void warn(std::string_view format, Args&&... args) {
        warning(format, std::forward<Args>(args)...);
    }

    template <typename... Args>
    void error(std::string_view format, Args&&... args) {
        log(LogLevel::Error, format, std::forward<Args>(args)...);
    }

    template <typename... Args>
    void critical(std::string_view format, Args&&... args) {
        log(LogLevel::Critical, format, std::forward<Args>(args)...);
    }

    void setLogFile(const std::string& path);
    void setEnabled(bool enabled);
    void setConsoleOutput(bool enabled);
    void setLevel(LogLevel level);
    void setLevel(const std::string& level);
    void setModuleLevel(std::string_view moduleName, LogLevel level);
    void setModuleLevel(std::string_view moduleName, const std::string& level);
    void clearModuleLevel(std::string_view moduleName);
    void flush();

    bool isEnabled() const;
    LogLevel level() const;
    LogLevel effectiveLevel() const;
    std::string logFilePath() const;

    static LogLevel levelFromString(const std::string& level);
    static std::string levelToString(LogLevel level);

    ~Logger();

private:
    Logger(std::string name, std::shared_ptr<LoggerBackend> backend);
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    void write(LogLevel level, const std::string& message);
    std::string modulePrefix() const;

    template <typename T>
    static std::string argumentToString(T&& value) {
        using ValueType = std::decay_t<T>;

        if constexpr (std::is_same_v<ValueType, std::string>) {
            return value;
        } else if constexpr (std::is_same_v<ValueType, std::string_view>) {
            return std::string(value);
        } else if constexpr (std::is_same_v<ValueType, const char*> ||
                             std::is_same_v<ValueType, char*>) {
            return value ? std::string(value) : std::string("(null)");
        } else if constexpr (std::is_same_v<ValueType, bool>) {
            return value ? "true" : "false";
        } else {
            std::ostringstream stream;
            stream << std::forward<T>(value);
            return stream.str();
        }
    }

    template <typename... Args>
    static std::string formatMessage(std::string_view format, Args&&... args) {
        std::vector<std::string> values{argumentToString(std::forward<Args>(args))...};
        std::string result;
        result.reserve(format.size() + values.size() * 8);

        size_t argIndex = 0;
        for (size_t i = 0; i < format.size(); ++i) {
            if (format[i] == '{' && i + 1 < format.size() && format[i + 1] == '}') {
                if (argIndex < values.size()) {
                    result += values[argIndex++];
                } else {
                    result += "{}";
                }
                ++i;
                continue;
            }

            if (format[i] == '{' && i + 1 < format.size() && format[i + 1] == '{') {
                result += '{';
                ++i;
                continue;
            }

            if (format[i] == '}' && i + 1 < format.size() && format[i + 1] == '}') {
                result += '}';
                ++i;
                continue;
            }

            result += format[i];
        }

        while (argIndex < values.size()) {
            result += ' ';
            result += values[argIndex++];
        }

        return result;
    }

    std::string m_name;
    std::shared_ptr<LoggerBackend> m_backend;
};

} // namespace VideoPlay

#endif // LOGGER_H
