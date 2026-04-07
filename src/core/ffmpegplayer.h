#ifndef FFMPEGPLAYER_H
#define FFMPEGPLAYER_H

#include <QObject>
#include <QThread>
#include <QMutex>
#include <QWaitCondition>
#include <QTimer>
#include <QAudioSink>
#include <QAudioFormat>
#include <QIODevice>
#include <QVector>
#include <QImage>

#include "core/common.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/time.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
}

namespace VideoPlay {

class FFmpegPlayer : public QObject {
    Q_OBJECT

public:
    explicit FFmpegPlayer(QObject* parent = nullptr);
    ~FFmpegPlayer() override;

    bool loadFile(const QString& filePath);
    void play();
    void pause();
    void stop();
    void seek(qint64 position); // position in milliseconds

    PlaybackState state() const;
    qint64 position() const; // current position in milliseconds
    qint64 duration() const; // total duration in milliseconds
    QString filePath() const;
    double playbackSpeed() const;
    int volume() const;
    bool isMuted() const;

    void setPlaybackSpeed(double speed);
    void setVolume(int volume);
    void setMuted(bool muted);

signals:
    void stateChanged(PlaybackState state);
    void positionChanged(qint64 position);
    void durationChanged(qint64 duration);
    void errorOccurred(const QString& error);
    void playbackSpeedChanged(double speed);
    void volumeChanged(int volume);
    void muteChanged(bool muted);
    void fileLoaded(const QString& filePath);
    void videoFrameReady(const QImage& frame, qint64 pts);
    void audioDataReady(const QVector<char>& data, qint64 pts);
    void writeAudioData(const QByteArray& data);

private slots:
    void onAudioSinkStateChanged(QAudio::State state);
    void onWriteAudioData(const QByteArray& data);

private:
    struct VideoStreamData {
        AVStream* stream = nullptr;
        AVCodecContext* codecContext = nullptr;
        SwsContext* swsContext = nullptr;
        int64_t startTime = 0;
        bool isKeyframeRequired = false;
    };

    struct AudioStreamData {
        AVStream* stream = nullptr;
        AVCodecContext* codecContext = nullptr;
        SwrContext* resampleContext = nullptr;
        int64_t startTime = 0;
        QAudioFormat audioFormat;
    };

    struct DecodeThread : public QThread {
        FFmpegPlayer* player;
        DecodeThread(FFmpegPlayer* p) : player(p) {}
        void run() override;
    };

    void initialize();
    void cleanup();
    void openFile(const QString& filePath);
    void closeFile();
    void decodeLoop();
    bool initializeVideoStream(VideoStreamData& vsd);
    bool initializeAudioStream(AudioStreamData& asd);
    QImage convertVideoFrame(AVFrame* frame, VideoStreamData& vsd);
    QAudioFormat getAudioFormatFromCodec(AVCodecContext* codecCtx);
    QVector<char> resampleAudio(AVFrame* frame, AudioStreamData& asd, int& numSamples);
    void managePlaybackSpeed(double speed);
    void handleSeek(qint64 position);
    void broadcastPosition();

    // Clock synchronization
    double getAudioClock() const;
    double getVideoClock() const;
    double getExternalClock() const;
    void synchronizeClocks();

    AVFormatContext* m_formatContext;
    DecodeThread* m_decodeThread;
    QMutex m_mutex;
    QWaitCondition m_condition;

    VideoStreamData m_videoStream;
    AudioStreamData m_audioStream;

    QString m_filePath;
    qint64 m_duration;
    qint64 m_position;
    double m_playbackSpeed;
    int m_volume;
    bool m_muted;
    PlaybackState m_state;
    bool m_autoPlayAfterLoad;
    bool m_seekRequested;
    qint64 m_seekPosition;
    bool m_abortRequest;

    QAudioSink* m_audioSink;
    QIODevice* m_audioDevice;
    QVector<char> m_audioBuffer;

    double m_baseTime; // Reference clock base
    double m_clock;    // Master clock (usually audio clock)
    double m_audioClock;
    qint64 m_lastVideoPts;
    qint64 m_startTime;
};

} // namespace VideoPlay

#endif // FFMPEGPLAYER_H
