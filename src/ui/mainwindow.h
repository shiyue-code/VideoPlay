#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include "core/common.h"

class QAction;
class QMenu;
class QToolBar;
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

private:
    PlayerEngine* m_engine;
    VideoRenderer* m_videoRenderer;  // 统一的视频+字幕渲染器
    Controls* m_controls;
    PlaylistWidget* m_playlistWidget;
    QDockWidget* m_playlistDock;
    SubtitleParser* m_subtitleParser;
    QAction* m_fullscreenAction;
    QAction* m_alwaysOnTopAction;
    QAction* m_loopModeAction;
    QLabel* m_timeLabel;
    QLabel* m_statusLabel;
    bool m_isFullscreen;
    bool m_alwaysOnTop;
    int m_loopMode;
    int m_subtitleDelay;

    void setupUi();
    void setupConnections();
    void loadSettings();
    void saveSettings();
    void updateWindowTitle();
    void updateSubtitles(qint64 position);
};

} // namespace VideoPlay

#endif
