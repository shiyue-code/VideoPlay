#ifndef SETTINGS_H
#define SETTINGS_H

#include <string>
#include <vector>
#include <mutex>
#include <cstdint>
#include <unordered_map>
#include <nlohmann/json.hpp>

namespace VideoPlay {

struct WindowConfig {
    int x = 100;
    int y = 100;
    int width = 1280;
    int height = 720;
    bool maximized = false;
};

struct SubtitleStyle {
    std::string fontFamily = "Microsoft YaHei";
    int fontSize = 24;
    std::string fontColor = "#FFFFFF";
    bool hasOutline = true;
    std::string outlineColor = "#000000";
    int outlineWidth = 2;
};

struct SeriesProgress {
    std::string seriesKey;      // 文件夹路径+系列名
    int lastEpisodeIndex = 0;
    std::unordered_map<std::string, int64_t> episodePositions; // 文件路径->位置
};

class Settings {
public:
    static Settings& instance();

    // 窗口配置
    void setWindowConfig(const WindowConfig& config);
    WindowConfig windowConfig() const;

    // 播放设置
    void setVolume(int volume);
    int volume() const;
    void setMuted(bool muted);
    bool isMuted() const;
    void setPlaybackSpeed(double speed);
    double playbackSpeed() const;

    // 最近文件
    void addRecentFile(const std::string& path);
    std::vector<std::string> recentFiles() const;
    void clearRecentFiles();

    // 字幕样式
    void setSubtitleStyle(const SubtitleStyle& style);
    SubtitleStyle subtitleStyle() const;

    // 播放位置记忆
    void setRememberPosition(bool remember);
    bool rememberPosition() const;
    void setLastPosition(const std::string& filePath, int64_t position);
    int64_t lastPosition(const std::string& filePath) const;
    void setLastDuration(const std::string& filePath, int64_t duration);
    int64_t lastDuration(const std::string& filePath) const;

    // 剧集进度记忆
    void setSeriesProgress(const std::string& seriesKey, int lastEpisodeIndex,
                           const std::unordered_map<std::string, int64_t>& positions);
    SeriesProgress seriesProgress(const std::string& seriesKey) const;

    // 保存和加载
    void save();
    void load();
    void reset();

private:
    Settings();
    ~Settings();
    Settings(const Settings&) = delete;
    Settings& operator=(const Settings&) = delete;

    std::string getConfigPath() const;
    void ensureDirectoryExists() const;

    mutable std::mutex m_mutex;
    nlohmann::json m_config;
    std::string m_configPath;
};

} // namespace VideoPlay

#endif // SETTINGS_H
