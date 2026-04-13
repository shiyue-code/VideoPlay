#ifndef LOGGER_H
#define LOGGER_H

#include <string>
#include <mutex>
#include <fstream>
#include <chrono>
#include <iostream>

namespace VideoPlay {

enum class LogLevel {
    Debug,
    Info,
    Warning,
    Error
};

class Logger {
public:
    static Logger& instance();

    void info(const std::string& message);
    void warning(const std::string& message);
    void error(const std::string& message);
    void debug(const std::string& message);

    void setLogFile(const std::string& path);
    void setEnabled(bool enabled);
    void setConsoleOutput(bool enabled);
    bool isEnabled() const;

private:
    Logger();
    ~Logger();
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    void write(LogLevel level, const std::string& message);
    std::string getCurrentTimestamp();
    std::string levelToString(LogLevel level);

    std::ofstream m_file;
    std::mutex m_mutex;
    bool m_enabled;
    bool m_consoleOutput;
    std::string m_logFilePath;
};

} // namespace VideoPlay

#endif // LOGGER_H
