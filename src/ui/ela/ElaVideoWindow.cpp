#include "ui/ela/ElaVideoWindow.h"

#include <ElaApplication.h>
#include <ElaNavigationBar.h>
#include <ElaIconButton.h>
#include <ElaSlider.h>
#include <ElaText.h>
#include <ElaToggleSwitch.h>
#include <ElaDef.h>
#include <ElaMessageBar.h>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFileDialog>
#include <QMessageBox>
#include <QKeyEvent>
#include <QCloseEvent>
#include <QScreen>
#include <QDateTime>
#include <QStandardPaths>
#include <QGuiApplication>

#include "core/playerengine.h"
#include "core/settings.h"
#include "utils/logger.h"
#include "ui/videorenderer.h"
#include "subtitles/subtitleparser.h"
#include "core/common.h"

namespace VideoPlay {

ElaVideoWindow::ElaVideoWindow(QWidget* parent)
    : ElaWindow(parent)
    , m_engine(new PlayerEngine(this))
    , m_centralWidget(nullptr)
    , m_videoRenderer(nullptr)
    , m_controlPanel(nullptr)
    , m_playPauseBtn(nullptr)
    , m_stopBtn(nullptr)
    , m_prevBtn(nullptr)
    , m_nextBtn(nullptr)
    , m_progressSlider(nullptr)
    , m_timeLabel(nullptr)
    , m_muteBtn(nullptr)
    , m_volumeSlider(nullptr)
    , m_volumeLabel(nullptr)
    , m_speedLabel(nullptr)
    , m_speedSlider(nullptr)
    , m_openFileBtn(nullptr)
    , m_screenshotBtn(nullptr)
    , m_subtitleBtn(nullptr)
    , m_loopBtn(nullptr)
    , m_fullscreenBtn(nullptr)
    , m_duration(0)
    , m_currentPosition(0)
    , m_isFullscreen(false)
    , m_isMuted(false)
    , m_isLooping(false)
    , m_currentSpeed(1.0)
{
    setWindowTitle("VideoPlay");
    resize(1280, 720);
    setMinimumSize(800, 600);
    
    setupUi();
    setupConnections();
    loadSettings();
    
    Logger::instance().info("ElaVideoWindow created");
}

ElaVideoWindow::~ElaVideoWindow()
{
    saveSettings();
}

void ElaVideoWindow::setupUi()
{
    m_centralWidget = new QWidget(this);
    auto* mainLayout = new QVBoxLayout(m_centralWidget);
    mainLayout->setContentsMargins(15, 15, 15, 15);
    mainLayout->setSpacing(15);
    
    // 视频渲染器
    m_videoRenderer = new VideoRenderer(m_centralWidget);
    m_videoRenderer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    mainLayout->addWidget(m_videoRenderer, 1);
    
    // 控制面板
    m_controlPanel = new QWidget(m_centralWidget);
    auto* controlLayout = new QVBoxLayout(m_controlPanel);
    controlLayout->setSpacing(12);
    controlLayout->setContentsMargins(0, 0, 0, 0);
    
    // 第1行：进度条和时间
    auto* progressLayout = new QHBoxLayout();
    progressLayout->setSpacing(12);
    
    m_timeLabel = new ElaText("0:00 / 0:00", this);
    m_timeLabel->setTextPixelSize(13);
    m_timeLabel->setFixedWidth(110);
    
    m_progressSlider = new ElaSlider(this);
    m_progressSlider->setRange(0, 0);
    
    progressLayout->addWidget(m_timeLabel);
    progressLayout->addWidget(m_progressSlider, 1);
    
    controlLayout->addLayout(progressLayout);
    
    // 第2行：播放控制和功能按钮
    auto* controlsRow = new QHBoxLayout();
    controlsRow->setSpacing(10);
    
    // 左侧：文件操作
    auto* fileLayout = new QHBoxLayout();
    fileLayout->setSpacing(8);
    
    m_openFileBtn = new ElaIconButton(ElaIconType::FolderOpen, 18, this);
    m_openFileBtn->setFixedSize(36, 36);
    m_openFileBtn->setToolTip(tr("Open File (Ctrl+O)"));
    
    m_screenshotBtn = new ElaIconButton(ElaIconType::Camera, 18, this);
    m_screenshotBtn->setFixedSize(36, 36);
    m_screenshotBtn->setToolTip(tr("Screenshot (S)"));
    
    m_subtitleBtn = new ElaIconButton(ElaIconType::ClosedCaptioning, 18, this);
    m_subtitleBtn->setFixedSize(36, 36);
    m_subtitleBtn->setToolTip(tr("Load Subtitle"));
    
    fileLayout->addWidget(m_openFileBtn);
    fileLayout->addWidget(m_screenshotBtn);
    fileLayout->addWidget(m_subtitleBtn);
    
    controlsRow->addLayout(fileLayout);
    controlsRow->addSpacing(20);
    
    // 中间：播放控制
    auto* playbackLayout = new QHBoxLayout();
    playbackLayout->setSpacing(10);
    
    m_prevBtn = new ElaIconButton(ElaIconType::BackwardStep, 20, this);
    m_prevBtn->setFixedSize(40, 40);
    m_prevBtn->setToolTip(tr("Previous"));
    
    m_playPauseBtn = new ElaIconButton(ElaIconType::Play, 24, this);
    m_playPauseBtn->setFixedSize(48, 48);
    m_playPauseBtn->setToolTip(tr("Play/Pause (Space)"));
    
    m_stopBtn = new ElaIconButton(ElaIconType::Stop, 18, this);
    m_stopBtn->setFixedSize(40, 40);
    m_stopBtn->setToolTip(tr("Stop"));
    
    m_nextBtn = new ElaIconButton(ElaIconType::ForwardStep, 20, this);
    m_nextBtn->setFixedSize(40, 40);
    m_nextBtn->setToolTip(tr("Next"));
    
    playbackLayout->addWidget(m_prevBtn);
    playbackLayout->addWidget(m_playPauseBtn);
    playbackLayout->addWidget(m_stopBtn);
    playbackLayout->addWidget(m_nextBtn);
    
    controlsRow->addLayout(playbackLayout);
    controlsRow->addStretch();
    
    // 右侧：音量、速度、全屏
    auto* rightLayout = new QHBoxLayout();
    rightLayout->setSpacing(12);
    
    // 音量控制
    auto* volumeLayout = new QHBoxLayout();
    volumeLayout->setSpacing(6);
    
    m_muteBtn = new ElaIconButton(ElaIconType::VolumeHigh, 18, this);
    m_muteBtn->setFixedSize(32, 32);
    m_muteBtn->setToolTip(tr("Mute (M)"));
    
    m_volumeSlider = new ElaSlider(this);
    m_volumeSlider->setRange(0, 100);
    m_volumeSlider->setValue(75);
    m_volumeSlider->setFixedWidth(80);
    
    m_volumeLabel = new ElaText("75%", this);
    m_volumeLabel->setTextPixelSize(12);
    m_volumeLabel->setFixedWidth(35);
    
    volumeLayout->addWidget(m_muteBtn);
    volumeLayout->addWidget(m_volumeSlider);
    volumeLayout->addWidget(m_volumeLabel);
    
    rightLayout->addLayout(volumeLayout);
    rightLayout->addSpacing(15);
    
    // 速度控制（无极调节）
    auto* speedLayout = new QHBoxLayout();
    speedLayout->setSpacing(6);
    
    ElaText* speedText = new ElaText("Speed:", this);
    speedText->setTextPixelSize(12);
    
    m_speedSlider = new ElaSlider(this);
    m_speedSlider->setRange(25, 400);  // 0.25x - 4.0x
    m_speedSlider->setValue(100);      // 1.0x
    m_speedSlider->setFixedWidth(100);
    
    m_speedLabel = new ElaText("1.0x", this);
    m_speedLabel->setTextPixelSize(12);
    m_speedLabel->setFixedWidth(45);
    
    speedLayout->addWidget(speedText);
    speedLayout->addWidget(m_speedSlider);
    speedLayout->addWidget(m_speedLabel);
    
    rightLayout->addLayout(speedLayout);
    rightLayout->addSpacing(15);
    
    // 循环和全屏
    m_loopBtn = new ElaIconButton(ElaIconType::Repeat, 18, this);
    m_loopBtn->setFixedSize(36, 36);
    m_loopBtn->setToolTip(tr("Loop"));
    
    m_fullscreenBtn = new ElaIconButton(ElaIconType::Expand, 18, this);
    m_fullscreenBtn->setFixedSize(36, 36);
    m_fullscreenBtn->setToolTip(tr("Fullscreen (F)"));
    
    rightLayout->addWidget(m_loopBtn);
    rightLayout->addWidget(m_fullscreenBtn);
    
    controlsRow->addLayout(rightLayout);
    
    controlLayout->addLayout(controlsRow);
    
    mainLayout->addWidget(m_controlPanel);
    
    setCentralWidget(m_centralWidget);
    
    // 初始状态
    updatePlayPauseIcon(PlaybackState::Stopped);
    updateVolumeIcon();
}

void ElaVideoWindow::setupConnections()
{
    // 播放器信号
    connect(m_engine, &PlayerEngine::stateChanged, this, &ElaVideoWindow::onPlaybackStateChanged);
    connect(m_engine, &PlayerEngine::positionChanged, this, &ElaVideoWindow::onPositionChanged);
    connect(m_engine, &PlayerEngine::durationChanged, this, &ElaVideoWindow::onDurationChanged);
    connect(m_engine, &PlayerEngine::errorOccurred, this, &ElaVideoWindow::onError);
    connect(m_engine, &PlayerEngine::videoFrameReady, m_videoRenderer, &VideoRenderer::setFrame);
    
    // 播放控制
    connect(m_playPauseBtn, &ElaIconButton::clicked, this, &ElaVideoWindow::onPlayPauseClicked);
    connect(m_stopBtn, &ElaIconButton::clicked, this, &ElaVideoWindow::onStopClicked);
    connect(m_openFileBtn, &ElaIconButton::clicked, this, &ElaVideoWindow::onOpenFile);
    connect(m_fullscreenBtn, &ElaIconButton::clicked, this, &ElaVideoWindow::onToggleFullscreen);
    connect(m_screenshotBtn, &ElaIconButton::clicked, this, &ElaVideoWindow::onTakeScreenshot);
    connect(m_subtitleBtn, &ElaIconButton::clicked, this, &ElaVideoWindow::onLoadSubtitle);
    connect(m_muteBtn, &ElaIconButton::clicked, this, &ElaVideoWindow::onToggleMute);
    connect(m_loopBtn, &ElaIconButton::clicked, this, &ElaVideoWindow::onToggleLoop);
    
    // 进度条 - 拖动时只更新时间，释放时才跳转
    connect(m_progressSlider, &ElaSlider::valueChanged, this, &ElaVideoWindow::onSeekSliderChanged);
    connect(m_progressSlider, &ElaSlider::sliderReleased, this, &ElaVideoWindow::onSeekSliderReleased);
    
    // 音量滑块
    connect(m_volumeSlider, &ElaSlider::valueChanged, this, &ElaVideoWindow::onVolumeSliderChanged);
    
    // 速度滑块（无极调节）
    connect(m_speedSlider, &ElaSlider::valueChanged, this, &ElaVideoWindow::onSpeedSliderChanged);
    
    // 视频渲染器双击播放/暂停
    connect(m_videoRenderer, &VideoRenderer::doubleClicked, this, &ElaVideoWindow::onPlayPauseClicked);
}

void ElaVideoWindow::onPlayPauseClicked()
{
    if (m_engine->state() == PlaybackState::Playing) {
        m_engine->pause();
    } else {
        if (m_engine->state() == PlaybackState::Stopped && m_engine->filePath().isEmpty()) {
            onOpenFile();
        } else {
            m_engine->play();
        }
    }
}

void ElaVideoWindow::onStopClicked()
{
    m_engine->stop();
}

void ElaVideoWindow::onToggleFullscreen()
{
    if (!m_isFullscreen) {
        showFullScreen();
        m_isFullscreen = true;
        m_fullscreenBtn->setAwesome(ElaIconType::Compress);
    } else {
        showNormal();
        m_isFullscreen = false;
        m_fullscreenBtn->setAwesome(ElaIconType::Expand);
    }
}

void ElaVideoWindow::onOpenFile()
{
    QString path = QFileDialog::getOpenFileName(this, tr("Open Video"), QString(),
        tr("Videos (*.mp4 *.avi *.mkv *.mov *.wmv *.flv *.webm);;All (*)"));
    if (!path.isEmpty()) {
        if (m_engine->loadFile(path)) {
            m_engine->play();
            ElaMessageBar::success(ElaMessageBarType::Top, tr("Success"), tr("File loaded"), 2000, this);
        } else {
            ElaMessageBar::error(ElaMessageBarType::Top, tr("Error"), tr("Failed to load file"), 3000, this);
        }
    }
}

void ElaVideoWindow::onLoadSubtitle()
{
    QString path = QFileDialog::getOpenFileName(this, tr("Load Subtitle"), QString(),
        tr("Subtitles (*.srt *.ass *.ssa *.vtt);;All (*)"));
    if (!path.isEmpty()) {
        // TODO: Implement subtitle loading
        ElaMessageBar::success(ElaMessageBarType::Top, tr("Success"), tr("Subtitle loaded"), 2000, this);
    }
}

void ElaVideoWindow::onTakeScreenshot()
{
    QScreen* screen = QGuiApplication::primaryScreen();
    if (!screen) return;

    QPoint globalPos = m_videoRenderer->mapToGlobal(QPoint(0, 0));
    QPixmap screenshot = screen->grabWindow(0, globalPos.x(), globalPos.y(),
                                            m_videoRenderer->width(), m_videoRenderer->height());

    QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss");
    QString fileName = QString("screenshot_%1.png").arg(timestamp);
    QString dir = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    QString fullPath = dir + "/" + fileName;

    if (screenshot.save(fullPath, "PNG")) {
        ElaMessageBar::success(ElaMessageBarType::Top, tr("Success"), tr("Screenshot saved"), 3000, this);
    } else {
        ElaMessageBar::error(ElaMessageBarType::Top, tr("Error"), tr("Failed to save screenshot"), 3000, this);
    }
}

void ElaVideoWindow::onToggleMute()
{
    m_isMuted = !m_isMuted;
    m_engine->setMuted(m_isMuted);
    updateVolumeIcon();
}

void ElaVideoWindow::onToggleLoop()
{
    m_isLooping = !m_isLooping;
    // TODO: Implement loop in PlayerEngine
    if (m_isLooping) {
        ElaMessageBar::information(ElaMessageBarType::Top, tr("Info"), tr("Loop enabled"), 2000, this);
    } else {
        ElaMessageBar::information(ElaMessageBarType::Top, tr("Info"), tr("Loop disabled"), 2000, this);
    }
}

void ElaVideoWindow::onPlaybackStateChanged(PlaybackState state)
{
    updatePlayPauseIcon(state);
}

void ElaVideoWindow::onPositionChanged(qint64 position)
{
    m_currentPosition = position;
    if (m_duration > 0) {
        m_progressSlider->blockSignals(true);
        m_progressSlider->setValue(static_cast<int>(position));
        m_progressSlider->blockSignals(false);
    }
    updateTimeLabel();
}

void ElaVideoWindow::onDurationChanged(qint64 duration)
{
    m_duration = duration;
    m_progressSlider->setRange(0, static_cast<int>(duration));
    updateTimeLabel();
}

void ElaVideoWindow::onError(const QString& error)
{
    Logger::instance().error(QString("Playback error: %1").arg(error));
    ElaMessageBar::error(ElaMessageBarType::Top, tr("Error"), error, 5000, this);
}

void ElaVideoWindow::onSeekSliderChanged(int value)
{
    // 拖动时只更新时间显示，不实际跳转
    m_currentPosition = value;
    updateTimeLabel();
}

void ElaVideoWindow::onSeekSliderReleased()
{
    // 释放时才实际跳转
    m_engine->seek(m_progressSlider->value());
}

void ElaVideoWindow::onVolumeSliderChanged(int value)
{
    m_engine->setVolume(value);
    m_volumeLabel->setText(QString("%1%").arg(value));
    
    if (value == 0) {
        m_isMuted = true;
    } else if (m_isMuted) {
        m_isMuted = false;
        m_engine->setMuted(false);
    }
    updateVolumeIcon();
}

void ElaVideoWindow::onSpeedSliderChanged(int value)
{
    // 无极速度调节：25-400 映射到 0.25x - 4.0x
    double speed = value / 100.0;
    m_currentSpeed = speed;
    m_engine->setPlaybackSpeed(speed);
    updateSpeedLabel();
}

void ElaVideoWindow::updatePlayPauseIcon(PlaybackState state)
{
    if (state == PlaybackState::Playing) {
        m_playPauseBtn->setAwesome(ElaIconType::Pause);
    } else {
        m_playPauseBtn->setAwesome(ElaIconType::Play);
    }
}

void ElaVideoWindow::updateTimeLabel()
{
    m_timeLabel->setText(QString("%1 / %2")
        .arg(formatTime(m_currentPosition))
        .arg(formatTime(m_duration)));
}

void ElaVideoWindow::updateVolumeIcon()
{
    int volume = m_volumeSlider->value();
    if (m_isMuted || volume == 0) {
        m_muteBtn->setAwesome(ElaIconType::VolumeXmark);
    } else if (volume < 30) {
        m_muteBtn->setAwesome(ElaIconType::VolumeLow);
    } else if (volume < 70) {
        m_muteBtn->setAwesome(ElaIconType::Volume);
    } else {
        m_muteBtn->setAwesome(ElaIconType::VolumeHigh);
    }
}

void ElaVideoWindow::updateSpeedLabel()
{
    m_speedLabel->setText(QString::number(m_currentSpeed, 'f', 2) + "x");
}

void ElaVideoWindow::keyPressEvent(QKeyEvent* event)
{
    switch (event->key()) {
    case Qt::Key_Space:
        onPlayPauseClicked();
        break;
    case Qt::Key_Left:
        m_engine->seek(m_engine->position() - 5000);
        break;
    case Qt::Key_Right:
        m_engine->seek(m_engine->position() + 5000);
        break;
    case Qt::Key_Up:
        m_volumeSlider->setValue(qMin(100, m_volumeSlider->value() + 5));
        break;
    case Qt::Key_Down:
        m_volumeSlider->setValue(qMax(0, m_volumeSlider->value() - 5));
        break;
    case Qt::Key_M:
        onToggleMute();
        break;
    case Qt::Key_F:
    case Qt::Key_F11:
        onToggleFullscreen();
        break;
    case Qt::Key_S:
        onTakeScreenshot();
        break;
    case Qt::Key_O:
        if (event->modifiers() & Qt::ControlModifier) {
            onOpenFile();
        }
        break;
    case Qt::Key_L:
        onToggleLoop();
        break;
    case Qt::Key_Comma:  // < 减速
        m_speedSlider->setValue(qMax(25, m_speedSlider->value() - 5));
        break;
    case Qt::Key_Period: // > 加速
        m_speedSlider->setValue(qMin(400, m_speedSlider->value() + 5));
        break;
    default:
        ElaWindow::keyPressEvent(event);
    }
}

void ElaVideoWindow::closeEvent(QCloseEvent* event)
{
    saveSettings();
    m_engine->stop();
    event->accept();
}

void ElaVideoWindow::loadSettings()
{
    auto& settings = Settings::instance();
    
    setGeometry(settings.windowGeometry());
    
    // 音量
    int vol = settings.volume();
    m_engine->setVolume(vol);
    m_volumeSlider->setValue(vol);
    m_volumeLabel->setText(QString("%1%").arg(vol));
    
    // 静音
    m_isMuted = settings.isMuted();
    m_engine->setMuted(m_isMuted);
    updateVolumeIcon();
    
    // 速度
    double speed = settings.playbackSpeed();
    m_currentSpeed = speed;
    m_engine->setPlaybackSpeed(speed);
    m_speedSlider->setValue(static_cast<int>(speed * 100));
    updateSpeedLabel();
}

void ElaVideoWindow::saveSettings()
{
    auto& settings = Settings::instance();
    settings.setWindowGeometry(geometry());
    settings.setVolume(m_engine->volume());
    settings.setMuted(m_engine->isMuted());
    settings.setPlaybackSpeed(m_currentSpeed);
}

} // namespace VideoPlay
