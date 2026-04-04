#include "core/settings.h"

#include <QCoreApplication>

namespace VideoPlay {

Settings::Settings(QObject* parent)
    : QObject(parent)
    , m_settings(new QSettings("VideoPlay", "VideoPlay", this))
{
}

Settings::~Settings() = default;

Settings& Settings::instance()
{
    static Settings instance;
    return instance;
}

void Settings::setWindowGeometry(const QRect& geometry)
{
    m_settings->setValue("window/geometry", geometry);
    m_settings->setValue("window/state", 0);
}

QRect Settings::windowGeometry() const
{
    return m_settings->value("window/geometry", QRect(960, 640, 960, 640)).toRect();
}

void Settings::setWindowState(int state)
{
    m_settings->setValue("window/state", state);
}

int Settings::windowState() const
{
    return m_settings->value("window/state", 0).toInt();
}

void Settings::setVolume(int volume)
{
    m_settings->setValue("playback/volume", volume);
}

int Settings::volume() const
{
    return m_settings->value("playback/volume", 75).toInt();
}

void Settings::setMuted(bool muted)
{
    m_settings->setValue("playback/muted", muted);
}

bool Settings::isMuted() const
{
    return m_settings->value("playback/muted", false).toBool();
}

void Settings::setPlaybackSpeed(double speed)
{
    m_settings->setValue("playback/speed", speed);
}

double Settings::playbackSpeed() const
{
    return m_settings->value("playback/speed", 1.0).toDouble();
}

void Settings::addRecentFile(const QString& path)
{
    QStringList files = recentFiles();
    files.removeAll(path);
    files.prepend(path);
    while (files.size() > 10)
        files.removeLast();
    m_settings->setValue("recent/files", files);
}

QStringList Settings::recentFiles() const
{
    return m_settings->value("recent/files").toStringList();
}

void Settings::clearRecentFiles()
{
    m_settings->remove("recent/files");
}

void Settings::setSubtitleFontFamily(const QString& family)
{
    m_settings->setValue("subtitles/fontFamily", family);
}

QString Settings::subtitleFontFamily() const
{
    return m_settings->value("subtitles/fontFamily", "Arial").toString();
}

void Settings::setSubtitleFontSize(int size)
{
    m_settings->setValue("subtitles/fontSize", size);
}

int Settings::subtitleFontSize() const
{
    return m_settings->value("subtitles/fontSize", 24).toInt();
}

void Settings::setSubtitleFontColor(const QString& color)
{
    m_settings->setValue("subtitles/fontColor", color);
}

QString Settings::subtitleFontColor() const
{
    return m_settings->value("subtitles/fontColor", "#FFFFFF").toString();
}

void Settings::setRememberPosition(bool remember)
{
    m_settings->setValue("general/rememberPosition", remember);
}

bool Settings::rememberPosition() const
{
    return m_settings->value("general/rememberPosition", true).toBool();
}

void Settings::setLastPosition(const QString& filePath, qint64 position)
{
    m_settings->setValue(QString("positions/%1").arg(filePath), position);
}

qint64 Settings::lastPosition(const QString& filePath) const
{
    return m_settings->value(QString("positions/%1").arg(filePath), 0).toLongLong();
}

void Settings::reset()
{
    m_settings->clear();
}

} // namespace VideoPlay
