#ifndef AUDIOPLAYBACKTHREAD_H
#define AUDIOPLAYBACKTHREAD_H

#include <QThread>
#include <QMutex>
#include <QWaitCondition>
#include <QQueue>
#include <QByteArray>
#include <QAudioSink>
#include <QAudioFormat>
#include <QIODevice>

namespace VideoPlay {

// Maximum audio queue size to prevent memory bloat (about 2 seconds of audio)
constexpr int MAX_QUEUE_SIZE = 100;

class AudioPlaybackThread : public QThread {
    Q_OBJECT

public:
    explicit AudioPlaybackThread(const QAudioFormat& format, QObject* parent = nullptr);
    ~AudioPlaybackThread() override;

    void enqueue(const QByteArray& audioData);
    void stop();
    bool isRunning() const;
    int queueSize() const;
    bool isQueueFull() const;

signals:
    void errorOccurred(const QString& error);

protected:
    void run() override;

private:
    QAudioFormat m_format;

    QQueue<QByteArray> m_queue;
    mutable QMutex m_mutex;
    QWaitCondition m_condition;
    bool m_stopRequested;
    bool m_started;
};

} // namespace VideoPlay

#endif // AUDIOPLAYBACKTHREAD_H