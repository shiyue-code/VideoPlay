#include <QStackedLayout>
#include <QScreen>
#include <QPixmap>
#include <QDateTime>
#include <QStandardPaths>
#include <QGuiApplication>
#include <QPainter>
#include <QWindow>
#include "ui/mainwindow.h"

#include <QApplication>
#include <QMenuBar>
#include <QToolBar>
#include <QStatusBar>
#include <QDockWidget>
#include <QVBoxLayout>
#include <QFileDialog>
#include <QMessageBox>
#include <QLabel>
#include <QAction>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QFileInfo>
#include "core/playerengine.h"
#include "core/settings.h"
#include "utils/logger.h"
#include "ui/controls.h"
#include "core/common.h"
#include "ui/playlistwidget.h"
#include "ui/videowidget.h"
#include "subtitles/subtitleparser.h"
#include "subtitles/subtitleoverlay.h"

namespace VideoPlay {

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , m_engine(new PlayerEngine(this))
    , m_controls(nullptr)
    , m_playlistWidget(nullptr)
    , m_playlistDock(nullptr)
    , m_subtitleParser(new SubtitleParser(this))
    , m_subtitleOverlay(nullptr)
    , m_fullscreenAction(nullptr)
    , m_alwaysOnTopAction(nullptr)
    , m_loopModeAction(nullptr)
    , m_timeLabel(nullptr)
    , m_statusLabel(nullptr)
    , m_isFullscreen(false)
    , m_alwaysOnTop(false)
    , m_loopMode(0)
    , m_subtitleDelay(0)
{
    Logger::instance().info("MainWindow created");
    setAcceptDrops(true);
    setupUi();
    setupConnections();
    loadSettings();
    setMinimumSize(640, 480);
}

MainWindow::~MainWindow() = default;

bool MainWindow::openFile(const QString& filePath)
{
    qDebug() << "MainWindow::openFile called with:" << filePath;
    if (filePath.isEmpty() || !QFileInfo::exists(filePath))
        return false;

    Logger::instance().info(QString("Opening file: %1").arg(filePath));

    bool result = m_engine->loadFile(filePath);
    qDebug() << "loadFile result:" << result;
    if (result) {
        // 检查播放列表中是否已存在，避免重复添加
        for (int i = 0; i < m_playlistWidget->count(); ++i) {
            if (m_playlistWidget->item(i) == filePath) {
                m_playlistWidget->setCurrentIndex(i);
                return true;
            }
        }
        m_playlistWidget->addItem(filePath);
        Settings::instance().addRecentFile(filePath);
        updateWindowTitle();
        return true;
    }
    return false;
}

void MainWindow::setupUi()
{
    // Central widget
    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Container for video + subtitle using stacked layout
    m_videoContainer = new QWidget(central);
    m_videoContainer->setMinimumSize(320, 240);
    m_videoContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    auto* videoLayout = new QStackedLayout(m_videoContainer);
    videoLayout->setStackingMode(QStackedLayout::StackAll);
    videoLayout->setContentsMargins(0, 0, 0, 0);

    // Video widget (receives QImage frames from FFmpegPlayer)
    m_videoWidget = new VideoWidget(m_videoContainer);
    m_videoWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    videoLayout->addWidget(m_videoWidget);

    layout->addWidget(m_videoContainer, 1);

    m_controls = new Controls(central);
    layout->addWidget(m_controls);

    setCentralWidget(central);

    // Playlist dock
    m_playlistWidget = new PlaylistWidget(this);
    m_playlistDock = new QDockWidget(tr("Playlist"), this);
    m_playlistDock->setWidget(m_playlistWidget);
    addDockWidget(Qt::RightDockWidgetArea, m_playlistDock);

    // Menu bar
    QMenu* fileMenu = menuBar()->addMenu(tr("&File"));
    fileMenu->addAction(tr("&Open..."), this, &MainWindow::onOpenFile, QKeySequence::Open);
    fileMenu->addSeparator();
    fileMenu->addAction(tr("E&xit"), this, &QWidget::close, QKeySequence::Quit);

    QMenu* playbackMenu = menuBar()->addMenu(tr("&Playback"));
    playbackMenu->addAction(tr("&Play/Pause"), this, [this]() {
        if (m_engine->state() == PlaybackState::Playing)
            m_engine->pause();
        else
            m_engine->play();
    }, Qt::Key_Space);
    playbackMenu->addAction(tr("&Stop"), m_engine, &PlayerEngine::stop);
    playbackMenu->addSeparator();
    playbackMenu->addAction(tr("Forward +5s"), this, [this]() {
        m_engine->seek(m_engine->position() + 5000);
    }, Qt::Key_Right);
    playbackMenu->addAction(tr("Backward -5s"), this, [this]() {
        m_engine->seek(m_engine->position() - 5000);
    }, Qt::Key_Left);
    playbackMenu->addSeparator();
    m_loopModeAction = playbackMenu->addAction(tr("Loop: &Off"), this, &MainWindow::onToggleLoopMode, Qt::Key_L);
    m_loopModeAction->setCheckable(true);
    playbackMenu->addAction(tr("Take &Screenshot"), this, &MainWindow::onTakeScreenshot, Qt::Key_S);

    QMenu* subtitleMenu = menuBar()->addMenu(tr("&Subtitle"));
    subtitleMenu->addAction(tr("&Load Subtitle..."), this, &MainWindow::onLoadSubtitle);
    subtitleMenu->addSeparator();
    subtitleMenu->addAction(tr("Delay &+100ms"), this, &MainWindow::onSubtitleDelayPlus, Qt::Key_BracketRight);
    subtitleMenu->addAction(tr("Delay &-100ms"), this, &MainWindow::onSubtitleDelayMinus, Qt::Key_BracketLeft);

    QMenu* viewMenu = menuBar()->addMenu(tr("&View"));
    m_fullscreenAction = viewMenu->addAction(tr("&Fullscreen"), this, &MainWindow::onToggleFullscreen, Qt::Key_F11);
    m_alwaysOnTopAction = viewMenu->addAction(tr("Always on &Top"), this, &MainWindow::onToggleAlwaysOnTop, Qt::Key_T);
    m_alwaysOnTopAction->setCheckable(true);
    viewMenu->addAction(m_playlistDock->toggleViewAction());

    menuBar()->addMenu(tr("&Help"))->addAction(tr("&About"), this, &MainWindow::onAbout);

    // Toolbar
    QToolBar* toolbar = addToolBar(tr("Main"));
    toolbar->addAction(fileMenu->actions().first());
    toolbar->addSeparator();
    toolbar->addAction(playbackMenu->actions().at(0));
    toolbar->addAction(playbackMenu->actions().at(1));

    // Status bar
    m_statusLabel = new QLabel(tr("Ready"), this);
    m_timeLabel = new QLabel("00:00 / 00:00", this);
    statusBar()->addWidget(m_statusLabel, 1);
    statusBar()->addPermanentWidget(m_timeLabel);
}

void MainWindow::setupConnections()
{
    connect(m_engine, &PlayerEngine::stateChanged, this, &MainWindow::onStateChanged);
    connect(m_engine, &PlayerEngine::positionChanged, this, &MainWindow::onPositionChanged);
    connect(m_engine, &PlayerEngine::positionChanged, this, &MainWindow::updateSubtitles);
    connect(m_engine, &PlayerEngine::durationChanged, this, &MainWindow::onDurationChanged);
    connect(m_engine, &PlayerEngine::errorOccurred, this, &MainWindow::onError);
    connect(m_engine, &PlayerEngine::videoFrameReady, m_videoWidget, &VideoWidget::setFrame);

    connect(m_controls, &Controls::playClicked, m_engine, &PlayerEngine::play);
    connect(m_controls, &Controls::pauseClicked, m_engine, &PlayerEngine::pause);
    connect(m_controls, &Controls::stopClicked, m_engine, &PlayerEngine::stop);
    connect(m_controls, &Controls::seekRequested, m_engine, &PlayerEngine::seek);
    connect(m_controls, &Controls::volumeChanged, m_engine, [this](int vol) {
        m_engine->setVolume(vol);
    });
    connect(m_controls, &Controls::muteToggled, m_engine, [this]() {
        m_engine->setMuted(!m_engine->isMuted());
    });
    connect(m_controls, &Controls::speedChanged, m_engine, [this](double speed) {
        m_engine->setPlaybackSpeed(speed);
    });
    connect(m_controls, &Controls::fullscreenClicked, this, &MainWindow::onToggleFullscreen);

    connect(m_playlistWidget, &PlaylistWidget::itemDoubleClicked, this, &MainWindow::onPlaylistDoubleClicked);
}

void MainWindow::loadSettings()
{
    auto& settings = Settings::instance();
    
    // Window geometry
    QRect geo = settings.windowGeometry();
    setGeometry(geo);

    // Volume
    int vol = settings.volume();
    m_engine->setVolume(vol);
    m_controls->setVolume(vol);

    // Muted
    bool muted = settings.isMuted();
    m_engine->setMuted(muted);
    m_controls->setMuted(muted);

    // Playback speed
    double speed = settings.playbackSpeed();
    m_engine->setPlaybackSpeed(speed);
    m_controls->setPlaybackSpeed(speed);

    // Always on top
    m_alwaysOnTop = false;
    m_alwaysOnTopAction->setChecked(false);

    // Loop mode
    m_loopMode = 0;
    m_loopModeAction->setText(tr("Loop: &Off"));
    m_loopModeAction->setChecked(false);

    Logger::instance().info(QString("Settings loaded: volume=%1, speed=%2, muted=%3").arg(vol).arg(speed).arg(muted ? "true" : "false"));
}

void MainWindow::saveSettings()
{
    auto& settings = Settings::instance();
    settings.setWindowGeometry(geometry());
    settings.setVolume(m_engine->volume());
    settings.setMuted(m_engine->isMuted());
    settings.setPlaybackSpeed(m_engine->playbackSpeed());
}

void MainWindow::updateWindowTitle()
{
    QString fileName = m_engine->filePath().isEmpty() 
        ? "VideoPlay" 
        : QString("%1 - VideoPlay").arg(QFileInfo(m_engine->filePath()).fileName());
    setWindowTitle(fileName);
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasUrls())
        event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent* event)
{
    for (const QUrl& url : event->mimeData()->urls()) {
        if (url.isLocalFile()) {
            openFile(url.toLocalFile());
            break;
        }
    }
}

void MainWindow::keyPressEvent(QKeyEvent* event)
{
    switch (event->key()) {
    case Qt::Key_Space:
        if (m_engine->state() == PlaybackState::Playing) m_engine->pause();
        else m_engine->play();
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
    case Qt::Key_T:
        onToggleAlwaysOnTop();
        break;
    case Qt::Key_L:
        onToggleLoopMode();
        break;
    case Qt::Key_S:
        onTakeScreenshot();
        break;
    default:
        QMainWindow::keyPressEvent(event);
    }
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    Logger::instance().info("MainWindow closing, saving settings");
    saveSettings();
    m_engine->stop();
    event->accept();
}

void MainWindow::onOpenFile()
{
    QString path = QFileDialog::getOpenFileName(this, tr("Open Video"), QString(),
        tr("Videos (*.mp4 *.avi *.mkv *.mov *.wmv *.flv *.webm);;All (*)"));
    if (!path.isEmpty())
        openFile(path);
}

void MainWindow::onStateChanged(PlaybackState state)
{
    qDebug() << "onStateChanged:" << static_cast<int>(state);
    m_controls->setPlaybackState(state);

    if (state == PlaybackState::Playing) {
        m_statusLabel->setText(tr("Playing: %1").arg(QFileInfo(m_engine->filePath()).fileName()));
    } else if (state == PlaybackState::Paused) {
        m_statusLabel->setText(tr("Paused"));
    } else {
        m_statusLabel->setText(tr("Stopped"));
    }
}

void MainWindow::onPositionChanged(qint64 position)
{
    m_controls->setPosition(position);
    m_timeLabel->setText(QString("%1 / %2").arg(formatTime(position)).arg(formatTime(m_engine->duration())));
    updateSubtitles(position);
}

void MainWindow::onDurationChanged(qint64 duration)
{
    m_controls->setDuration(duration);
}

void MainWindow::onError(const QString& error)
{
    m_statusLabel->setText(tr("Error: %1").arg(error));
    Logger::instance().error(QString("Playback error: %1").arg(error));
}

void MainWindow::onPlaylistDoubleClicked(int index)
{
    QString path = m_playlistWidget->item(index);
    if (!path.isEmpty())
        openFile(path);
}

void MainWindow::onToggleFullscreen()
{
    m_isFullscreen = !m_isFullscreen;
    if (m_isFullscreen) {
        menuBar()->hide();
        statusBar()->hide();
        m_playlistDock->hide();
        showFullScreen();
    } else {
        menuBar()->show();
        statusBar()->show();
        m_playlistDock->show();
        showNormal();
    }
}

void MainWindow::onToggleAlwaysOnTop()
{
    m_alwaysOnTop = !m_alwaysOnTop;
    m_alwaysOnTopAction->setChecked(m_alwaysOnTop);
    setWindowFlag(Qt::WindowStaysOnTopHint, m_alwaysOnTop);
    show();
    Logger::instance().info(QString("Always on top: %1").arg(m_alwaysOnTop ? "ON" : "OFF"));
}

void MainWindow::onToggleLoopMode()
{
    m_loopMode = (m_loopMode + 1) % 3;
    switch (m_loopMode) {
    case 0:
        m_loopModeAction->setText(tr("Loop: &Off"));
        m_loopModeAction->setChecked(false);
        Logger::instance().info("Loop mode: OFF");
        break;
    case 1:
        m_loopModeAction->setText(tr("Loop: &Single"));
        m_loopModeAction->setChecked(true);
        Logger::instance().info("Loop mode: SINGLE");
        break;
    case 2:
        m_loopModeAction->setText(tr("Loop: &All"));
        m_loopModeAction->setChecked(true);
        Logger::instance().info("Loop mode: ALL");
        break;
    }
}

void MainWindow::onTakeScreenshot()
{
    // Use screen capture of the video widget area
    QScreen* screen = windowHandle()->screen();
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }
    if (!screen) return;

    // Get the video container's global position and size
    QPoint globalPos = m_videoContainer->mapToGlobal(QPoint(0, 0));
    QSize videoSize = m_videoContainer->size();
    QRect videoRect(globalPos, videoSize);

    // Capture the screen area where video is displayed
    QPixmap screenshot = screen->grabWindow(0, videoRect.x(), videoRect.y(), videoRect.width(), videoRect.height());

    if (screenshot.isNull() || screenshot.width() == 0) {
        m_statusLabel->setText(tr("Failed to capture screenshot"));
        Logger::instance().error("Screenshot capture failed");
        return;
    }

    QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss");
    QString fileName = QString("screenshot_%1.png").arg(timestamp);
    QString dir = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    QDir().mkpath(dir);
    QString fullPath = QString("%1/%2").arg(dir, fileName);

    if (screenshot.save(fullPath, "PNG")) {
        m_statusLabel->setText(tr("Screenshot saved: %1").arg(fullPath));
        Logger::instance().info(QString("Screenshot saved: %1").arg(fullPath));
    } else {
        m_statusLabel->setText(tr("Failed to save screenshot"));
        Logger::instance().error("Failed to save screenshot");
    }
}

void MainWindow::onAbout()
{
    QMessageBox::about(this, tr("About VideoPlay"),
        tr("<h3>VideoPlay 1.0</h3><p>A modern video player built with Qt6.</p>"));
}

void MainWindow::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (m_engine->state() == PlaybackState::Playing) {
        m_engine->pause();
    } else {
        m_engine->play();
    }
    event->accept();
}

void MainWindow::onLoadSubtitle()
{
    QString path = QFileDialog::getOpenFileName(this, tr("Load Subtitle"), QString(),
        tr("Subtitles (*.srt *.ass *.ssa *.vtt);;All (*)"));
    if (path.isEmpty())
        return;

    if (m_subtitleParser->loadFile(path)) {
        m_statusLabel->setText(tr("Subtitle loaded: %1").arg(QFileInfo(path).fileName()));
        Logger::instance().info(QString("Subtitle loaded: %1").arg(path));
        qDebug() << "Subtitle entries loaded:" << m_subtitleParser->entries().size();
        // Force update subtitle for current position after loading
        onPositionChanged(m_engine->position());
    } else {
        m_statusLabel->setText(tr("Failed to load subtitle"));
        Logger::instance().error(QString("Failed to load subtitle: %1").arg(path));
    }
}

void MainWindow::onSubtitleDelayPlus()
{
    m_subtitleDelay += 100;
    m_statusLabel->setText(tr("Subtitle delay: +%1ms").arg(m_subtitleDelay));
}

void MainWindow::onSubtitleDelayMinus()
{
    m_subtitleDelay -= 100;
    m_statusLabel->setText(tr("Subtitle delay: %1ms").arg(m_subtitleDelay));
}

void MainWindow::updateSubtitles(qint64 position)
{
    if (!m_subtitleParser->isLoaded()) {
        if (m_subtitleOverlay) {
            m_subtitleOverlay->setText(QString());
        }
        return;
    }

    qint64 adjustedPos = position + m_subtitleDelay;
    QString text = m_subtitleParser->subtitleAt(adjustedPos);

    if (m_subtitleOverlay) {
        m_subtitleOverlay->setText(text);
    }
}

} // namespace VideoPlay
