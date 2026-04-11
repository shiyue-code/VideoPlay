#ifndef ELAVIDEOWINDOW_H
#define ELAVIDEOWINDOW_H

#include <ElaWindow.h>
#include "core/common.h"

class ElaNavigationBar;
class ElaIconButton;
class ElaSlider;
class ElaText;
class ElaToggleSwitch;
class ElaComboBox;
class QVBoxLayout;
class QHBoxLayout;

namespace VideoPlay {

class PlayerEngine;
class VideoRenderer;
class SubtitleParser;

class ElaVideoWindow : public ElaWindow {
    Q_OBJECT

public:
    explicit ElaVideoWindow(QWidget* parent = nullptr);
    ~ElaVideoWindow();

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onPlayPauseClicked();
    void onStopClicked();
    void onToggleFullscreen();
    void onOpenFile();
    void onPlaybackStateChanged(PlaybackState state);
    void onPositionChanged(qint64 position);
    void onDurationChanged(qint64 duration);
    void onError(const QString& error);
    void onSpeedChanged(int index);

private:
    void setupUi();
    void setupConnections();
    void loadSettings();
    void saveSettings();
    void updatePlayPauseIcon(PlaybackState state);
    void updateTimeLabel();

    PlayerEngine* m_engine;
    SubtitleParser* m_subtitleParser;
    
    QWidget* m_centralWidget;
    VideoRenderer* m_videoRenderer;
    QWidget* m_controlPanel;
    
    // Controls
    ElaIconButton* m_playPauseBtn;
    ElaIconButton* m_stopBtn;
    ElaSlider* m_progressSlider;
    ElaText* m_timeLabel;
    ElaComboBox* m_speedCombo;
    ElaIconButton* m_fullscreenBtn;
    
    qint64 m_duration;
    qint64 m_currentPosition;
    bool m_isFullscreen;
};

} // namespace VideoPlay

#endif // ELAVIDEOWINDOW_H
