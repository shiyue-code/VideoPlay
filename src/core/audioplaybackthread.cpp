#include "core/audioplaybackthread.h"
#include <QDebug>

namespace VideoPlay {

AudioPlaybackThread::AudioPlaybackThread(const QAudioFormat& format, QObject* parent)
    : QThread(parent)
    , m_format(format)
    , m_audioSink(nullptr)
    , m_audioDevice(nullptr)
    , m_stopRequested(false)
    , m_started(false)
{
}

AudioPlaybackThread::~AudioPlaybackThread()
{
    stop();
    wait(3000);
}

void AudioPlaybackThread::enqueue(const QByteArray& audioData)
{
    QMutexLocker locker(&m_mutex);
    // Block if queue is full to prevent memory bloat and maintain sync
    while (m_queue.size() >= MAX_QUEUE_SIZE && !m_stopRequested) {
        m_condition.wait(&m_mutex, 10);
    }
    if (m_stopRequested) return;
    
    m_queue.enqueue(audioData);
    m_condition.wakeOne();
}

void AudioPlaybackThread::stop()
{
    {
        QMutexLocker locker(&m_mutex);
        m_stopRequested = true;
    }
    m_condition.wakeAll();
}

bool AudioPlaybackThread::isRunning() const
{
    return m_started;
}

int AudioPlaybackThread::queueSize() const
{
    QMutexLocker locker(&m_mutex);
    return m_queue.size();
}

bool AudioPlaybackThread::isQueueFull() const
{
    QMutexLocker locker(&m_mutex);
    return m_queue.size() >= MAX_QUEUE_SIZE;
}

void AudioPlaybackThread::run()
{
    m_started = true;
    qDebug() << "AudioPlaybackThread started";

    // Create audio sink in this thread
    m_audioSink = new QAudioSink(m_format, this);
    // Increase buffer size for smoother playback (about 1 second of audio)
    int bufferSize = qMax(384000, m_format.sampleRate() * m_format.channelCount() * 2);
    m_audioSink->setBufferSize(bufferSize);
    qDebug() << "Audio buffer size set to:" << bufferSize;
    
    // Start audio device
    m_audioDevice = m_audioSink->start();
    if (!m_audioDevice) {
        qDebug() << "Failed to start audio device in AudioPlaybackThread";
        emit errorOccurred("Failed to start audio device");
        m_started = false;
        return;
    }
    
    qDebug() << "Audio device started in playback thread";

    // Pre-buffer: wait until we have some data before starting playback
    bool preBuffered = false;
    
    // Main playback loop
    while (!m_stopRequested) {
        QByteArray data;
        
        {
            QMutexLocker locker(&m_mutex);
            if (m_queue.isEmpty()) {
                // Use shorter timeout for more responsive stop
                m_condition.wait(&m_mutex, 10);
                if (m_stopRequested) break;
                continue;
            }
            data = m_queue.dequeue();
            
            // Signal that queue has space for more data
            if (m_queue.size() < MAX_QUEUE_SIZE / 2) {
                m_condition.wakeOne();
            }
        }

        if (!data.isEmpty() && m_audioDevice && m_audioDevice->isOpen()) {
            qint64 bytesWritten = 0;
            // Write all data (handle partial writes)
            while (bytesWritten < data.size() && !m_stopRequested) {
                qint64 written = m_audioDevice->write(data.constData() + bytesWritten, 
                                                       data.size() - bytesWritten);
                if (written < 0) {
                    qDebug() << "Audio write error in playback thread";
                    break;
                } else if (written == 0) {
                    // Buffer full, wait a bit
                    QThread::usleep(1000); // 1ms
                } else {
                    bytesWritten += written;
                }
            }
        }
    }

    // Cleanup
    if (m_audioSink) {
        m_audioSink->stop();
    }
    
    qDebug() << "AudioPlaybackThread stopped";
    m_started = false;
}

} // namespace VideoPlay