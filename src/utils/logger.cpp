#include "utils/logger.h"

#include <QCoreApplication>
#include <QDir>
#include <QDebug>

namespace VideoPlay {

Logger::Logger()
    : m_file(nullptr)
    , m_enabled(true)
{
}

Logger::~Logger()
{
    if (m_file && m_file->isOpen()) {
        m_file->close();
    }
    delete m_file;
}

Logger& Logger::instance()
{
    static Logger instance;
    return instance;
}

void Logger::info(const QString& message)
{
    write("INFO", message);
}

void Logger::warning(const QString& message)
{
    write("WARN", message);
}

void Logger::error(const QString& message)
{
    write("ERROR", message);
}

void Logger::debug(const QString& message)
{
    write("DEBUG", message);
}

void Logger::setLogFile(const QString& path)
{
    QMutexLocker locker(&m_mutex);
    if (m_file && m_file->isOpen()) {
        m_file->close();
    }
    delete m_file;
    m_file = new QFile(path);
    if (m_file->open(QIODevice::Append | QIODevice::Text)) {
        m_stream.setDevice(m_file);
    }
}

void Logger::setEnabled(bool enabled)
{
    m_enabled = enabled;
}

bool Logger::isEnabled() const
{
    return m_enabled;
}

void Logger::write(const QString& level, const QString& message)
{
    if (!m_enabled) return;

    QMutexLocker locker(&m_mutex);
    QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz");
    QString logLine = QString("[%1] [%2] %3").arg(timestamp, level, message);

    qDebug().noquote() << logLine;

    if (m_file && m_file->isOpen()) {
        m_stream << logLine << Qt::endl;
        m_file->flush();
    }
}

} // namespace VideoPlay
