#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTimer>
#include "core/common.h"

class QAction;
class QLabel;
class QDockWidget;
class PlaylistWidget;
class SubtitleParser;

namespace VideoPlay {

class PlayerEngine;
class Controls;
class VideoRenderer;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

    bool openFile(const QString& filePath);

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private slots:
    void onOpenFile();
    void onLoadSubtitle();
    void onStateChanged(PlaybackState state);
    void onPositionChanged(qint64 position);
    void onDurationChanged(qint64 duration);
    void onError(const QString& error);
    void onPlaylistDoubleClicked(int index);
    void onToggleFullscreen();
    void onAbout();
    void onToggleAlwaysOnTop();
    void onToggleLoopMode();
    void onTakeScreenshot();
    void onSubtitleDelayPlus();
    void onSubtitleDelayMinus();
    void onMouseIdleTimeout();

private:
    void setupUi();
    void setupConnections();
    void loadSettings();
    void saveSettings();
    void updateWindowTitle();
    void updateSubtitles(qint64 position);
    void enterFullscreen();
    void exitFullscreen();
    void updateControlsPosition();

    PlayerEngine* m_engine;
    VideoRenderer* m_videoRenderer;
    Controls* m_controls;
    PlaylistWidget* m_playlistWidget;
    QDockWidget* m_playlistDock;
    SubtitleParser* m_subtitleParser;
    QAction* m_fullscreenAction;
    QAction* m_alwaysOnTopAction;
    QAction* m_loopModeAction;
    QLabel* m_timeLabel;
    QLabel* m_statusLabel;
    
    QTimer* m_mouseIdleTimer;
    QPoint m_lastMousePos;
    
    bool m_isFullscreen;
    bool m_isImmersiveMode;
    bool m_alwaysOnTop;
    int m_loopMode;
    int m_subtitleDelay;
};

} // namespace VideoPlay

#endif
