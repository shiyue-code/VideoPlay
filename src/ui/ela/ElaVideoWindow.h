#ifndef ELAVIDEOWINDOW_H
#define ELAVIDEOWINDOW_H

#include <ElaWindow.h>
#include "core/common.h"

class ElaNavigationBar;
class ElaIconButton;
class ElaSlider;
class ElaText;
class ElaToggleSwitch;

namespace VideoPlay {

class PlayerEngine;
class VideoRenderer;

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
    void onLoadSubtitle();
    void onTakeScreenshot();
    void onToggleMute();
    void onToggleLoop();
    
    void onPlaybackStateChanged(PlaybackState state);
    void onPositionChanged(qint64 position);
    void onDurationChanged(qint64 duration);
    void onError(const QString& error);
    
    // 滑块控制
    void onSeekSliderChanged(int value);
    void onSeekSliderReleased();
    void onVolumeSliderChanged(int value);
    void onSpeedSliderChanged(int value);

private:
    void setupUi();
    void setupConnections();
    void loadSettings();
    void saveSettings();
    void updatePlayPauseIcon(PlaybackState state);
    void updateTimeLabel();
    void updateVolumeIcon();
    void updateSpeedLabel();

    PlayerEngine* m_engine;
    
    QWidget* m_centralWidget;
    VideoRenderer* m_videoRenderer;
    QWidget* m_controlPanel;
    
    // 播放控制
    ElaIconButton* m_playPauseBtn;
    ElaIconButton* m_stopBtn;
    ElaIconButton* m_prevBtn;
    ElaIconButton* m_nextBtn;
    
    // 进度条
    ElaSlider* m_progressSlider;
    ElaText* m_timeLabel;
    
    // 音量控制
    ElaIconButton* m_muteBtn;
    ElaSlider* m_volumeSlider;
    ElaText* m_volumeLabel;
    
    // 速度控制（无极调节）
    ElaText* m_speedLabel;
    ElaSlider* m_speedSlider;
    
    // 功能按钮
    ElaIconButton* m_openFileBtn;
    ElaIconButton* m_screenshotBtn;
    ElaIconButton* m_subtitleBtn;
    ElaIconButton* m_loopBtn;
    ElaIconButton* m_fullscreenBtn;
    
    qint64 m_duration;
    qint64 m_currentPosition;
    bool m_isFullscreen;
    bool m_isMuted;
    bool m_isLooping;
    double m_currentSpeed;
};

} // namespace VideoPlay

#endif // ELAVIDEOWINDOW_H
