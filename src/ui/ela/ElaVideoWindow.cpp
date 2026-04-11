#include "ui/ela/ElaVideoWindow.h"

#include <ElaApplication.h>
#include <ElaNavigationBar.h>
#include <ElaIconButton.h>
#include <ElaSlider.h>
#include <ElaText.h>
#include <ElaComboBox.h>
#include <ElaDef.h>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QMessageBox>
#include <QKeyEvent>
#include <QCloseEvent>

#include "core/playerengine.h"
#include "core/settings.h"
#include "utils/logger.h"
#include "ui/videorenderer.h"
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
    , m_progressSlider(nullptr)
    , m_timeLabel(nullptr)
    , m_speedCombo(nullptr)
    , m_fullscreenBtn(nullptr)
    , m_duration(0)
    , m_currentPosition(0)
    , m_isFullscreen(false)
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
    mainLayout->setContentsMargins(10, 10, 10, 10);
    mainLayout->setSpacing(10);
    
    // Video renderer
    m_videoRenderer = new VideoRenderer(m_centralWidget);
    m_videoRenderer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    mainLayout->addWidget(m_videoRenderer, 1);
    
    // Control panel
    m_controlPanel = new QWidget(m_centralWidget);
    auto* controlLayout = new QVBoxLayout(m_controlPanel);
    controlLayout->setSpacing(10);
    
    // Progress row
    auto* progressLayout = new QHBoxLayout();
    progressLayout->setSpacing(10);
    
    m_playPauseBtn = new ElaIconButton(ElaIconType::Play, 24, this);
    m_playPauseBtn->setFixedSize(40, 40);
    
    m_stopBtn = new ElaIconButton(ElaIconType::Stop, 20, this);
    m_stopBtn->setFixedSize(36, 36);
    
    m_progressSlider = new ElaSlider(this);
    m_progressSlider->setRange(0, 0);
    
    m_timeLabel = new ElaText("0:00 / 0:00", this);
    
    progressLayout->addWidget(m_playPauseBtn);
    progressLayout->addWidget(m_stopBtn);
    progressLayout->addWidget(m_progressSlider, 1);
    progressLayout->addWidget(m_timeLabel);
    
    controlLayout->addLayout(progressLayout);
    
    // Buttons row
    auto* buttonsLayout = new QHBoxLayout();
    buttonsLayout->setSpacing(15);
    
    m_speedCombo = new ElaComboBox(this);
    m_speedCombo->addItems({"0.5x", "0.75x", "1.0x", "1.25x", "1.5x", "2.0x"});
    m_speedCombo->setCurrentIndex(2);
    m_speedCombo->setFixedWidth(80);
    
    m_fullscreenBtn = new ElaIconButton(ElaIconType::Expand, 20, this);
    m_fullscreenBtn->setFixedSize(32, 32);
    m_fullscreenBtn->setToolTip("Fullscreen (F)");
    
    buttonsLayout->addWidget(m_speedCombo);
    buttonsLayout->addStretch();
    buttonsLayout->addWidget(m_fullscreenBtn);
    
    controlLayout->addLayout(buttonsLayout);
    
    mainLayout->addWidget(m_controlPanel);
    
    setCentralWidget(m_centralWidget);
    
    // Set initial button states
    updatePlayPauseIcon(PlaybackState::Stopped);
}

void ElaVideoWindow::setupConnections()
{
    // Player signals
    connect(m_engine, &PlayerEngine::stateChanged, this, &ElaVideoWindow::onPlaybackStateChanged);
    connect(m_engine, &PlayerEngine::positionChanged, this, &ElaVideoWindow::onPositionChanged);
    connect(m_engine, &PlayerEngine::durationChanged, this, &ElaVideoWindow::onDurationChanged);
    connect(m_engine, &PlayerEngine::errorOccurred, this, &ElaVideoWindow::onError);
    connect(m_engine, &PlayerEngine::videoFrameReady, m_videoRenderer, &VideoRenderer::setFrame);
    
    // Control signals
    connect(m_playPauseBtn, &ElaIconButton::clicked, this, &ElaVideoWindow::onPlayPauseClicked);
    connect(m_stopBtn, &ElaIconButton::clicked, this, &ElaVideoWindow::onStopClicked);
    connect(m_fullscreenBtn, &ElaIconButton::clicked, this, &ElaVideoWindow::onToggleFullscreen);
    connect(m_progressSlider, &ElaSlider::sliderReleased, this, [this]() {
        m_engine->seek(m_progressSlider->value());
    });
    connect(m_speedCombo, QOverload<int>::of(&ElaComboBox::currentIndexChanged), 
            this, &ElaVideoWindow::onSpeedChanged);
    
    // Video renderer signals
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
    } else {
        showNormal();
        m_isFullscreen = false;
    }
}

void ElaVideoWindow::onOpenFile()
{
    QString path = QFileDialog::getOpenFileName(this, tr("Open Video"), QString(),
        tr("Videos (*.mp4 *.avi *.mkv *.mov *.wmv *.flv *.webm);;All (*)"));
    if (!path.isEmpty()) {
        if (m_engine->loadFile(path)) {
            m_engine->play();
        } else {
            QMessageBox::critical(this, tr("Error"), tr("Failed to load file"));
        }
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
        m_progressSlider->setValue(static_cast<int>(position));
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
    QMessageBox::critical(this, tr("Playback Error"), error);
}

void ElaVideoWindow::onSpeedChanged(int index)
{
    double speeds[] = {0.5, 0.75, 1.0, 1.25, 1.5, 2.0};
    m_engine->setPlaybackSpeed(speeds[index]);
}

void ElaVideoWindow::updatePlayPauseIcon(PlaybackState state)
{
    // Update button states based on playback state
    Q_UNUSED(state)
    // ElaIconButton doesn't have setIcon, use text or style
}

void ElaVideoWindow::updateTimeLabel()
{
    m_timeLabel->setText(QString("%1 / %2")
        .arg(formatTime(m_currentPosition))
        .arg(formatTime(m_duration)));
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
        m_engine->setVolume(qMin(100, m_engine->volume() + 5));
        break;
    case Qt::Key_Down:
        m_engine->setVolume(qMax(0, m_engine->volume() - 5));
        break;
    case Qt::Key_M:
        m_engine->setMuted(!m_engine->isMuted());
        break;
    case Qt::Key_F:
    case Qt::Key_F11:
        onToggleFullscreen();
        break;
    case Qt::Key_O:
        if (event->modifiers() & Qt::ControlModifier) {
            onOpenFile();
        }
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
    
    int vol = settings.volume();
    m_engine->setVolume(vol);
    
    m_engine->setMuted(settings.isMuted());
    
    double speed = settings.playbackSpeed();
    m_engine->setPlaybackSpeed(speed);
    
    // Map speed to combo index
    if (speed <= 0.5) m_speedCombo->setCurrentIndex(0);
    else if (speed <= 0.75) m_speedCombo->setCurrentIndex(1);
    else if (speed <= 1.0) m_speedCombo->setCurrentIndex(2);
    else if (speed <= 1.25) m_speedCombo->setCurrentIndex(3);
    else if (speed <= 1.5) m_speedCombo->setCurrentIndex(4);
    else m_speedCombo->setCurrentIndex(5);
}

void ElaVideoWindow::saveSettings()
{
    auto& settings = Settings::instance();
    settings.setWindowGeometry(geometry());
    settings.setVolume(m_engine->volume());
    settings.setMuted(m_engine->isMuted());
    settings.setPlaybackSpeed(m_engine->playbackSpeed());
}

} // namespace VideoPlay
