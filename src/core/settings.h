#ifndef SETTINGS_H
#define SETTINGS_H

#include <QSettings>
#include <QVariant>
#include <QString>
#include <QPoint>
#include <QSize>
#include <QRect>
#include <QStringList>

namespace VideoPlay {

class Settings : public QObject {
    Q_OBJECT

public:
    static Settings& instance();

    // Window
    void setWindowGeometry(const QRect& geometry);
    QRect windowGeometry() const;
    void setWindowState(int state);
    int windowState() const;

    // Playback
    void setVolume(int volume);
    int volume() const;
    void setMuted(bool muted);
    bool isMuted() const;
    void setPlaybackSpeed(double speed);
    double playbackSpeed() const;

    // Recent files
    void addRecentFile(const QString& path);
    QStringList recentFiles() const;
    void clearRecentFiles();

    // Subtitles
    void setSubtitleFontFamily(const QString& family);
    QString subtitleFontFamily() const;
    void setSubtitleFontSize(int size);
    int subtitleFontSize() const;
    void setSubtitleFontColor(const QString& color);
    QString subtitleFontColor() const;

    // General
    void setRememberPosition(bool remember);
    bool rememberPosition() const;
    void setLastPosition(const QString& filePath, qint64 position);
    qint64 lastPosition(const QString& filePath) const;

    // Reset
    void reset();

private:
    explicit Settings(QObject* parent = nullptr);
    ~Settings();
    Settings(const Settings&) = delete;
    Settings& operator=(const Settings&) = delete;

    QSettings* m_settings;
};

} // namespace VideoPlay

#endif // SETTINGS_H
