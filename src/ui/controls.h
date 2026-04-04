#ifndef VIDEOPLAY_SRC_CONTROLS_H
#define VIDEOPLAY_SRC_CONTROLS_H

#include <QWidget>
#include <QPushButton>
#include <QSlider>
#include <QLabel>
#include <QComboBox>
#include <QTimer>

#include "core/common.h"

namespace VideoPlay {

class Controls : public QWidget {
    Q_OBJECT

public:
    explicit Controls(QWidget* parent = nullptr);

    void setPlaybackState(PlaybackState state);
    void setPosition(qint64 position);
    void setDuration(qint64 duration);
    void setVolume(int volume);
    void setMuted(bool muted);
    void setPlaybackSpeed(double speed);

signals:
    void playClicked();
    void pauseClicked();
    void stopClicked();
    void seekRequested(qint64 position);
    void volumeChanged(int volume);
    void muteToggled();
    void speedChanged(double speed);
    void fullscreenClicked();

protected:
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

private slots:
    void onPlayPauseClicked();
    void onPositionSliderMoved(int value);
    void onPositionSliderPressed();
    void onPositionSliderReleased();
    void onVolumeSliderMoved(int value);
    void onSpeedSliderMoved(int value);
    void onHideTimeout();

private:
    void setupUi();
    void applyStyling();
    void updatePlayPauseIcon();

    QPushButton* m_playBtn;
    QPushButton* m_stopBtn;
    QSlider* m_positionSlider;
    QLabel* m_timeLabel;
    QPushButton* m_muteBtn;
    QSlider* m_volumeSlider;
    QSlider* m_speedSlider;
    QLabel* m_speedLabel;
    QPushButton* m_fullscreenBtn;
    QTimer* m_hideTimer;

    PlaybackState m_state;
    qint64 m_duration;
    bool m_sliderPressed;
};

} // namespace VideoPlay

#endif // VIDEOPLAY_SRC_CONTROLS_H
