#ifndef PLAYERENGINE_H
#define PLAYERENGINE_H

#include <QObject>
#include <QString>
#include "core/common.h"

// Forward declaration
class QAudioSink;

namespace VideoPlay {

class FFmpegPlayer;

class PlayerEngine : public QObject {
    Q_OBJECT

public:
    explicit PlayerEngine(QObject* parent = nullptr);
    ~PlayerEngine() override;

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

signals:
    void stateChanged(PlaybackState state);
    void positionChanged(qint64 position);
    void durationChanged(qint64 duration);
    void errorOccurred(const QString& error);
    void playbackSpeedChanged(double speed);
    void volumeChanged(int volume);
    void muteChanged(bool muted);
    void fileLoaded(const QString& filePath);
    void videoFrameReady(const QImage& frame);

private slots:
    void onPositionChanged(qint64 position);
    void onDurationChanged(qint64 duration);
    void onErrorOccurred(const QString& error);
    void onPlaybackStateChanged(PlaybackState state);
    void onVideoFrameReady(const QImage& frame, qint64 pts);

private:
    FFmpegPlayer* m_player;
    QString m_filePath;
    PlaybackState m_state;
    double m_playbackSpeed;
    int m_volume;
    bool m_muted;
    bool m_autoPlayAfterLoad;
    void propagateState(PlaybackState state);
};

} // namespace VideoPlay

#endif // PLAYERENGINE_H