#include "utils/logger.h"
#include <ctime>
#include <iomanip>
#include <sstream>
#include <filesystem>

namespace VideoPlay {

Logger& Logger::instance() {
    static Logger instance;
    return instance;
}

Logger::Logger()
    : m_enabled(true)
    , m_consoleOutput(true) {
    std::string logDir;
    
#ifdef _WIN32
    const char* appData = getenv("APPDATA");
    if (appData) {
        logDir = std::string(appData) + "/VideoPlay/logs";
    } else {
        logDir = "./logs";
    }
#else
    const char* home = getenv("HOME");
    if (home) {
        logDir = std::string(home) + "/.local/share/VideoPlay/logs";
    } else {
        logDir = "./logs";
    }
#endif

    std::filesystem::create_directories(logDir);
    setLogFile(logDir + "/videoplay.log");
}

Logger::~Logger() {
    if (m_file.is_open()) {
        m_file.close();
    }
}

void Logger::setLogFile(const std::string& path) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_file.is_open()) {
        m_file.close();
    }
    m_logFilePath = path;
    m_file.open(path, std::ios::out | std::ios::app);
}

void Logger::setEnabled(bool enabled) {
    m_enabled = enabled;
}

void Logger::setConsoleOutput(bool enabled) {
    m_consoleOutput = enabled;
}

bool Logger::isEnabled() const {
    return m_enabled;
}

void Logger::info(const std::string& message) {
    write(LogLevel::Info, message);
}

void Logger::warning(const std::string& message) {
    write(LogLevel::Warning, message);
}

void Logger::error(const std::string& message) {
    write(LogLevel::Error, message);
}

void Logger::debug(const std::string& message) {
    write(LogLevel::Debug, message);
}

std::string Logger::getCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    
    std::stringstream ss;
    ss << std::put_time(std::localtime(&time_t_now), "%Y-%m-%d %H:%M:%S");
    ss << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return ss.str();
}

std::string Logger::levelToString(LogLevel level) {
    switch (level) {
        case LogLevel::Debug:   return "DEBUG";
        case LogLevel::Info:    return "INFO";
        case LogLevel::Warning: return "WARN";
        case LogLevel::Error:   return "ERROR";
        default:                return "UNKNOWN";
    }
}

void Logger::write(LogLevel level, const std::string& message) {
    if (!m_enabled) return;

    std::lock_guard<std::mutex> lock(m_mutex);

    std::string timestamp = getCurrentTimestamp();
    std::string levelStr = levelToString(level);
    std::string formatted = "[" + timestamp + "] [" + levelStr + "] " + message;

    if (m_file.is_open()) {
        m_file << formatted << '\n';
        // Only flush for Error/Warning to avoid I/O overhead in hot paths (debug logs)
        if (level == LogLevel::Error || level == LogLevel::Warning) {
            m_file.flush();
        }
    }

    if (m_consoleOutput) {
        if (level == LogLevel::Error) {
            std::cerr << formatted << std::endl;
        } else {
            std::cout << formatted << std::endl;
        }
    }
}

} // namespace VideoPlay
