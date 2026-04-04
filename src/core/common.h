#ifndef COMMON_H
#define COMMON_H

#include <QString>
#include <QStringList>
#include <QList>
#include <QUrl>
#include <QMetaType>

namespace VideoPlay {

enum class PlaybackState {
    Stopped,
    Playing,
    Paused
};

enum class PlaybackSpeed {
    Speed_0_25 = 0,
    Speed_0_5,
    Speed_0_75,
    Speed_1_0,
    Speed_1_25,
    Speed_1_5,
    Speed_2_0,
    Speed_4_0
};

inline double playbackSpeedToDouble(PlaybackSpeed speed) {
    switch (speed) {
        case PlaybackSpeed::Speed_0_25: return 0.25;
        case PlaybackSpeed::Speed_0_5:  return 0.5;
        case PlaybackSpeed::Speed_0_75: return 0.75;
        case PlaybackSpeed::Speed_1_0:  return 1.0;
        case PlaybackSpeed::Speed_1_25: return 1.25;
        case PlaybackSpeed::Speed_1_5:  return 1.5;
        case PlaybackSpeed::Speed_2_0:  return 2.0;
        case PlaybackSpeed::Speed_4_0:  return 4.0;
    }
    return 1.0;
}

inline PlaybackSpeed doubleToPlaybackSpeed(double rate) {
    if (rate <= 0.3) return PlaybackSpeed::Speed_0_25;
    if (rate <= 0.6) return PlaybackSpeed::Speed_0_5;
    if (rate <= 0.8) return PlaybackSpeed::Speed_0_75;
    if (rate <= 1.1) return PlaybackSpeed::Speed_1_0;
    if (rate <= 1.35) return PlaybackSpeed::Speed_1_25;
    if (rate <= 1.75) return PlaybackSpeed::Speed_1_5;
    if (rate <= 3.0) return PlaybackSpeed::Speed_2_0;
    return PlaybackSpeed::Speed_4_0;
}

inline QString formatTime(qint64 ms) {
    qint64 seconds = ms / 1000;
    qint64 minutes = seconds / 60;
    qint64 hours = minutes / 60;
    seconds %= 60;
    minutes %= 60;

    if (hours > 0) {
        return QString("%1:%2:%3")
            .arg(hours)
            .arg(minutes, 2, 10, QChar('0'))
            .arg(seconds, 2, 10, QChar('0'));
    }
    return QString("%1:%2")
        .arg(minutes)
        .arg(seconds, 2, 10, QChar('0'));
}

} // namespace VideoPlay

#endif // COMMON_H
