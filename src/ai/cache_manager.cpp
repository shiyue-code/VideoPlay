#include "ai/cache_manager.h"
#include "ai/aianalyzer.h"
#include "ai/ai_utils.h"
#include <filesystem>
#include <fstream>

namespace VideoPlay {

CacheManager::CacheManager(AIAnalyzer* analyzer) : m_analyzer(analyzer) {}

std::string CacheManager::getCacheDir() const {
    std::lock_guard<std::mutex> lock(m_analyzer->m_mutex);
    if (!m_analyzer->m_config.cacheDir.empty()) {
        return m_analyzer->m_config.cacheDir;
    }
#ifdef _WIN32
    const char* appData = getenv("APPDATA");
    if (appData) {
        return std::string(appData) + "/VideoPlay/ai_cache";
    }
#else
    const char* home = getenv("HOME");
    if (home) {
        return std::string(home) + "/.config/VideoPlay/ai_cache";
    }
#endif
    return "./ai_cache";
}

std::string CacheManager::computeSourceHash(const std::string& filePath) const {
    try {
        auto filePathObj = std::filesystem::u8path(filePath);
        if (!std::filesystem::exists(filePathObj)) return "";

        auto fileSize = std::filesystem::file_size(filePathObj);
        auto modTime = std::filesystem::last_write_time(filePathObj).time_since_epoch().count();

        std::stringstream ss;
        ss << filePath << "_" << fileSize << "_" << modTime
           << "_" << kVideoTranscodeCacheVersion;

        size_t hash = std::hash<std::string>{}(ss.str());

        std::stringstream result;
        result << std::hex << hash;
        return result.str();
    } catch (const std::exception& e) {
        logger().error("[AI] computeSourceHash failed: " + std::string(e.what()));
        return "";
    }
}

std::string CacheManager::computeFileHash(const std::string& filePath) const {
    try {
        auto filePathObj = std::filesystem::u8path(filePath);
        if (!std::filesystem::exists(filePathObj)) return "";

        std::ifstream file(filePathObj, std::ios::binary);
        if (!file.is_open()) return "";

        auto fileSize = std::filesystem::file_size(filePathObj);
        auto modTime = std::filesystem::last_write_time(filePathObj).time_since_epoch().count();

        std::stringstream ss;
        AIConfig config = m_analyzer->snapshotConfig();
        ss << filePath << "_" << fileSize << "_" << modTime
           << "_" << config.provider << "_" << config.model
           << "_" << std::clamp(config.analysisDetailLevel, 0, 2)
           << "_" << kAnalysisCacheVersion;

        std::string input = ss.str();
        size_t hash = std::hash<std::string>{}(input);

        std::stringstream result;
        result << std::hex << hash;
        return result.str();
    } catch (const std::exception& e) {
        logger().error("[AI] computeFileHash failed: " + std::string(e.what()));
        return "";
    }
}

std::string CacheManager::getCachePath(const std::string& videoPath) const {
    std::string hash = computeFileHash(videoPath);
    std::string cacheDir = getCacheDir();
    std::filesystem::create_directories(cacheDir);
    return cacheDir + "/" + hash + ".json";
}

std::string CacheManager::getTranscodeCacheDir() const {
    std::filesystem::path cacheDir = std::filesystem::path(getCacheDir()) / "video";
    std::filesystem::create_directories(cacheDir);
    return cacheDir.u8string();
}

std::string CacheManager::getTranscodeCachePath(const std::string& sourceHash,
                                              int clipSeconds,
                                              int64_t maxOutputBytes) const {
    std::filesystem::path cacheDir = std::filesystem::u8path(getTranscodeCacheDir());
    int64_t limitKb = std::max<int64_t>(1, maxOutputBytes / 1024);
    std::string fileName = sourceHash + "_clip" + std::to_string(clipSeconds) +
                           "_limit" + std::to_string(limitKb) + "kb_ai.mp4";
    return (cacheDir / fileName).u8string();
}

std::string CacheManager::findReusableTranscodeCache(const std::string& sourceHash,
                                                   int clipSeconds,
                                                   int64_t maxOutputBytes) const {
    std::filesystem::path cacheDir = std::filesystem::u8path(getTranscodeCacheDir());
    if (!std::filesystem::exists(cacheDir)) {
        return "";
    }

    std::string prefix = sourceHash + "_clip" + std::to_string(clipSeconds) + "_";
    std::string bestPath;
    uintmax_t bestSize = 0;

    try {
        for (const auto& entry : std::filesystem::directory_iterator(cacheDir)) {
            if (!entry.is_regular_file()) {
                continue;
            }

            std::filesystem::path path = entry.path();
            std::string fileName = path.filename().u8string();
            if (fileName.rfind(prefix, 0) != 0 || path.extension() != ".mp4") {
                continue;
            }

            uintmax_t fileSize = std::filesystem::file_size(path);
            if (fileSize == 0) {
                std::filesystem::remove(path);
                continue;
            }

            if (fileSize <= static_cast<uintmax_t>(maxOutputBytes) && fileSize > bestSize) {
                bestSize = fileSize;
                bestPath = path.u8string();
            }
        }
    } catch (const std::exception& e) {
        logger().warning("[AI] Failed to scan transcode cache: " + std::string(e.what()));
    }

    return bestPath;
}

std::string CacheManager::getGeminiFileCachePath(const std::string& sourceHash,
                                               int clipSeconds,
                                               int64_t maxOutputBytes) const {
    std::filesystem::path cacheDir = std::filesystem::path(getCacheDir()) / "remote";
    std::filesystem::create_directories(cacheDir);

    int64_t limitKb = std::max<int64_t>(1, maxOutputBytes / 1024);
    std::string fileName = "gemini_" + sourceHash + "_clip" +
                           std::to_string(clipSeconds) + "_limit" +
                           std::to_string(limitKb) + "kb_file.json";
    return (cacheDir / fileName).u8string();
}

bool CacheManager::hasCache(const std::string& videoPath) const {
    std::string path = getCachePath(videoPath);
    return std::filesystem::exists(path);
}

AIAnalysisResult CacheManager::loadCache(const std::string& videoPath) const {
    AIAnalysisResult result;
    std::string path = getCachePath(videoPath);

    if (!std::filesystem::exists(path)) {
        return result;
    }

    try {
        std::ifstream file(path);
        nlohmann::json j;
        file >> j;

        result.summary = j.value("summary", std::string());
        result.language = j.value("language", std::string());
        result.analyzedAt = j.value("analyzedAt", int64_t(0));

        if (j.contains("chapters") && j["chapters"].is_array()) {
            for (const auto& ch : j["chapters"]) {
                ChapterInfo chapter;
                chapter.startTime = ch.value("startTime", int64_t(0));
                chapter.endTime = ch.value("endTime", int64_t(0));
                chapter.title = ch.value("title", std::string());
                result.chapters.push_back(chapter);
            }
        }

        if (j.contains("transcript") && j["transcript"].is_array()) {
            for (const auto& seg : j["transcript"]) {
                TranscriptSegment segment;
                segment.startTime = seg.value("startTime", int64_t(0));
                segment.endTime = seg.value("endTime", int64_t(0));
                segment.text = seg.value("text", std::string());
                segment.confidence = seg.value("confidence", 0.0f);
                result.transcript.push_back(segment);
            }
        }

        result.valid = hasUsableAnalysis(result);
        if (result.valid) {
            logger().info("[AI] Loaded cache for: " + videoPath +
                " (" + std::to_string(result.chapters.size()) + " chapters)");
        } else {
            logger().warning("[AI] Ignoring incomplete cache without chapters: " + videoPath);
        }
    } catch (const std::exception& e) {
        logger().error("[AI] Failed to load cache: " + std::string(e.what()));
        result.valid = false;
    }

    return result;
}

void CacheManager::saveCache(const std::string& videoPath, const AIAnalysisResult& result) {
    if (!hasUsableAnalysis(result)) {
        logger().warning("[AI] Skip saving incomplete analysis without chapters: " + videoPath);
        return;
    }

    std::string path = getCachePath(videoPath);

    try {
        nlohmann::json j;
        j["videoPath"] = videoPath;
        j["videoHash"] = computeFileHash(videoPath);
        j["summary"] = result.summary;
        j["language"] = result.language;
        j["analyzedAt"] = result.analyzedAt;

        j["chapters"] = nlohmann::json::array();
        for (const auto& ch : result.chapters) {
            j["chapters"].push_back({
                {"startTime", ch.startTime},
                {"endTime", ch.endTime},
                {"title", ch.title}
            });
        }

        j["transcript"] = nlohmann::json::array();
        for (const auto& seg : result.transcript) {
            j["transcript"].push_back({
                {"startTime", seg.startTime},
                {"endTime", seg.endTime},
                {"text", seg.text},
                {"confidence", seg.confidence}
            });
        }

        std::ofstream file(path);
        file << j.dump(2);

        logger().info("[AI] Saved cache for: " + videoPath);
    } catch (const std::exception& e) {
        logger().error("[AI] Failed to save cache: " + std::string(e.what()));
    }
}

void CacheManager::clearCache(const std::string& videoPath) {
    std::string path = getCachePath(videoPath);
    if (std::filesystem::exists(path)) {
        std::filesystem::remove(path);
        logger().info("[AI] Cleared cache for: " + videoPath);
    }
    clearTranscodeCache(videoPath);
}

void CacheManager::clearAllCache() {
    std::string cacheDir = getCacheDir();
    if (std::filesystem::exists(cacheDir)) {
        std::filesystem::remove_all(cacheDir);
        logger().info("[AI] Cleared all cache");
    }
}

void CacheManager::clearTranscodeCache(const std::string& videoPath) {
    std::string sourceHash = computeSourceHash(videoPath);
    if (sourceHash.empty()) {
        return;
    }

    std::filesystem::path cacheDir = std::filesystem::u8path(getTranscodeCacheDir());
    std::string prefix = sourceHash + "_clip";
    size_t removedCount = 0;

    try {
        if (std::filesystem::exists(cacheDir)) {
            for (const auto& entry : std::filesystem::directory_iterator(cacheDir)) {
                if (!entry.is_regular_file()) {
                    continue;
                }

                std::string fileName = entry.path().filename().u8string();
                if (fileName.rfind(prefix, 0) == 0 && entry.path().extension() == ".mp4") {
                    std::filesystem::remove(entry.path());
                    ++removedCount;
                }
            }
        }

        std::filesystem::path legacyPath = std::filesystem::path(getCacheDir()) /
            (computeFileHash(videoPath) + "_ai.mp4");
        if (std::filesystem::exists(legacyPath)) {
            std::filesystem::remove(legacyPath);
            ++removedCount;
        }

        std::filesystem::path remoteDir = std::filesystem::path(getCacheDir()) / "remote";
        std::string remotePrefix = "gemini_" + sourceHash + "_clip";
        if (std::filesystem::exists(remoteDir)) {
            for (const auto& entry : std::filesystem::directory_iterator(remoteDir)) {
                if (!entry.is_regular_file()) {
                    continue;
                }

                std::string fileName = entry.path().filename().u8string();
                if (fileName.rfind(remotePrefix, 0) == 0 &&
                    entry.path().extension() == ".json") {
                    std::filesystem::remove(entry.path());
                    ++removedCount;
                }
            }
        }

        if (removedCount > 0) {
            logger().info("[AI] Cleared " + std::to_string(removedCount) +
                          " transcode cache file(s) for: " + videoPath);
        }
    } catch (const std::exception& e) {
        logger().warning("[AI] Failed to clear transcode cache: " + std::string(e.what()));
    }
}

GeminiVideoFile CacheManager::loadGeminiFileCache(const std::string& cachePath) const {
    GeminiVideoFile file;
    try {
        if (!std::filesystem::exists(cachePath)) {
            return file;
        }

        std::ifstream input(cachePath);
        nlohmann::json j;
        input >> j;

        file.name = j.value("name", std::string());
        file.uri = j.value("uri", std::string());
        file.mimeType = j.value("mimeType", std::string("video/mp4"));
        file.expiresAt = j.value("expiresAt", int64_t(0));

        int64_t now = static_cast<int64_t>(std::time(nullptr));
        file.valid = !file.name.empty() && !file.uri.empty() && file.expiresAt > now + 300;
    } catch (const std::exception& e) {
        logger().warning("[AI] Failed to load Gemini file cache: " + std::string(e.what()));
    }
    return file;
}

void CacheManager::saveGeminiFileCache(const std::string& cachePath,
                                     const GeminiVideoFile& file) const {
    if (!file.valid || file.name.empty() || file.uri.empty()) {
        return;
    }

    try {
        std::filesystem::create_directories(std::filesystem::path(cachePath).parent_path());
        nlohmann::json j = {
            {"name", file.name},
            {"uri", file.uri},
            {"mimeType", file.mimeType},
            {"expiresAt", file.expiresAt}
        };

        std::ofstream output(cachePath);
        output << j.dump(2);
    } catch (const std::exception& e) {
        logger().warning("[AI] Failed to save Gemini file cache: " + std::string(e.what()));
    }
}


} // namespace VideoPlay
