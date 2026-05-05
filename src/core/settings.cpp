#include "core/settings.h"
#include "utils/logger.h"
#include <algorithm>
#include <filesystem>
#include <fstream>

// 单头文件 JSON 库
#include <nlohmann/json.hpp>

namespace VideoPlay {

Settings& Settings::instance() {
    static Settings instance;
    return instance;
}

Settings::Settings() {
    m_configPath = getConfigPath();
    ensureDirectoryExists();
    load();
}

Settings::~Settings() {
    save();
}

std::string Settings::getConfigPath() const {
#ifdef _WIN32
    const char* appData = getenv("APPDATA");
    if (appData) {
        return std::string(appData) + "/VideoPlay/VideoPlay.json";
    }
#else
    const char* home = getenv("HOME");
    if (home) {
        return std::string(home) + "/.config/VideoPlay/VideoPlay.json";
    }
#endif
    return "./VideoPlay.json";
}

void Settings::ensureDirectoryExists() const {
    std::filesystem::path path(m_configPath);
    std::filesystem::create_directories(path.parent_path());
}

void Settings::load() {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (!std::filesystem::exists(m_configPath)) {
        // 使用默认配置
        m_config = {
            {"window", {
                {"x", 100},
                {"y", 100},
                {"width", 1280},
                {"height", 720},
                {"maximized", false}
            }},
            {"playback", {
                {"volume", 100},
                {"muted", false},
                {"speed", 1.0},
                {"loopMode", 2},
                {"aspectMode", 0},
                {"alwaysOnTop", false}
            }},
            {"recentFiles", nlohmann::json::array()},
            {"subtitle", {
                {"fontFamily", "Microsoft YaHei"},
                {"fontSize", 24},
                {"fontColor", "#FFFFFF"},
                {"hasOutline", true},
                {"outlineColor", "#000000"},
                {"outlineWidth", 2}
            }},
            {"rememberPosition", true},
            {"positions", nlohmann::json::object()},
            {"seriesProgress", nlohmann::json::object()}
        };
        return;
    }

    try {
        std::ifstream file(m_configPath);
        if (file.is_open()) {
            file >> m_config;
        }
    } catch (const std::exception& e) {
        Logger::instance().error("Failed to load settings: " + std::string(e.what()));
        // 使用默认配置
        m_config = nlohmann::json::object();
    }
}

void Settings::save() {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    try {
        std::ofstream file(m_configPath);
        if (file.is_open()) {
            file << m_config.dump(4);
        }
    } catch (const std::exception& e) {
        Logger::instance().error("Failed to save settings: " + std::string(e.what()));
    }
}

void Settings::reset() {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_config = nlohmann::json::object();
    }
    save();
}

// 窗口配置
void Settings::setWindowConfig(const WindowConfig& config) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_config["window"] = {
        {"x", config.x},
        {"y", config.y},
        {"width", config.width},
        {"height", config.height},
        {"maximized", config.maximized}
    };
}

WindowConfig Settings::windowConfig() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    WindowConfig config;
    if (m_config.contains("window")) {
        auto& w = m_config["window"];
        config.x = w.value("x", 100);
        config.y = w.value("y", 100);
        config.width = w.value("width", 1280);
        config.height = w.value("height", 720);
        config.maximized = w.value("maximized", false);
    }
    return config;
}

// 播放设置
void Settings::setVolume(int volume) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_config["playback"]["volume"] = std::clamp(volume, 0, 100);
}

int Settings::volume() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_config.contains("playback")) {
        return m_config["playback"].value("volume", 100);
    }
    return 100;
}

void Settings::setMuted(bool muted) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_config["playback"]["muted"] = muted;
}

bool Settings::isMuted() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_config.contains("playback")) {
        return m_config["playback"].value("muted", false);
    }
    return false;
}

void Settings::setPlaybackSpeed(double speed) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_config["playback"]["speed"] = std::clamp(speed, 0.25, 4.0);
}

double Settings::playbackSpeed() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_config.contains("playback")) {
        return m_config["playback"].value("speed", 1.0);
    }
    return 1.0;
}

void Settings::setLoopMode(LoopMode mode) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_config["playback"]["loopMode"] = static_cast<int>(mode);
}

LoopMode Settings::loopMode() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_config.contains("playback")) {
        int val = m_config["playback"].value("loopMode", 2);
        if (val >= 0 && val <= 2) return static_cast<LoopMode>(val);
    }
    return LoopMode::Playlist;
}

void Settings::setAspectMode(AspectMode mode) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_config["playback"]["aspectMode"] = static_cast<int>(mode);
}

AspectMode Settings::aspectMode() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_config.contains("playback")) {
        int val = m_config["playback"].value("aspectMode", 0);
        if (val >= 0 && val <= 3) return static_cast<AspectMode>(val);
    }
    return AspectMode::Original;
}

void Settings::setAlwaysOnTop(bool enabled) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_config["playback"]["alwaysOnTop"] = enabled;
}

bool Settings::alwaysOnTop() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_config.contains("playback")) {
        return m_config["playback"].value("alwaysOnTop", false);
    }
    return false;
}

// 最近文件
void Settings::addRecentFile(const std::string& path) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (!m_config.contains("recentFiles")) {
        m_config["recentFiles"] = nlohmann::json::array();
    }
    
    auto& recent = m_config["recentFiles"];
    // 移除已存在的相同路径
    recent.erase(std::remove(recent.begin(), recent.end(), path), recent.end());
    // 添加到开头
    recent.insert(recent.begin(), path);
    // 限制数量
    if (recent.size() > 10) {
        while (recent.size() > 10) recent.erase(recent.end() - 1);
    }
}

std::vector<std::string> Settings::recentFiles() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<std::string> result;
    if (m_config.contains("recentFiles") && m_config["recentFiles"].is_array()) {
        for (const auto& item : m_config["recentFiles"]) {
            if (item.is_string()) {
                result.push_back(item.get<std::string>());
            }
        }
    }
    return result;
}

void Settings::clearRecentFiles() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_config["recentFiles"] = nlohmann::json::array();
}

// 字幕样式
void Settings::setSubtitleStyle(const SubtitleStyle& style) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_config["subtitle"] = {
        {"fontFamily", style.fontFamily},
        {"fontSize", style.fontSize},
        {"fontColor", style.fontColor},
        {"hasOutline", style.hasOutline},
        {"outlineColor", style.outlineColor},
        {"outlineWidth", style.outlineWidth}
    };
}

SubtitleStyle Settings::subtitleStyle() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    SubtitleStyle style;
    if (m_config.contains("subtitle")) {
        auto& s = m_config["subtitle"];
        style.fontFamily = s.value("fontFamily", "Microsoft YaHei");
        style.fontSize = s.value("fontSize", 24);
        style.fontColor = s.value("fontColor", "#FFFFFF");
        style.hasOutline = s.value("hasOutline", true);
        style.outlineColor = s.value("outlineColor", "#000000");
        style.outlineWidth = s.value("outlineWidth", 2);
    }
    return style;
}

// 播放位置记忆
void Settings::setRememberPosition(bool remember) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_config["rememberPosition"] = remember;
}

bool Settings::rememberPosition() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_config.value("rememberPosition", true);
}

void Settings::setLastPosition(const std::string& filePath, int64_t position) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_config.contains("positions")) {
        m_config["positions"] = nlohmann::json::object();
    }
    m_config["positions"][filePath] = position;
}

int64_t Settings::lastPosition(const std::string& filePath) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_config.contains("positions") && m_config["positions"].contains(filePath)) {
        return m_config["positions"][filePath].get<int64_t>();
    }
    return 0;
}

void Settings::setLastDuration(const std::string& filePath, int64_t duration) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_config.contains("durations")) {
        m_config["durations"] = nlohmann::json::object();
    }
    m_config["durations"][filePath] = duration;
}

int64_t Settings::lastDuration(const std::string& filePath) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_config.contains("durations") && m_config["durations"].contains(filePath)) {
        return m_config["durations"][filePath].get<int64_t>();
    }
    return 0;
}

// 剧集进度记忆
void Settings::setSeriesProgress(const std::string& seriesKey, int lastEpisodeIndex,
                                 const std::unordered_map<std::string, int64_t>& positions) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_config.contains("seriesProgress")) {
        m_config["seriesProgress"] = nlohmann::json::object();
    }
    nlohmann::json j;
    j["lastEpisodeIndex"] = lastEpisodeIndex;
    nlohmann::json posJson = nlohmann::json::object();
    for (const auto& pair : positions) {
        posJson[pair.first] = pair.second;
    }
    j["episodePositions"] = posJson;
    m_config["seriesProgress"][seriesKey] = j;
}

SeriesProgress Settings::seriesProgress(const std::string& seriesKey) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    SeriesProgress progress;
    progress.seriesKey = seriesKey;
    if (m_config.contains("seriesProgress") && m_config["seriesProgress"].contains(seriesKey)) {
        const auto& j = m_config["seriesProgress"][seriesKey];
        progress.lastEpisodeIndex = j.value("lastEpisodeIndex", 0);
        if (j.contains("episodePositions")) {
            for (const auto& [key, val] : j["episodePositions"].items()) {
                if (val.is_number()) {
                    progress.episodePositions[key] = val.get<int64_t>();
                }
            }
        }
    }
    return progress;
}

// 意外停止自动恢复
void Settings::setLastSession(const SessionInfo& session) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_config["lastSession"] = {
        {"filePath", session.filePath},
        {"position", session.position},
        {"duration", session.duration},
        {"playlistIndex", session.playlistIndex},
        {"hasValidSession", session.hasValidSession}
    };
}

SessionInfo Settings::lastSession() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    SessionInfo info;
    if (m_config.contains("lastSession")) {
        const auto& s = m_config["lastSession"];
        info.filePath = s.value("filePath", std::string());
        info.position = s.value("position", 0);
        info.duration = s.value("duration", 0);
        info.playlistIndex = s.value("playlistIndex", size_t(0));
        info.hasValidSession = s.value("hasValidSession", false);
    }
    return info;
}

void Settings::clearLastSession() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_config["lastSession"] = {
        {"filePath", std::string()},
        {"position", 0},
        {"duration", 0},
        {"playlistIndex", 0},
        {"hasValidSession", false}
    };
}

// AI 配置
void Settings::setAIConfig(const AIConfig& config) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_config["ai"] = {
            {"baseUrl", config.baseUrl},
            {"apiKey", config.apiKey},
            {"model", config.model},
            {"cacheDir", config.cacheDir},
            {"autoAnalyze", config.autoAnalyze}
        };
    }
    save();
}

AIConfig Settings::aiConfig() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    AIConfig config;
    if (m_config.contains("ai")) {
        const auto& ai = m_config["ai"];
        config.baseUrl = ai.value("baseUrl", "https://api.xiaomimimo.com/v1");
        config.apiKey = ai.value("apiKey", std::string());
        config.model = ai.value("model", "mimo-v2-pro");
        config.cacheDir = ai.value("cacheDir", std::string());
        config.autoAnalyze = ai.value("autoAnalyze", false);
    }
    return config;
}

} // namespace VideoPlay
