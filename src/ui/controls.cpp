#include "ui/controls.h"

#include <QHBoxLayout>
#include <QToolTip>
#include <QStyle>
#include <QFontMetrics>

namespace VideoPlay {

Controls::Controls(QWidget* parent)
    : QWidget(parent)
    , m_playBtn(new QPushButton(this))
    , m_stopBtn(new QPushButton(this))
    , m_positionSlider(new QSlider(Qt::Horizontal, this))
    , m_timeLabel(new QLabel("0:00 / 0:00", this))
    , m_muteBtn(new QPushButton(this))
    , m_volumeSlider(new QSlider(Qt::Horizontal, this))
    , m_speedSlider(new QSlider(Qt::Horizontal, this))
    , m_speedLabel(new QLabel("1.0x", this))
    , m_fullscreenBtn(new QPushButton(this))
    , m_hideTimer(new QTimer(this))
    , m_state(PlaybackState::Stopped)
    , m_duration(0)
    , m_sliderPressed(false)
{
    setupUi();
    applyStyling();

    m_hideTimer->setSingleShot(true);
    m_hideTimer->setInterval(5000);

    connect(m_hideTimer, &QTimer::timeout, this, &Controls::onHideTimeout);
    connect(m_playBtn, &QPushButton::clicked, this, &Controls::onPlayPauseClicked);
    connect(m_stopBtn, &QPushButton::clicked, this, &Controls::stopClicked);
    connect(m_fullscreenBtn, &QPushButton::clicked, this, &Controls::fullscreenClicked);
    connect(m_positionSlider, &QSlider::valueChanged, this, &Controls::onPositionSliderMoved);
    connect(m_positionSlider, &QSlider::sliderPressed, this, &Controls::onPositionSliderPressed);
    connect(m_positionSlider, &QSlider::sliderReleased, this, &Controls::onPositionSliderReleased);
    connect(m_volumeSlider, &QSlider::valueChanged, this, &Controls::onVolumeSliderMoved);
    connect(m_muteBtn, &QPushButton::clicked, this, &Controls::muteToggled);
    connect(m_speedSlider, &QSlider::valueChanged, this, &Controls::onSpeedSliderMoved);
}

void Controls::setupUi()
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(8, 4, 8, 4);
    layout->setSpacing(6);

    m_playBtn->setFixedSize(32, 32);
    m_playBtn->setFlat(true);
    m_playBtn->setToolTip("Play");

    m_stopBtn->setFixedSize(32, 32);
    m_stopBtn->setFlat(true);
    m_stopBtn->setText(QString::fromUtf8("\u25A0"));
    m_stopBtn->setToolTip("Stop");

    m_positionSlider->setRange(0, 0);
    m_positionSlider->setSingleStep(1000);
    m_positionSlider->setPageStep(10000);
    m_positionSlider->setMinimumHeight(24);
    m_positionSlider->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    m_timeLabel->setFixedWidth(110);
    m_timeLabel->setAlignment(Qt::AlignCenter);

    m_muteBtn->setFixedSize(32, 32);
    m_muteBtn->setFlat(true);
    m_muteBtn->setToolTip("Mute");

    m_volumeSlider->setRange(0, 100);
    m_volumeSlider->setValue(75);
    m_volumeSlider->setFixedWidth(90);
    m_volumeSlider->setToolTip("Volume");

    // Speed slider: 10 to 400 (0.1x to 4.0x)
    m_speedSlider->setRange(10, 400);
    m_speedSlider->setValue(100);
    m_speedSlider->setFixedWidth(80);
    m_speedSlider->setToolTip("Playback Speed");
    
    m_speedLabel->setFixedWidth(45);
    m_speedLabel->setAlignment(Qt::AlignCenter);

    m_fullscreenBtn->setFixedSize(32, 32);
    m_fullscreenBtn->setFlat(true);
    m_fullscreenBtn->setText(QString::fromUtf8("\u26F6"));
    m_fullscreenBtn->setToolTip("Fullscreen (F11)");

    layout->addWidget(m_playBtn);
    layout->addWidget(m_stopBtn);
    layout->addWidget(m_positionSlider);
    layout->addWidget(m_timeLabel);
    layout->addWidget(m_muteBtn);
    layout->addWidget(m_volumeSlider);
    layout->addWidget(m_speedSlider);
    layout->addWidget(m_speedLabel);
    layout->addWidget(m_fullscreenBtn);
}

void Controls::applyStyling()
{
    setStyleSheet(R"(
        QPushButton {
            border: none;
            border-radius: 4px;
            background: transparent;
        }
        QPushButton:hover {
            background: rgba(255, 255, 255, 0.1);
        }
        QSlider::groove:horizontal {
            height: 4px;
            background: #444;
            border-radius: 2px;
        }
        QSlider::handle:horizontal {
            width: 12px;
            margin: -4px 0;
            background: #fff;
            border-radius: 6px;
        }
        QSlider::handle:horizontal:hover {
            background: #ccc;
        }
    )");
}

void Controls::setPlaybackState(PlaybackState state)
{
    if (m_state != state) {
        m_state = state;
        updatePlayPauseIcon();
    }
}

void Controls::setPosition(qint64 position)
{
    if (!m_sliderPressed && m_duration > 0) {
        m_positionSlider->setValue(static_cast<int>(position));
        m_timeLabel->setText(QString("%1 / %2").arg(formatTime(position)).arg(formatTime(m_duration)));
    }
}

void Controls::setDuration(qint64 duration)
{
    m_duration = duration;
    m_positionSlider->setRange(0, static_cast<int>(duration));
}

void Controls::setVolume(int volume)
{
    m_volumeSlider->setValue(volume);
}

void Controls::setMuted(bool muted)
{
    QString icon = muted ? QString::fromUtf8("\xE2\x9D\x87") : QString::fromUtf8("\xE2\x99\x8A");
    m_muteBtn->setText(icon);
}

void Controls::setPlaybackSpeed(double speed)
{
    int value = static_cast<int>(speed * 100);
    m_speedSlider->setValue(value);
    m_speedLabel->setText(QString::number(speed, 'f', 1) + "x");
}

void Controls::updatePlayPauseIcon()
{
    if (m_state == PlaybackState::Playing) {
        m_playBtn->setText(QString::fromUtf8("\u23F8"));
    } else {
        m_playBtn->setText(QString::fromUtf8("\u25B6"));
    }
}

void Controls::enterEvent(QEnterEvent* event)
{
    m_hideTimer->stop();
    show();
    QWidget::enterEvent(event);
}

void Controls::leaveEvent(QEvent* event)
{
    QWidget::leaveEvent(event);
}

void Controls::onPlayPauseClicked()
{
    if (m_state == PlaybackState::Playing) {
        emit pauseClicked();
    } else {
        emit playClicked();
    }
}

void Controls::onPositionSliderMoved(int value)
{
    if (m_sliderPressed) {
        QToolTip::showText(
            QCursor::pos(),
            formatTime(static_cast<qint64>(value)),
            this);
    }
}

void Controls::onPositionSliderPressed()
{
    m_sliderPressed = true;
}

void Controls::onPositionSliderReleased()
{
    m_sliderPressed = false;
    emit seekRequested(m_positionSlider->value());
}

void Controls::onVolumeSliderMoved(int value)
{
    emit volumeChanged(value);
}

void Controls::onSpeedSliderMoved(int value)
{
    double speed = value / 100.0;
    m_speedLabel->setText(QString::number(speed, 'f', 1) + "x");
    emit speedChanged(speed);
}

void Controls::onHideTimeout()
{
    hide();
}

} // namespace VideoPlay
