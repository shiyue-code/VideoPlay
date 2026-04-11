#include <QApplication>
#include <QMenuBar>
#include <QStatusBar>
#include <QToolBar>
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
#include <QScreen>
#include <QWindow>
#include <QPixmap>
#include <QDateTime>
#include <QStandardPaths>
#include <QGuiApplication>
#include <QPainter>
#include "ui/mainwindow.h"

#include "core/playerengine.h"
#include "core/settings.h"
#include "utils/logger.h"
#include "ui/controls.h"
#include "ui/playlistwidget.h"
#include "ui/videorenderer.h"
#include "subtitles/subtitleparser.h"
#include "core/common.h"

namespace VideoPlay {

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , m_engine(new PlayerEngine(this))
    , m_videoRenderer(nullptr)
    , m_controls(nullptr)
    , m_playlistWidget(nullptr)
    , m_playlistDock(nullptr)
    , m_subtitleParser(new SubtitleParser(this))
    , m_fullscreenAction(nullptr)
    , m_alwaysOnTopAction(nullptr)
    , m_loopModeAction(nullptr)
    , m_timeLabel(nullptr)
    , m_statusLabel(nullptr)
    , m_mouseIdleTimer(new QTimer(this))
    , m_lastMousePos(-1, -1)
    , m_isFullscreen(false)
    , m_isImmersiveMode(false)
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
    if (filePath.isEmpty() || !QFileInfo::exists(filePath))
        return false;

    Logger::instance().info(QString("Opening file: %1").arg(filePath));

    bool result = m_engine->loadFile(filePath);
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
    // 中央部件
    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // 视频渲染器
    m_videoRenderer = new VideoRenderer(central);
    m_videoRenderer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    layout->addWidget(m_videoRenderer, 1);

    // 控制栏（默认嵌入式）
    m_controls = new Controls(central);
    layout->addWidget(m_controls);

    setCentralWidget(central);

    // 播放列表 dock
    m_playlistWidget = new PlaylistWidget(this);
    m_playlistDock = new QDockWidget(tr("Playlist"), this);
    m_playlistDock->setWidget(m_playlistWidget);
    addDockWidget(Qt::RightDockWidgetArea, m_playlistDock);

    // 菜单栏
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

    // 工具栏
    QToolBar* toolbar = addToolBar(tr("Main"));
    toolbar->addAction(fileMenu->actions().first());
    toolbar->addSeparator();
    toolbar->addAction(playbackMenu->actions().at(0));
    toolbar->addAction(playbackMenu->actions().at(1));

    // 状态栏
    m_statusLabel = new QLabel(tr("Ready"), this);
    m_timeLabel = new QLabel("00:00 / 00:00", this);
    statusBar()->addWidget(m_statusLabel, 1);
    statusBar()->addPermanentWidget(m_timeLabel);

    // 鼠标空闲检测定时器（用于沉浸式模式）
    m_mouseIdleTimer->setInterval(100); // 100ms 检测一次
    connect(m_mouseIdleTimer, &QTimer::timeout, this, &MainWindow::onMouseIdleTimeout);
}

void MainWindow::setupConnections()
{
    // 引擎信号
    connect(m_engine, &PlayerEngine::stateChanged, this, &MainWindow::onStateChanged);
    connect(m_engine, &PlayerEngine::positionChanged, this, &MainWindow::onPositionChanged);
    connect(m_engine, &PlayerEngine::positionChanged, this, &MainWindow::updateSubtitles);
    connect(m_engine, &PlayerEngine::durationChanged, this, &MainWindow::onDurationChanged);
    connect(m_engine, &PlayerEngine::errorOccurred, this, &MainWindow::onError);
    connect(m_engine, &PlayerEngine::videoFrameReady, m_videoRenderer, &VideoRenderer::setFrame);

    // 控制栏信号
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

    // 播放列表信号
    connect(m_playlistWidget, &PlaylistWidget::itemDoubleClicked, this, &MainWindow::onPlaylistDoubleClicked);

    // 视频渲染器信号
    connect(m_videoRenderer, &VideoRenderer::doubleClicked, this, [this]() {
        if (m_engine->state() == PlaybackState::Playing)
            m_engine->pause();
        else
            m_engine->play();
    });
    connect(m_videoRenderer, &VideoRenderer::fileDropped, this, &MainWindow::openFile);
    connect(m_videoRenderer, &VideoRenderer::volumeChangedByWheel, this, [this](int delta) {
        m_engine->setVolume(qBound(0, m_engine->volume() + delta, 100));
    });
}

void MainWindow::loadSettings()
{
    auto& settings = Settings::instance();
    
    QRect geo = settings.windowGeometry();
    setGeometry(geo);

    int vol = settings.volume();
    m_engine->setVolume(vol);
    m_controls->setVolume(vol);

    bool muted = settings.isMuted();
    m_engine->setMuted(muted);
    m_controls->setMuted(muted);

    double speed = settings.playbackSpeed();
    m_engine->setPlaybackSpeed(speed);
    m_controls->setPlaybackSpeed(speed);

    m_alwaysOnTop = false;
    m_alwaysOnTopAction->setChecked(false);

    m_loopMode = 0;
    m_loopModeAction->setText(tr("Loop: &Off"));
    m_loopModeAction->setChecked(false);

    Logger::instance().info(QString("Settings loaded: volume=%1, speed=%2, muted=%3")
                            .arg(vol).arg(speed).arg(muted ? "true" : "false"));
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

void MainWindow::mouseDoubleClickEvent(QMouseEvent* event)
{
    Q_UNUSED(event);
    if (m_engine->state() == PlaybackState::Playing)
        m_engine->pause();
    else
        m_engine->play();
}

void MainWindow::mouseMoveEvent(QMouseEvent* event)
{
    if (m_isImmersiveMode) {
        // 鼠标移动时显示控制栏
        if (!m_controls->isVisible() || m_controls->property("opacity").toReal() < 0.5) {
            m_controls->fadeIn();
        }
        m_mouseIdleTimer->start();
    }
    m_lastMousePos = event->pos();
    QMainWindow::mouseMoveEvent(event);
}

void MainWindow::resizeEvent(QResizeEvent* event)
{
    QMainWindow::resizeEvent(event);
    if (m_isImmersiveMode) {
        updateControlsPosition();
    }
}

void MainWindow::onMouseIdleTimeout()
{
    if (m_isImmersiveMode) {
        QPoint currentPos = QCursor::pos();
        QPoint widgetPos = mapFromGlobal(currentPos);
        
        // 检查鼠标是否在控制栏区域
        QRect controlsRect = m_controls->geometry();
        if (!controlsRect.contains(widgetPos)) {
            // 鼠标不在控制栏上，开始隐藏
            m_controls->fadeOut();
        }
    }
}

void MainWindow::enterFullscreen()
{
    m_isFullscreen = true;
    m_isImmersiveMode = true;
    
    // 先切换全屏，确保窗口大小正确
    showFullScreen();
    
    // 隐藏其他 UI 元素
    m_playlistDock->hide();
    menuBar()->hide();
    statusBar()->hide();
    
    // 切换到悬浮控制栏模式
    m_controls->setParent(centralWidget());
    m_controls->setFloatingMode(true);
    m_controls->setVisible(true);
    m_controls->raise(); // 确保在最上层
    updateControlsPosition();
    
    // 开始鼠标空闲检测
    m_mouseIdleTimer->start();
    setMouseTracking(true);
    centralWidget()->setMouseTracking(true);
    m_videoRenderer->setMouseTracking(true);
}

void MainWindow::exitFullscreen()
{
    m_isFullscreen = false;
    m_isImmersiveMode = false;
    
    // 恢复 UI 元素
    menuBar()->show();
    statusBar()->show();
    m_playlistDock->show();
    
    // 恢复嵌入式控制栏
    m_mouseIdleTimer->stop();
    m_controls->setFloatingMode(false);
    
    auto* layout = qobject_cast<QVBoxLayout*>(centralWidget()->layout());
    if (layout) {
        layout->addWidget(m_controls);
    }
    
    setMouseTracking(false);
    centralWidget()->setMouseTracking(false);
    m_videoRenderer->setMouseTracking(false);
    
    showNormal();
}

void MainWindow::updateControlsPosition()
{
    if (!m_isImmersiveMode) return;
    
    // 控制栏悬浮在底部，宽度占满，有边距
    int margin = 20;
    int height = 70;
    int width = centralWidget()->width() - 2 * margin;
    int x = margin;
    int y = centralWidget()->height() - height - margin;
    
    m_controls->setGeometry(x, y, width, height);
}

void MainWindow::onToggleFullscreen()
{
    if (!m_isFullscreen) {
        enterFullscreen();
    } else {
        exitFullscreen();
    }
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
    m_controls->setPlaybackState(state);

    if (state == PlaybackState::Playing) {
        m_statusLabel->setText(tr("Playing: %1").arg(QFileInfo(m_engine->filePath()).fileName()));
    } else if (state == PlaybackState::Paused) {
        m_statusLabel->setText(tr("Paused"));
    } else {
        m_statusLabel->setText(tr("Stopped"));
        m_videoRenderer->setSubtitleText(QString());
    }
}

void MainWindow::onPositionChanged(qint64 position)
{
    m_controls->setPosition(position);
    m_timeLabel->setText(QString("%1 / %2")
                         .arg(formatTime(position))
                         .arg(formatTime(m_engine->duration())));
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
    QScreen* screen = windowHandle()->screen();
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }
    if (!screen) return;

    QPoint globalPos = m_videoRenderer->mapToGlobal(QPoint(0, 0));
    QSize videoSize = m_videoRenderer->size();

    QPixmap screenshot = screen->grabWindow(0, globalPos.x(), globalPos.y(), 
                                            videoSize.width(), videoSize.height());

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

void MainWindow::onLoadSubtitle()
{
    QString path = QFileDialog::getOpenFileName(this, tr("Load Subtitle"), QString(),
        tr("Subtitles (*.srt *.ass *.ssa *.vtt);;All (*)"));
    if (path.isEmpty())
        return;

    if (m_subtitleParser->loadFile(path)) {
        m_statusLabel->setText(tr("Subtitle loaded: %1").arg(QFileInfo(path).fileName()));
        Logger::instance().info(QString("Subtitle loaded: %1").arg(path));
        updateSubtitles(m_engine->position());
    } else {
        m_statusLabel->setText(tr("Failed to load subtitle"));
        Logger::instance().error(QString("Failed to load subtitle: %1").arg(path));
    }
}

void MainWindow::onSubtitleDelayPlus()
{
    m_subtitleDelay += 100;
    m_statusLabel->setText(tr("Subtitle delay: +%1ms").arg(m_subtitleDelay));
    updateSubtitles(m_engine->position());
}

void MainWindow::onSubtitleDelayMinus()
{
    m_subtitleDelay -= 100;
    m_statusLabel->setText(tr("Subtitle delay: %1ms").arg(m_subtitleDelay));
    updateSubtitles(m_engine->position());
}

void MainWindow::updateSubtitles(qint64 position)
{
    if (!m_subtitleParser->isLoaded()) {
        m_videoRenderer->setSubtitleText(QString());
        return;
    }

    qint64 adjustedPos = position + m_subtitleDelay;
    QString text = m_subtitleParser->subtitleAt(adjustedPos);
    m_videoRenderer->setSubtitleText(text);
}

} // namespace VideoPlay
