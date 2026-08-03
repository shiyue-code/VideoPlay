#ifndef CACHE_MANAGER_H
#define CACHE_MANAGER_H

#include "core/common.h"
#include "ai/gemini_client.h"
#include <string>

namespace VideoPlay {

class AIAnalyzer;

class CacheManager {
public:
    explicit CacheManager(AIAnalyzer* analyzer);

    bool hasCache(const std::string& videoPath) const;
    AIAnalysisResult loadCache(const std::string& videoPath) const;
    void saveCache(const std::string& videoPath, const AIAnalysisResult& result);

    void clearCache(const std::string& videoPath);
    void clearAllCache();

    std::string computeFileHash(const std::string& filePath) const;
    std::string computeSourceHash(const std::string& filePath) const;
    std::string getCachePath(const std::string& videoPath) const;
    std::string getCacheDir() const;
    std::string getTranscodeCacheDir() const;
    std::string getTranscodeCachePath(const std::string& sourceHash,
                                      int clipSeconds,
                                      int64_t maxOutputBytes) const;
    std::string findReusableTranscodeCache(const std::string& sourceHash,
                                           int clipSeconds,
                                           int64_t maxOutputBytes) const;
    void clearTranscodeCache(const std::string& videoPath);
    GeminiVideoFile loadGeminiFileCache(const std::string& cachePath) const;
    void saveGeminiFileCache(const std::string& cachePath,
                             const GeminiVideoFile& file) const;
    std::string getGeminiFileCachePath(const std::string& sourceHash,
                                       int clipSeconds,
                                       int64_t maxOutputBytes) const;

private:
    AIAnalyzer* m_analyzer;
};

} // namespace VideoPlay

#endif // CACHE_MANAGER_H
