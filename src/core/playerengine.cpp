#include "core/playerengine.h"
#include "core/ffmpegplayer.h"
#include <QDebug>
#include <QFileInfo>

namespace VideoPlay {

PlayerEngine::PlayerEngine(QObject* parent)
    : QObject(parent)
    , m_player(new FFmpegPlayer(this))
    , m_state(PlaybackState::Stopped)
    , m_playbackSpeed(1.0)
    , m_volume(100)
    , m_muted(false)
    , m_autoPlayAfterLoad(false)
{
    // Connect FFmpegPlayer signals to our signals
    connect(m_player, &FFmpegPlayer::stateChanged,
            this, &PlayerEngine::onPlaybackStateChanged);
    connect(m_player, &FFmpegPlayer::positionChanged,
            this, &PlayerEngine::onPositionChanged);
    connect(m_player, &FFmpegPlayer::durationChanged,
            this, &PlayerEngine::onDurationChanged);
    connect(m_player, &FFmpegPlayer::errorOccurred,
            this, &PlayerEngine::onErrorOccurred);
    connect(m_player, &FFmpegPlayer::fileLoaded,
            this, &PlayerEngine::fileLoaded);
    connect(m_player, &FFmpegPlayer::playbackSpeedChanged,
            this, &PlayerEngine::playbackSpeedChanged);
    connect(m_player, &FFmpegPlayer::volumeChanged,
            this, &PlayerEngine::volumeChanged);
    connect(m_player, &FFmpegPlayer::muteChanged,
            this, &PlayerEngine::muteChanged);
    connect(m_player, &FFmpegPlayer::videoFrameReady,
            this, &PlayerEngine::onVideoFrameReady);

    m_player->setVolume(m_volume);
    m_player->setPlaybackSpeed(m_playbackSpeed);

    qDebug() << "PlayerEngine initialized with FFmpeg backend";
}

PlayerEngine::~PlayerEngine()
{
    if (m_player->state() != PlaybackState::Stopped) {
        m_player->stop();
    }
}

bool PlayerEngine::loadFile(const QString& filePath)
{
    QFileInfo fileInfo(filePath);
    QString absolutePath = fileInfo.absoluteFilePath();

    qDebug() << "Loading file:" << absolutePath;

    if (!fileInfo.exists()) {
        emit errorOccurred(QString("File not found: %1").arg(filePath));
        return false;
    }

    m_filePath = filePath;
    bool result = m_player->loadFile(absolutePath);

    if (result) {
        emit fileLoaded(filePath);
    }

    return result;
}

void PlayerEngine::play()
{
    qDebug() << "PlayerEngine::play() called";
    m_player->play();
}

void PlayerEngine::pause()
{
    qDebug() << "PlayerEngine::pause() called";
    m_player->pause();
}

void PlayerEngine::stop()
{
    qDebug() << "PlayerEngine::stop() called";
    m_player->stop();
}

void PlayerEngine::seek(qint64 position)
{
    if (position < 0) position = 0;
    qint64 dur = duration();
    if (dur > 0 && position > dur) position = dur;

    qDebug() << "PlayerEngine::seek() to" << position << "ms";
    m_player->seek(position);
}

void PlayerEngine::setPlaybackSpeed(double speed)
{
    if (speed < 0.25) speed = 0.25;
    if (speed > 4.0) speed = 4.0;

    if (!qFuzzyCompare(m_playbackSpeed, speed)) {
        m_playbackSpeed = speed;
        m_player->setPlaybackSpeed(speed);
        emit playbackSpeedChanged(speed);
    }
}

void PlayerEngine::setVolume(int volume)
{
    if (volume < 0) volume = 0;
    if (volume > 100) volume = 100;

    if (m_volume != volume) {
        m_volume = volume;
        m_player->setVolume(volume);
        emit volumeChanged(volume);
    }
}

void PlayerEngine::setMuted(bool muted)
{
    if (m_muted != muted) {
        m_muted = muted;
        m_player->setMuted(muted);
        emit muteChanged(muted);
    }
}

PlaybackState PlayerEngine::state() const
{
    return m_state;
}

qint64 PlayerEngine::position() const
{
    return m_player->position();
}

qint64 PlayerEngine::duration() const
{
    return m_player->duration();
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

void PlayerEngine::onPositionChanged(qint64 position)
{
    emit positionChanged(position);
}

void PlayerEngine::onDurationChanged(qint64 duration)
{
    emit durationChanged(duration);
}

void PlayerEngine::onErrorOccurred(const QString& error)
{
    emit errorOccurred(error);
}

void PlayerEngine::onPlaybackStateChanged(PlaybackState state)
{
    if (m_state != state) {
        m_state = state;
        emit stateChanged(state);
    }
}

void PlayerEngine::onVideoFrameReady(const QImage& frame, qint64 pts)
{
    Q_UNUSED(pts);
    emit videoFrameReady(frame);
}

} // namespace VideoPlay
