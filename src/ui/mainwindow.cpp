#include <QDebug>
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
#include <QMediaPlayer>

#include "core/playerengine.h"
#include "ui/controls.h"
#include "ui/playlistwidget.h"

namespace VideoPlay {

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , m_engine(new PlayerEngine(this))
    , m_videoWidget(nullptr)
    , m_controls(nullptr)
    , m_playlistWidget(nullptr)
    , m_playlistDock(nullptr)
    , m_fullscreenAction(nullptr)
    , m_timeLabel(nullptr)
    , m_statusLabel(nullptr)
    , m_isFullscreen(false)
{
    setAcceptDrops(true);
    setupUi();
    setupConnections();
    setMinimumSize(640, 480);
    resize(960, 640);
}

MainWindow::~MainWindow() = default;

bool MainWindow::openFile(const QString& filePath)
{
    if (filePath.isEmpty() || !QFileInfo::exists(filePath))
        return false;

    if (m_engine->loadFile(filePath)) {
        // 检查播放列表中是否已存在，避免重复添加
        for (int i = 0; i < m_playlistWidget->count(); ++i) {
            if (m_playlistWidget->item(i) == filePath) {
                m_playlistWidget->setCurrentIndex(i);
                m_engine->play();
                return true;
            }
        }
        m_playlistWidget->addItem(filePath);
        setWindowTitle(QString("%1 - VideoPlay").arg(QFileInfo(filePath).fileName()));
        m_engine->play();
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

    // Use QVideoWidget from Qt Multimedia
    m_videoWidget = new QVideoWidget(central);
    m_engine->mediaPlayer()->setVideoOutput(m_videoWidget);
    
    m_controls = new Controls(central);

    layout->addWidget(m_videoWidget, 1);
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

    QMenu* viewMenu = menuBar()->addMenu(tr("&View"));
    m_fullscreenAction = viewMenu->addAction(tr("&Fullscreen"), this, &MainWindow::onToggleFullscreen, Qt::Key_F11);
    viewMenu->addAction(m_playlistDock->toggleViewAction());

    menuBar()->addMenu(tr("&Help"))->addAction(tr("&About"), this, &MainWindow::onAbout);

    // Toolbar
    QToolBar* toolbar = addToolBar(tr("Main"));
    toolbar->addAction(fileMenu->actions().first());
    toolbar->addSeparator();
    toolbar->addAction(playbackMenu->actions().at(0)); // Play/Pause
    toolbar->addAction(playbackMenu->actions().at(1)); // Stop

    // Status bar
    m_statusLabel = new QLabel(tr("Ready"), this);
    m_timeLabel = new QLabel("00:00 / 00:00", this);
    statusBar()->addWidget(m_statusLabel, 1);
    statusBar()->addPermanentWidget(m_timeLabel);
}

void MainWindow::setupConnections()
{
    // Engine signals
    connect(m_engine, &PlayerEngine::stateChanged, this, &MainWindow::onStateChanged);
    connect(m_engine, &PlayerEngine::positionChanged, this, &MainWindow::onPositionChanged);
    connect(m_engine, &PlayerEngine::durationChanged, this, &MainWindow::onDurationChanged);
    connect(m_engine, &PlayerEngine::errorOccurred, this, &MainWindow::onError);

    // Controls signals
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

    // Playlist
    connect(m_playlistWidget, &PlaylistWidget::itemDoubleClicked, this, &MainWindow::onPlaylistDoubleClicked);

    // Video widget - using QVideoWidget, handle double click via event
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
    default:
        QMainWindow::keyPressEvent(event);
    }
}

void MainWindow::closeEvent(QCloseEvent* event)
{
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
    switch (state) {
    case PlaybackState::Stopped: m_statusLabel->setText(tr("Stopped")); break;
    case PlaybackState::Playing: m_statusLabel->setText(tr("Playing")); break;
    case PlaybackState::Paused:  m_statusLabel->setText(tr("Paused"));  break;
    }
    m_controls->setPlaybackState(state);
}

void MainWindow::onPositionChanged(qint64 position)
{
    m_controls->setPosition(position);
    m_timeLabel->setText(QString("%1 / %2").arg(formatTime(position)).arg(formatTime(m_engine->duration())));
}

void MainWindow::onDurationChanged(qint64 duration)
{
    m_controls->setDuration(duration);
}

void MainWindow::onError(const QString& error)
{
    m_statusLabel->setText(tr("Error: %1").arg(error));
    QMessageBox::warning(this, tr("Playback Error"), error);
    qDebug() << "Playback error:" << error;
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
    if (m_isFullscreen) showFullScreen();
    else showNormal();
}

void MainWindow::onAbout()
{
    QMessageBox::about(this, tr("About VideoPlay"),
        tr("<h3>VideoPlay 1.0</h3><p>A modern video player built with Qt6.</p>"));
}

void MainWindow::mouseDoubleClickEvent(QMouseEvent* event)
{
    // 双击播放界面：暂停/播放切换
    if (m_engine->state() == PlaybackState::Playing) {
        m_engine->pause();
    } else {
        m_engine->play();
    }
    event->accept();
}

} // namespace VideoPlay
