#ifndef LOGGER_H
#define LOGGER_H

#include <QString>
#include <QDateTime>
#include <QFile>
#include <QTextStream>
#include <QMutex>

namespace VideoPlay {

class Logger {
public:
    static Logger& instance();

    void info(const QString& message);
    void warning(const QString& message);
    void error(const QString& message);
    void debug(const QString& message);

    void setLogFile(const QString& path);
    void setEnabled(bool enabled);
    bool isEnabled() const;

private:
    Logger();
    ~Logger();
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    void write(const QString& level, const QString& message);

    QFile* m_file;
    QTextStream m_stream;
    QMutex m_mutex;
    bool m_enabled;
};

} // namespace VideoPlay

#endif // LOGGER_H
