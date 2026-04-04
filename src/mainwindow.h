#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QVideoWidget>
#include "common.h"

class QAction;
class QMenu;
class QToolBar;
class QLabel;
class QDockWidget;
class QVBoxLayout;
class PlaylistWidget;

namespace VideoPlay {

class PlayerEngine;
class Controls;

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
    void onStateChanged(PlaybackState state);
    void onPositionChanged(qint64 position);
    void onDurationChanged(qint64 duration);
    void onError(const QString& error);
    void onPlaylistDoubleClicked(int index);
    void onToggleFullscreen();
    void onAbout();

private:
    PlayerEngine* m_engine;
    QVideoWidget* m_videoWidget;
    Controls* m_controls;
    PlaylistWidget* m_playlistWidget;
    QDockWidget* m_playlistDock;
    QAction* m_fullscreenAction;
    QLabel* m_timeLabel;
    QLabel* m_statusLabel;
    bool m_isFullscreen;

    void setupUi();
    void setupConnections();
};

} // namespace VideoPlay

#endif
