#include "utils/logger.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

#if defined(VIDEOPLAY_HAS_SPDLOG)
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>
#endif

namespace VideoPlay {

namespace {

std::string toLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

std::string defaultLogFilePath() {
#ifdef _WIN32
    const char* appData = nullptr;
    if (const char* p = getenv("APPDATA"); p) {
        appData = p;
    } else if (const char* p2 = getenv("LOCALAPPDATA"); p2) {
        appData = p2;
    } else if (const char* p3 = getenv("USERPROFILE"); p3) {
        appData = p3;
    }
    if (appData && *appData) {
        return std::string(appData) + "/VideoPlay/logs/videoplay.log";
    }
#else
    const char* home = getenv("HOME");
    if (home && *home) {
        return std::string(home) + "/.local/share/VideoPlay/logs/videoplay.log";
    }
#endif
    return "./logs/videoplay.log";
}

std::string currentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;

    struct tm tm_buf;
#if defined(_WIN32)
    localtime_s(&tm_buf, &time_t_now);
#else
    localtime_r(&time_t_now, &tm_buf);
#endif

    std::stringstream ss;
    ss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S");
    ss << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return ss.str();
}

#if defined(VIDEOPLAY_HAS_SPDLOG)
spdlog::level::level_enum toSpdlogLevel(LogLevel level) {
    switch (level) {
        case LogLevel::Trace:    return spdlog::level::trace;
        case LogLevel::Debug:    return spdlog::level::debug;
        case LogLevel::Info:     return spdlog::level::info;
        case LogLevel::Warning:  return spdlog::level::warn;
        case LogLevel::Error:    return spdlog::level::err;
        case LogLevel::Critical: return spdlog::level::critical;
        case LogLevel::Off:      return spdlog::level::off;
        default:                 return spdlog::level::info;
    }
}
#endif

} // namespace

struct LoggerBackend {
    friend class Logger;

    mutable std::mutex mutex;
    std::ofstream file;
    bool enabled = true;
    bool consoleOutput = true;
    bool flushOnWarning = true;
    size_t maxFileSize = 5 * 1024 * 1024;
    size_t maxFiles = 3;
    LogLevel level = LogLevel::Info;
    std::unordered_map<std::string, LogLevel> moduleLevels;
    std::string logFilePath = defaultLogFilePath();

#if defined(VIDEOPLAY_HAS_SPDLOG)
    std::shared_ptr<spdlog::logger> logger;
#endif

    LoggerBackend() = default;

    ~LoggerBackend() {
        flush();
        if (file.is_open()) {
            file.close();
        }
    }

    void configure(const LogConfig& config) {
        std::lock_guard<std::mutex> lock(mutex);

        enabled = config.enabled;
        consoleOutput = config.consoleOutput;
        level = config.enabled ? Logger::levelFromString(config.level) : LogLevel::Off;
        flushOnWarning = config.flushOnWarning;
        maxFileSize = std::max<size_t>(config.maxFileSize, 1024);
        maxFiles = std::max<size_t>(config.maxFiles, 1);
        logFilePath = config.filePath.empty() ? defaultLogFilePath() : config.filePath;

        moduleLevels.clear();
        for (const auto& [module, moduleLevel] : config.moduleLevels) {
            moduleLevels[module] = Logger::levelFromString(moduleLevel);
        }

        rebuildLocked();
    }

    void setLogFile(const std::string& path) {
        std::lock_guard<std::mutex> lock(mutex);
        logFilePath = path.empty() ? defaultLogFilePath() : path;
        rebuildLocked();
    }

    void setEnabled(bool value) {
        std::lock_guard<std::mutex> lock(mutex);
        enabled = value;

#if defined(VIDEOPLAY_HAS_SPDLOG)
        if (logger) {
            logger->set_level(value ? toSpdlogLevel(level) : spdlog::level::off);
        }
#endif
    }

    void setConsoleOutput(bool value) {
        std::lock_guard<std::mutex> lock(mutex);
        if (consoleOutput == value) {
            return;
        }
        consoleOutput = value;
        rebuildLocked();
    }

    void setLevel(LogLevel value) {
        std::lock_guard<std::mutex> lock(mutex);
        level = value;

#if defined(VIDEOPLAY_HAS_SPDLOG)
        if (logger) {
            logger->set_level(toSpdlogLevel(value));
        }
#endif
    }

    void setModuleLevel(std::string_view module, LogLevel value) {
        std::lock_guard<std::mutex> lock(mutex);
        moduleLevels[std::string(module)] = value;
    }

    void clearModuleLevel(std::string_view module) {
        std::lock_guard<std::mutex> lock(mutex);
        moduleLevels.erase(std::string(module));
    }

    void flush() {
        std::lock_guard<std::mutex> lock(mutex);

#if defined(VIDEOPLAY_HAS_SPDLOG)
        if (logger) {
            logger->flush();
        }
#else
        if (file.is_open()) {
            file.flush();
        }
#endif
    }

    LogLevel effectiveLevelLocked(const std::string& module) const {
        if (!module.empty()) {
            auto it = moduleLevels.find(module);
            if (it != moduleLevels.end()) {
                return it->second;
            }
        }
        return level;
    }

private:
    void rebuildLocked() {
#if defined(VIDEOPLAY_HAS_SPDLOG)
        std::vector<spdlog::sink_ptr> sinks;

        std::filesystem::create_directories(std::filesystem::path(logFilePath).parent_path());
        sinks.push_back(std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            logFilePath, maxFileSize, maxFiles));

        if (consoleOutput) {
            sinks.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
        }

        logger = std::make_shared<spdlog::logger>("VideoPlay", sinks.begin(), sinks.end());
        logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");
        logger->set_level(enabled ? toSpdlogLevel(level) : spdlog::level::off);

        if (flushOnWarning) {
            logger->flush_on(spdlog::level::warn);
        }
#else
        reopenFile();
#endif
    }

    void reopenFile() {
        if (file.is_open()) {
            file.close();
        }

        std::filesystem::create_directories(std::filesystem::path(logFilePath).parent_path());
        file.open(logFilePath, std::ios::out | std::ios::app);
    }
};

namespace {
struct LoggerRegistry {
    std::mutex mutex;
    std::unordered_map<std::string, std::weak_ptr<Logger>> map;
    LoggerRegistry() = default;
    ~LoggerRegistry() = default;
};
} // namespace

Logger& Logger::root() {
    static auto backend = std::make_shared<LoggerBackend>();
    static Logger logger("", backend);
    return logger;
}

std::shared_ptr<Logger> Logger::get(std::string_view name) {
    if (name.empty()) {
        return std::shared_ptr<Logger>(&root(), [](Logger*) {});
    }

    static auto s_registry = std::make_shared<LoggerRegistry>();
    std::lock_guard<std::mutex> lock(s_registry->mutex);
    const std::string key(name);
    auto it = s_registry->map.find(key);
    if (it != s_registry->map.end()) {
        if (auto logger = it->second.lock()) {
            return logger;
        }
    }

    auto logger = std::shared_ptr<Logger>(new Logger(key, root().m_backend));
    s_registry->map[key] = logger;
    return logger;
}

Logger::Logger(std::string name, std::shared_ptr<LoggerBackend> backend)
    : m_name(std::move(name))
    , m_backend(std::move(backend)) {
}

Logger::~Logger() = default;

void Logger::configure(const LogConfig& config) {
    m_backend->configure(config);
}

const std::string& Logger::name() const {
    return m_name;
}

void Logger::log(LogLevel level, std::string_view message) {
    write(level, std::string(message));
}

void Logger::trace(std::string_view message) {
    log(LogLevel::Trace, message);
}

void Logger::debug(std::string_view message) {
    log(LogLevel::Debug, message);
}

void Logger::info(std::string_view message) {
    log(LogLevel::Info, message);
}

void Logger::warning(std::string_view message) {
    log(LogLevel::Warning, message);
}

void Logger::warn(std::string_view message) {
    warning(message);
}

void Logger::error(std::string_view message) {
    log(LogLevel::Error, message);
}

void Logger::critical(std::string_view message) {
    log(LogLevel::Critical, message);
}

void Logger::setLogFile(const std::string& path) {
    m_backend->setLogFile(path);
}

void Logger::setEnabled(bool enabled) {
    m_backend->setEnabled(enabled);
}

void Logger::setConsoleOutput(bool enabled) {
    m_backend->setConsoleOutput(enabled);
}

void Logger::setLevel(LogLevel level) {
    m_backend->setLevel(level);
}

void Logger::setLevel(const std::string& level) {
    setLevel(levelFromString(level));
}

void Logger::setModuleLevel(std::string_view moduleName, LogLevel level) {
    m_backend->setModuleLevel(moduleName, level);
}

void Logger::setModuleLevel(std::string_view moduleName, const std::string& level) {
    setModuleLevel(moduleName, levelFromString(level));
}

void Logger::clearModuleLevel(std::string_view moduleName) {
    m_backend->clearModuleLevel(moduleName);
}

void Logger::flush() {
    m_backend->flush();
}

bool Logger::isEnabled() const {
    std::lock_guard<std::mutex> lock(m_backend->mutex);
    return m_backend->enabled;
}

LogLevel Logger::level() const {
    std::lock_guard<std::mutex> lock(m_backend->mutex);
    return m_backend->level;
}

LogLevel Logger::effectiveLevel() const {
    std::lock_guard<std::mutex> lock(m_backend->mutex);
    return m_backend->effectiveLevelLocked(m_name);
}

std::string Logger::logFilePath() const {
    std::lock_guard<std::mutex> lock(m_backend->mutex);
    return m_backend->logFilePath;
}

LogLevel Logger::levelFromString(const std::string& level) {
    const std::string normalized = toLower(level);
    if (normalized == "trace") return LogLevel::Trace;
    if (normalized == "debug") return LogLevel::Debug;
    if (normalized == "warning" || normalized == "warn") return LogLevel::Warning;
    if (normalized == "error") return LogLevel::Error;
    if (normalized == "critical" || normalized == "fatal") return LogLevel::Critical;
    if (normalized == "off" || normalized == "disabled") return LogLevel::Off;
    return LogLevel::Info;
}

std::string Logger::levelToString(LogLevel level) {
    switch (level) {
        case LogLevel::Trace:    return "TRACE";
        case LogLevel::Debug:    return "DEBUG";
        case LogLevel::Info:     return "INFO";
        case LogLevel::Warning:  return "WARN";
        case LogLevel::Error:    return "ERROR";
        case LogLevel::Critical: return "CRITICAL";
        case LogLevel::Off:      return "OFF";
        default:                 return "UNKNOWN";
    }
}

void Logger::write(LogLevel level, const std::string& message) {
#if defined(VIDEOPLAY_HAS_SPDLOG)
    std::shared_ptr<spdlog::logger> logger;
#endif
    bool enabled = false;
    bool flushOnWarning = false;
    LogLevel threshold = LogLevel::Info;

    {
        std::lock_guard<std::mutex> lock(m_backend->mutex);
        enabled = m_backend->enabled;
        flushOnWarning = m_backend->flushOnWarning;
        threshold = m_backend->effectiveLevelLocked(m_name);
#if defined(VIDEOPLAY_HAS_SPDLOG)
        logger = m_backend->logger;
#endif
    }

    if (!enabled || level < threshold || threshold == LogLevel::Off || level == LogLevel::Off) {
        return;
    }

    const std::string formattedMessage = modulePrefix() + message;

    // 首次写日志时确保文件已打开
    {
        std::lock_guard<std::mutex> lazyLock(m_backend->mutex);
        if (!m_backend->file.is_open()) {
            m_backend->reopenFile();
        }
    }

#if defined(VIDEOPLAY_HAS_SPDLOG)
    if (!logger) {
        return;
    }

    logger->log(toSpdlogLevel(level), formattedMessage);
    if (flushOnWarning && level >= LogLevel::Warning) {
        logger->flush();
    }
#else
    std::lock_guard<std::mutex> lock(m_backend->mutex);

    const std::string formatted = "[" + currentTimestamp() + "] [" +
                                  levelToString(level) + "] " + formattedMessage;

    if (!m_backend->file.is_open()) {
        m_backend->reopenFile();
    }

    if (m_backend->file.is_open()) {
        m_backend->file << formatted << '\n';
        if (flushOnWarning && level >= LogLevel::Warning) {
            m_backend->file.flush();
        }
    }

    if (m_backend->consoleOutput) {
        if (level >= LogLevel::Error) {
            std::cerr << formatted << std::endl;
        } else {
            std::cout << formatted << std::endl;
        }
    }
#endif
}

std::string Logger::modulePrefix() const {
    if (m_name.empty()) {
        return std::string();
    }
    return "[" + m_name + "] ";
}

} // namespace VideoPlay
