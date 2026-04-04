#include "playerengine.h"
#include <QUrl>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QMimeType>
#include <QMimeDatabase>

namespace VideoPlay {

PlayerEngine::PlayerEngine(QObject* parent)
    : QObject(parent)
    , m_mediaPlayer(new QMediaPlayer(this))
    , m_audioOutput(new QAudioOutput(this))
    , m_videoSink(new QVideoSink(this))
    , m_state(PlaybackState::Stopped)
    , m_playbackSpeed(1.0)
    , m_volume(100)
    , m_muted(false)
    , m_autoPlayAfterLoad(false)
{
    m_mediaPlayer->setAudioOutput(m_audioOutput);
    m_mediaPlayer->setVideoOutput(m_videoSink);
    
    connect(m_mediaPlayer, &QMediaPlayer::mediaStatusChanged,
            this, &PlayerEngine::onMediaStatusChanged);
    connect(m_mediaPlayer, &QMediaPlayer::positionChanged,
            this, &PlayerEngine::onPositionChanged);
    connect(m_mediaPlayer, &QMediaPlayer::durationChanged,
            this, &PlayerEngine::onDurationChanged);
    connect(m_mediaPlayer, &QMediaPlayer::errorOccurred,
            this, &PlayerEngine::onErrorOccurred);
    connect(m_mediaPlayer, &QMediaPlayer::playbackStateChanged,
            this, &PlayerEngine::onPlaybackStateChanged);
    
    m_audioOutput->setVolume(m_volume / 100.0);
    
    qDebug() << "PlayerEngine initialized";
}

PlayerEngine::~PlayerEngine()
{
    if (m_mediaPlayer->playbackState() != QMediaPlayer::StoppedState) {
        m_mediaPlayer->stop();
    }
}

bool PlayerEngine::loadFile(const QString& filePath)
{
    QFileInfo fileInfo(filePath);
    QString absolutePath = fileInfo.absoluteFilePath();
    QString suffix = fileInfo.suffix().toLower();
    QUrl url = QUrl::fromLocalFile(absolutePath);
    
    qDebug() << "File extension:" << suffix;
    
    QMimeDatabase mimeDb;
    QMimeType mimeType = mimeDb.mimeTypeForFile(fileInfo);
    qDebug() << "MIME type:" << mimeType.name();
    
    qDebug() << "Absolute path:" << absolutePath;
    qDebug() << "URL:" << url;
    qDebug() << "URL is valid:" << url.isValid();
    qDebug() << "File exists:" << QFile::exists(absolutePath);
    
    if (!url.isValid() || !QFile::exists(absolutePath)) {
        emit errorOccurred(QString("Invalid file: %1").arg(filePath));
        return false;
    }
    
    m_filePath = filePath;
    m_autoPlayAfterLoad = true;
    
    m_mediaPlayer->setSource(url);
    
    qDebug() << "Source set, media status:" << m_mediaPlayer->mediaStatus();
    
    emit fileLoaded(filePath);
    return true;
}

void PlayerEngine::play()
{
    qDebug() << "play() called, media status:" << m_mediaPlayer->mediaStatus();
    
    // 如果媒体还没准备好加载，等待加载完成后再播放
    if (m_mediaPlayer->mediaStatus() == QMediaPlayer::NoMedia) {
        qDebug() << "No media, setting auto-play flag";
        m_autoPlayAfterLoad = true;
        return;
    }
    
    // 如果媒体正在加载中，也等待加载完成
    if (m_mediaPlayer->mediaStatus() == QMediaPlayer::LoadingMedia ||
        m_mediaPlayer->mediaStatus() == QMediaPlayer::StalledMedia) {
        qDebug() << "Media loading, setting auto-play flag";
        m_autoPlayAfterLoad = true;
        return;
    }
    
    // 媒体已准备好，直接播放
    m_mediaPlayer->play();
}

void PlayerEngine::pause()
{
    m_mediaPlayer->pause();
}

void PlayerEngine::stop()
{
    m_mediaPlayer->stop();
}

void PlayerEngine::seek(qint64 position)
{
    if (position < 0) position = 0;
    qint64 dur = duration();
    if (dur > 0 && position > dur) position = dur;
    m_mediaPlayer->setPosition(position);
}

void PlayerEngine::setPlaybackSpeed(double speed)
{
    if (speed < 0.25) speed = 0.25;
    if (speed > 4.0) speed = 4.0;
    
    m_playbackSpeed = speed;
    m_mediaPlayer->setPlaybackRate(speed);
    emit playbackSpeedChanged(speed);
}

void PlayerEngine::setVolume(int volume)
{
    if (volume < 0) volume = 0;
    if (volume > 100) volume = 100;
    
    m_volume = volume;
    m_audioOutput->setVolume(volume / 100.0);
    emit volumeChanged(volume);
}

void PlayerEngine::setMuted(bool muted)
{
    m_muted = muted;
    m_audioOutput->setMuted(muted);
    emit muteChanged(muted);
}

PlaybackState PlayerEngine::state() const
{
    return m_state;
}

qint64 PlayerEngine::position() const
{
    return m_mediaPlayer->position();
}

qint64 PlayerEngine::duration() const
{
    return m_mediaPlayer->duration();
}

QString PlayerEngine::filePath() const
{
    return m_filePath;
}

double PlayerEngine::playbackSpeed() const
{
    return m_playbackSpeed;
}

int PlayerEngine::volume() const
{
    return m_volume;
}

bool PlayerEngine::isMuted() const
{
    return m_muted;
}

void PlayerEngine::onMediaStatusChanged(QMediaPlayer::MediaStatus status)
{
    qDebug() << "Media status changed:" << status;
    switch (status) {
        case QMediaPlayer::LoadedMedia:
        case QMediaPlayer::BufferedMedia:
            // Media ready, state remains as current playback state
            qDebug() << "Media loaded/buffered";
            // 如果设置了自动播放标志，则开始播放
            if (m_autoPlayAfterLoad) {
                m_autoPlayAfterLoad = false;
                qDebug() << "Auto-playing after load";
                m_mediaPlayer->play();
            }
            break;
        case QMediaPlayer::EndOfMedia:
            m_state = PlaybackState::Stopped;
            emit stateChanged(m_state);
            qDebug() << "End of media";
            break;
        case QMediaPlayer::InvalidMedia:
            emit errorOccurred("Invalid media");
            qDebug() << "Invalid media";
            break;
        default:
            break;
    }
}

void PlayerEngine::onPositionChanged(qint64 position)
{
    emit positionChanged(position);
}

void PlayerEngine::onDurationChanged(qint64 duration)
{
    emit durationChanged(duration);
}

void PlayerEngine::onErrorOccurred(QMediaPlayer::Error error, const QString& errorString)
{
    Q_UNUSED(error)
    emit errorOccurred(errorString);
}

void PlayerEngine::onPlaybackStateChanged(QMediaPlayer::PlaybackState state)
{
    PlaybackState newState;
    switch (state) {
        case QMediaPlayer::PlayingState:
            newState = PlaybackState::Playing;
            break;
        case QMediaPlayer::PausedState:
            newState = PlaybackState::Paused;
            break;
        case QMediaPlayer::StoppedState:
            newState = PlaybackState::Stopped;
            break;
        default:
            return;
    }
    
    if (m_state != newState) {
        m_state = newState;
        emit stateChanged(newState);
    }
}

} // namespace VideoPlay