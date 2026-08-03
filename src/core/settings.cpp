#include "core/settings.h"
#include "utils/logger.h"
#include "utils/string_utils.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>

// 单头文件 JSON 库
#include <nlohmann/json.hpp>

namespace VideoPlay {

namespace {
Logger& logger() {
    static auto logger = Logger::get("settings");
    return *logger;
}


std::string normalizeAIProvider(std::string provider) {
    provider = toLower(trim(provider));
    if (provider.empty() || provider == "auto") {
        return "mimo";
    }
    if (provider == "google") {
        return "gemini";
    }
    if (provider == "xiaomi" || provider == "xiaomimimo") {
        return "mimo";
    }
    return provider;
}

AIProviderConfig defaultAIProviderConfig(const std::string& provider) {
    if (provider == "gemini") {
        return {"https://generativelanguage.googleapis.com", std::string(), "gemini-2.5-flash"};
    }
    return {"https://api.xiaomimimo.com", std::string(), "mimo-v2.5"};
}

void sanitizeAIBaseUrl(std::string& url) {
    url = trim(url);

    size_t firstHttps = url.find("https://");
    if (firstHttps != std::string::npos) {
        size_t secondHttps = url.find("https://", firstHttps + 1);
        if (secondHttps != std::string::npos) {
            url = url.substr(0, secondHttps);
        }
    }

    while (!url.empty() && url.back() == '/') {
        url.pop_back();
    }

    if (url.size() >= 7 && url.substr(url.size() - 7) == "/v1beta") {
        url = url.substr(0, url.size() - 7);
    } else if (url.size() >= 3 && url.substr(url.size() - 3) == "/v1") {
        url = url.substr(0, url.size() - 3);
    }
}

void normalizeAIProviderConfig(const std::string& provider, AIProviderConfig& providerConfig) {
    sanitizeAIBaseUrl(providerConfig.baseUrl);
    providerConfig.model = trim(providerConfig.model);

    AIProviderConfig defaults = defaultAIProviderConfig(provider);
    std::string lowerUrl = toLower(providerConfig.baseUrl);
    std::string lowerModel = toLower(providerConfig.model);

    if (provider == "gemini") {
        if (providerConfig.baseUrl.empty() ||
            lowerUrl.find("xiaomimimo") != std::string::npos) {
            providerConfig.baseUrl = defaults.baseUrl;
        }
        if (providerConfig.model.empty() || lowerModel.find("mimo") != std::string::npos ||
            lowerModel == "gemini-3.5-flash") {
            providerConfig.model = defaults.model;
        }
        return;
    }

    if (provider == "mimo") {
        if (providerConfig.baseUrl.empty() ||
            lowerUrl.find("generativelanguage.googleapis.com") != std::string::npos) {
            providerConfig.baseUrl = defaults.baseUrl;
        }
        if (providerConfig.model.empty() || lowerModel.find("gemini") != std::string::npos) {
            providerConfig.model = defaults.model;
        }
    }
}
}


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
                {"alwaysOnTop", false},
                {"hardwareDecodingEnabled", true},
                {"audioFilter", {
                    {"enabled", false},
                    {"preset", 0},
                    {"preampDb", 0.0},
                    {"limiterEnabled", false},
                    {"dynamicNormalizerEnabled", false},
                    {"eqBands", nlohmann::json::array()}
                }}
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
            {"log", {
                {"enabled", true},
                {"consoleOutput", true},
                {"level", "info"},
                {"modules", nlohmann::json::object()},
                {"filePath", std::string()},
                {"maxFileSize", 5 * 1024 * 1024},
                {"maxFiles", 3},
                {"flushOnWarning", true}
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
        logger().error("Failed to load settings: " + std::string(e.what()));
        // 使用默认配置
        m_config = nlohmann::json::object();
    }

    if (!m_config.contains("log")) {
        m_config["log"] = {
            {"enabled", true},
            {"consoleOutput", true},
            {"level", "info"},
            {"modules", nlohmann::json::object()},
            {"filePath", std::string()},
            {"maxFileSize", 5 * 1024 * 1024},
            {"maxFiles", 3},
            {"flushOnWarning", true}
        };
    }
    if (!m_config.contains("playback")) {
        m_config["playback"] = nlohmann::json::object();
    }
    if (!m_config["playback"].contains("audioFilter")) {
        m_config["playback"]["audioFilter"] = {
            {"enabled", false},
            {"preset", 0},
            {"preampDb", 0.0},
            {"limiterEnabled", false},
            {"dynamicNormalizerEnabled", false},
            {"eqBands", nlohmann::json::array()}
        };
    }
    if (!m_config["playback"].contains("hardwareDecodingEnabled")) {
        m_config["playback"]["hardwareDecodingEnabled"] = true;
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
        logger().error("Failed to save settings: " + std::string(e.what()));
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
        {"maximized", config.maximized},
        {"borderless", config.borderless}
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
        config.borderless = w.value("borderless", true);
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

void Settings::setHardwareDecodingEnabled(bool enabled) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_config["playback"]["hardwareDecodingEnabled"] = enabled;
}

bool Settings::hardwareDecodingEnabled() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_config.contains("playback")) {
        return m_config["playback"].value("hardwareDecodingEnabled", true);
    }
    return true;
}

void Settings::setAudioFilterConfig(const AudioFilterConfig& config) {
    std::lock_guard<std::mutex> lock(m_mutex);

    nlohmann::json eqBands = nlohmann::json::array();
    for (const auto& band : config.eqBands) {
        eqBands.push_back({
            {"frequency", band.frequency},
            {"width", band.width},
            {"gainDb", band.gainDb}
        });
    }

    m_config["playback"]["audioFilter"] = {
        {"enabled", config.enabled},
        {"preset", static_cast<int>(config.preset)},
        {"preampDb", config.preampDb},
        {"limiterEnabled", config.limiterEnabled},
        {"dynamicNormalizerEnabled", config.dynamicNormalizerEnabled},
        {"eqBands", eqBands}
    };
}

AudioFilterConfig Settings::audioFilterConfig() const {
    std::lock_guard<std::mutex> lock(m_mutex);

    AudioFilterConfig config;
    if (!m_config.contains("playback") ||
        !m_config["playback"].contains("audioFilter")) {
        return config;
    }

    const auto& audio = m_config["playback"]["audioFilter"];
    config.enabled = audio.value("enabled", false);
    int preset = audio.value("preset", 0);
    if (preset < 0 || preset > 3) {
        preset = 0;
    }
    config.preset = static_cast<AudioFilterPreset>(preset);
    config.preampDb = audio.value("preampDb", 0.0);
    config.limiterEnabled = audio.value("limiterEnabled", false);
    config.dynamicNormalizerEnabled = audio.value("dynamicNormalizerEnabled", false);

    if (audio.contains("eqBands") && audio["eqBands"].is_array()) {
        for (const auto& item : audio["eqBands"]) {
            EQBand band;
            band.frequency = item.value("frequency", 1000.0);
            band.width = item.value("width", 1.0);
            band.gainDb = item.value("gainDb", 0.0);
            if (band.frequency > 0.0 && band.width > 0.0) {
                config.eqBands.push_back(band);
            }
        }
    }

    return config;
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
    std::string provider = normalizeAIProvider(config.provider);

    std::unordered_map<std::string, AIProviderConfig> providers;
    for (const auto& entry : config.providers) {
        std::string providerId = normalizeAIProvider(entry.first);
        AIProviderConfig providerConfig = entry.second;
        normalizeAIProviderConfig(providerId, providerConfig);
        providers[providerId] = providerConfig;
    }

    AIProviderConfig activeConfig = {
        config.baseUrl,
        config.apiKey,
        config.model
    };

    if (activeConfig.baseUrl.empty() && activeConfig.apiKey.empty() && activeConfig.model.empty()) {
        auto activeIt = providers.find(provider);
        activeConfig = (activeIt != providers.end()) ?
            activeIt->second :
            defaultAIProviderConfig(provider);
    }
    normalizeAIProviderConfig(provider, activeConfig);
    providers[provider] = activeConfig;

    if (providers.find("mimo") == providers.end()) {
        providers["mimo"] = defaultAIProviderConfig("mimo");
    }
    if (providers.find("gemini") == providers.end()) {
        providers["gemini"] = defaultAIProviderConfig("gemini");
    }

    nlohmann::json providersJson = nlohmann::json::object();
    for (const auto& entry : providers) {
        providersJson[entry.first] = {
            {"baseUrl", entry.second.baseUrl},
            {"apiKey", entry.second.apiKey},
            {"model", entry.second.model}
        };
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_config["ai"] = {
            {"provider", provider},
            {"baseUrl", activeConfig.baseUrl},
            {"apiKey", activeConfig.apiKey},
            {"model", activeConfig.model},
            {"cacheDir", config.cacheDir},
            {"autoAnalyze", config.autoAnalyze},
            {"analysisDetailLevel", std::clamp(config.analysisDetailLevel, 0, 2)},
            {"providers", providersJson}
        };
    }
    save();
}

AIConfig Settings::aiConfig() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    AIConfig config;
    if (m_config.contains("ai")) {
        const auto& ai = m_config["ai"];
        config.provider = ai.value("provider", std::string());
        config.baseUrl = ai.value("baseUrl", "https://api.xiaomimimo.com");
        config.apiKey = ai.value("apiKey", std::string());
        config.model = ai.value("model", "mimo-v2.5");
        config.cacheDir = ai.value("cacheDir", std::string());
        config.autoAnalyze = ai.value("autoAnalyze", false);
        config.analysisDetailLevel = std::clamp(ai.value("analysisDetailLevel", 1), 0, 2);

        if (config.provider.empty()) {
            std::string lowerBaseUrl = config.baseUrl;
            std::string lowerModel = config.model;
            std::transform(lowerBaseUrl.begin(), lowerBaseUrl.end(), lowerBaseUrl.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            std::transform(lowerModel.begin(), lowerModel.end(), lowerModel.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            config.provider =
                (lowerBaseUrl.find("generativelanguage.googleapis.com") != std::string::npos ||
                 lowerModel.find("gemini") != std::string::npos) ? "gemini" : "mimo";
        }

        config.provider = normalizeAIProvider(config.provider);

        if (ai.contains("providers") && ai["providers"].is_object()) {
            for (const auto& entry : ai["providers"].items()) {
                if (!entry.value().is_object()) {
                    continue;
                }
                std::string providerId = normalizeAIProvider(entry.key());
                AIProviderConfig providerConfig;
                providerConfig.baseUrl = entry.value().value("baseUrl", std::string());
                providerConfig.apiKey = entry.value().value("apiKey", std::string());
                providerConfig.model = entry.value().value("model", std::string());
                normalizeAIProviderConfig(providerId, providerConfig);
                config.providers[providerId] = providerConfig;
            }
        }
    }

    AIProviderConfig legacyActive = {
        config.baseUrl,
        config.apiKey,
        config.model
    };
    normalizeAIProviderConfig(config.provider, legacyActive);
    if (config.providers.find(config.provider) == config.providers.end()) {
        config.providers[config.provider] = legacyActive;
    }
    if (config.providers.find("mimo") == config.providers.end()) {
        config.providers["mimo"] = defaultAIProviderConfig("mimo");
    }
    if (config.providers.find("gemini") == config.providers.end()) {
        config.providers["gemini"] = defaultAIProviderConfig("gemini");
    }

    auto activeIt = config.providers.find(config.provider);
    if (activeIt != config.providers.end()) {
        config.baseUrl = activeIt->second.baseUrl;
        config.apiKey = activeIt->second.apiKey;
        config.model = activeIt->second.model;
    }
    return config;
}

// 日志配置
void Settings::setLogConfig(const LogConfig& config) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_config["log"] = {
            {"enabled", config.enabled},
            {"consoleOutput", config.consoleOutput},
            {"level", config.level},
            {"modules", config.moduleLevels},
            {"filePath", config.filePath},
            {"maxFileSize", config.maxFileSize},
            {"maxFiles", config.maxFiles},
            {"flushOnWarning", config.flushOnWarning}
        };
    }
    save();
}

LogConfig Settings::logConfig() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    LogConfig config;
    if (m_config.contains("log")) {
        const auto& log = m_config["log"];
        config.enabled = log.value("enabled", true);
        config.consoleOutput = log.value("consoleOutput", true);
        config.level = log.value("level", "info");
        config.moduleLevels.clear();
        if (log.contains("modules") && log["modules"].is_object()) {
            for (const auto& [module, level] : log["modules"].items()) {
                if (level.is_string()) {
                    config.moduleLevels[module] = level.get<std::string>();
                }
            }
        }
        config.filePath = log.value("filePath", std::string());
        config.maxFileSize = log.value("maxFileSize", static_cast<size_t>(5 * 1024 * 1024));
        config.maxFiles = log.value("maxFiles", static_cast<size_t>(3));
        config.flushOnWarning = log.value("flushOnWarning", true);
    }
    return config;
}

} // namespace VideoPlay
