#include "ui/controls.h"

#include <QHBoxLayout>
#include <QToolTip>
#include <QStyle>
#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>

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
    , m_fadeAnimation(new QPropertyAnimation(this, "opacity", this))
    , m_state(PlaybackState::Stopped)
    , m_duration(0)
    , m_position(0)
    , m_sliderPressed(false)
    , m_isFloating(false)
    , m_opacity(1.0)
{
    setupUi();
    applyStyling();

    m_hideTimer->setSingleShot(true);
    m_hideTimer->setInterval(3000); // 3秒后隐藏

    m_fadeAnimation->setDuration(200);

    connect(m_hideTimer, &QTimer::timeout, this, &Controls::fadeOut);
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
    // 使用 Qlementine 友好的边距
    layout->setContentsMargins(12, 6, 12, 6);
    layout->setSpacing(6);

    // 播放按钮 - 更大的点击区域
    m_playBtn->setFixedSize(40, 40);
    m_playBtn->setFlat(true);
    m_playBtn->setToolTip(tr("Play/Pause"));
    m_playBtn->setCursor(Qt::PointingHandCursor);

    // 停止按钮
    m_stopBtn->setFixedSize(36, 36);
    m_stopBtn->setFlat(true);
    m_stopBtn->setText(QString::fromUtf8("\u25A0"));
    m_stopBtn->setToolTip(tr("Stop"));
    m_stopBtn->setCursor(Qt::PointingHandCursor);

    // 进度滑块 - 更大更易拖动
    m_positionSlider->setRange(0, 0);
    m_positionSlider->setSingleStep(1000);
    m_positionSlider->setPageStep(10000);
    m_positionSlider->setMinimumHeight(28);
    m_positionSlider->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_positionSlider->setCursor(Qt::PointingHandCursor);

    // 时间标签 - 使用等宽字体
    m_timeLabel->setFixedWidth(120);
    m_timeLabel->setAlignment(Qt::AlignCenter);
    QFont timeFont = m_timeLabel->font();
    timeFont.setFamily("Consolas");
    m_timeLabel->setFont(timeFont);

    // 静音按钮
    m_muteBtn->setFixedSize(36, 36);
    m_muteBtn->setFlat(true);
    m_muteBtn->setToolTip(tr("Mute"));
    m_muteBtn->setCursor(Qt::PointingHandCursor);

    // 音量滑块
    m_volumeSlider->setRange(0, 100);
    m_volumeSlider->setValue(75);
    m_volumeSlider->setFixedWidth(100);
    m_volumeSlider->setToolTip(tr("Volume"));
    m_volumeSlider->setCursor(Qt::PointingHandCursor);

    // 倍速滑块和标签
    m_speedSlider->setRange(25, 400);
    m_speedSlider->setValue(100);
    m_speedSlider->setFixedWidth(80);
    m_speedSlider->setToolTip(tr("Speed"));
    m_speedSlider->setCursor(Qt::PointingHandCursor);
    
    m_speedLabel->setFixedWidth(50);
    m_speedLabel->setAlignment(Qt::AlignCenter);

    // 全屏按钮
    m_fullscreenBtn->setFixedSize(36, 36);
    m_fullscreenBtn->setFlat(true);
    m_fullscreenBtn->setText(QString::fromUtf8("\u26F6"));
    m_fullscreenBtn->setToolTip(tr("Fullscreen (F11)"));
    m_fullscreenBtn->setCursor(Qt::PointingHandCursor);

    layout->addWidget(m_playBtn);
    layout->addWidget(m_stopBtn);
    layout->addWidget(m_positionSlider, 1);
    layout->addWidget(m_timeLabel);
    layout->addWidget(m_muteBtn);
    layout->addWidget(m_volumeSlider);
    layout->addWidget(m_speedSlider);
    layout->addWidget(m_speedLabel);
    layout->addWidget(m_fullscreenBtn);
}

void Controls::applyStyling()
{
    // 不设置全局样式表，让 Qlementine 接管样式
    // 只设置特定于播放控制器的最小样式调整
    
    // 按钮使用 Unicode 符号，字体大小稍大
    QString buttonStyle = "QPushButton { font-size: 14px; }";
    m_playBtn->setStyleSheet(buttonStyle);
    m_stopBtn->setStyleSheet(buttonStyle);
    m_muteBtn->setStyleSheet(buttonStyle);
    m_fullscreenBtn->setStyleSheet(buttonStyle);
    
    // 时间标签使用等宽字体
    QFont timeFont = m_timeLabel->font();
    timeFont.setFamily("Consolas");
    m_timeLabel->setFont(timeFont);
}

void Controls::paintEvent(QPaintEvent* event)
{
    // 悬浮模式：绘制半透明背景
    if (m_isFloating) {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        
        // 先填充整个背景（确保不透明）
        painter.fillRect(rect(), Qt::transparent);
        
        // 绘制圆角半透明背景
        QRect bgRect = this->rect().adjusted(4, 4, -4, -4);
        QPainterPath path;
        path.addRoundedRect(bgRect, 8, 8);
        
        // 使用深色半透明背景
        QColor bgColor(40, 40, 40, 230);
        painter.fillPath(path, bgColor);
        
        // 绘制边框
        painter.setPen(QPen(QColor(80, 80, 80, 200), 1));
        painter.drawPath(path);
        
        // 顶部高光装饰线
        QLinearGradient highlight(bgRect.topLeft(), bgRect.topRight());
        highlight.setColorAt(0, QColor(100, 100, 100, 0));
        highlight.setColorAt(0.5, QColor(150, 150, 150, 100));
        highlight.setColorAt(1, QColor(100, 100, 100, 0));
        painter.setPen(QPen(highlight, 2));
        painter.drawLine(bgRect.left() + 10, bgRect.top() + 1, 
                         bgRect.right() - 10, bgRect.top() + 1);
    }
    
    // 调用基类绘制子控件
    QWidget::paintEvent(event);
}

void Controls::setOpacity(qreal opacity)
{
    m_opacity = opacity;
    if (m_isFloating) {
        update();
        setVisible(opacity > 0.01);
    }
}

void Controls::fadeIn()
{
    if (!m_isFloating) {
        show();
        return;
    }
    
    show(); // 确保显示
    m_fadeAnimation->stop();
    m_fadeAnimation->setStartValue(m_opacity);
    m_fadeAnimation->setEndValue(1.0);
    m_fadeAnimation->start();
    
    m_hideTimer->stop();
    m_hideTimer->start();
}

void Controls::fadeOut()
{
    if (!m_isFloating) {
        return;
    }
    
    m_fadeAnimation->stop();
    m_fadeAnimation->setStartValue(m_opacity);
    m_fadeAnimation->setEndValue(0.0);
    m_fadeAnimation->start();
}

void Controls::setFloatingMode(bool floating)
{
    m_isFloating = floating;
    
    if (floating) {
        // 悬浮模式：需要填充背景以实现半透明效果
        setAttribute(Qt::WA_TransparentForMouseEvents, false);
        setAutoFillBackground(true);
        
        // 覆盖 Qlementine 的样式设置
        setAttribute(Qt::WA_TranslucentBackground, false);
        setAttribute(Qt::WA_NoSystemBackground, false);
        setAttribute(Qt::WA_OpaquePaintEvent, false);
        
        setOpacity(1.0);
    } else {
        // 嵌入模式：恢复 Qlementine 默认样式
        setAutoFillBackground(false);
        setAttribute(Qt::WA_TranslucentBackground, false);
        setOpacity(1.0);
    }
    
    update();
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
    m_position = position;
    if (!m_sliderPressed && m_duration > 0) {
        m_positionSlider->setValue(static_cast<int>(position));
        updateTimeLabel();
    }
}

void Controls::setDuration(qint64 duration)
{
    m_duration = duration;
    m_positionSlider->setRange(0, static_cast<int>(duration));
    updateTimeLabel();
}

void Controls::updateTimeLabel()
{
    m_timeLabel->setText(QString("%1 / %2")
        .arg(formatTime(m_position))
        .arg(formatTime(m_duration)));
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
    fadeIn();
    QWidget::enterEvent(event);
}

void Controls::leaveEvent(QEvent* event)
{
    if (m_isFloating) {
        m_hideTimer->start();
    }
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
        m_position = value;
        updateTimeLabel();
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
    fadeOut();
}

} // namespace VideoPlay
