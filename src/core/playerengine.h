#ifndef PLAYERENGINE_H
#define PLAYERENGINE_H

#include <QObject>
#include <QString>
#include <QMediaPlayer>
#include <QAudioOutput>
#include "core/common.h"

namespace VideoPlay {

class PlayerEngine : public QObject {
    Q_OBJECT

public:
    explicit PlayerEngine(QObject* parent = nullptr);
    ~PlayerEngine();

    bool loadFile(const QString& filePath);
    void play();
    void pause();
    void stop();
    void seek(qint64 position);

    void setPlaybackSpeed(double speed);
    void setVolume(int volume);
    void setMuted(bool muted);

    PlaybackState state() const;
    qint64 position() const;
    qint64 duration() const;
    QString filePath() const;
    double playbackSpeed() const;
    int volume() const;
    bool isMuted() const;
    QMediaPlayer* mediaPlayer() const { return m_mediaPlayer; }

signals:
    void stateChanged(PlaybackState state);
    void positionChanged(qint64 position);
    void durationChanged(qint64 duration);
    void errorOccurred(const QString& error);
    void playbackSpeedChanged(double speed);
    void volumeChanged(int volume);
    void muteChanged(bool muted);
    void fileLoaded(const QString& filePath);

private slots:
    void onMediaStatusChanged(QMediaPlayer::MediaStatus status);
    void onPositionChanged(qint64 position);
    void onDurationChanged(qint64 duration);
    void onErrorOccurred(QMediaPlayer::Error error, const QString& errorString);
    void onPlaybackStateChanged(QMediaPlayer::PlaybackState state);

private:
    QMediaPlayer* m_mediaPlayer;
    QAudioOutput* m_audioOutput;
    QString m_filePath;
    PlaybackState m_state;
    double m_playbackSpeed;
    int m_volume;
    bool m_muted;
    bool m_autoPlayAfterLoad;  // 加载完成后自动播放
};

} // namespace VideoPlay

#endif // PLAYERENGINE_H