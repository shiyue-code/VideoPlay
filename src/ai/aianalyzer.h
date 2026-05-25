#ifndef AIANALYZER_H
#define AIANALYZER_H

#include "core/common.h"
#include "core/settings.h"
#include "ai/httpclient.h"
#include "subtitles/subtitleparser.h"
#include <string>
#include <functional>
#include <atomic>
#include <mutex>
#include <thread>

namespace VideoPlay {

class AIAnalyzer {
public:
    using ProgressCallback = std::function<void(float progress, const std::string& status)>;
    using CompleteCallback = std::function<void(const AIAnalysisResult& result)>;
    using ErrorCallback = std::function<void(const std::string& error)>;

    AIAnalyzer();
    ~AIAnalyzer();

    void configure(const AIConfig& config);
    bool isConfigured() const;

    void analyze(const std::string& videoPath,
                 CompleteCallback onComplete,
                 ProgressCallback onProgress = nullptr,
                 ErrorCallback onError = nullptr);

    void cancel();

    bool hasCache(const std::string& videoPath) const;
    AIAnalysisResult loadCache(const std::string& videoPath) const;

    void clearCache(const std::string& videoPath);
    void clearAllCache();

private:
    HttpClient m_http;
    AIConfig m_config;
    std::atomic<bool> m_cancelled{false};
    mutable std::mutex m_mutex;

    AIConfig snapshotConfig() const;
    std::string extractAudio(const std::string& videoPath, ProgressCallback onProgress);
    std::vector<TranscriptSegment> transcribe(const std::string& audioPath, ProgressCallback onProgress);
    AIAnalysisResult analyzeWithGPT(const std::vector<TranscriptSegment>& transcript,
                                     const std::string& videoPath,
                                     ProgressCallback onProgress);
    AIAnalysisResult analyzeWithMimoVideoUnderstanding(const std::string& videoPath,
                                                       const AIConfig& config,
                                                       ProgressCallback onProgress);
    AIAnalysisResult analyzeWithGeminiVideoUnderstanding(const std::string& videoPath,
                                                         const AIConfig& config,
                                                         ProgressCallback onProgress);
    std::string extractVideoForAI(const std::string& videoPath,
                                  ProgressCallback onProgress,
                                  int64_t maxOutputBytes,
                                  int maxDurationSeconds);
    std::string fileToBase64(const std::string& filePath);
    std::string findSubtitleFile(const std::string& videoPath);
    std::vector<TranscriptSegment> loadSubtitleAsTranscript(const std::string& subtitlePath);

    std::string getCachePath(const std::string& videoPath) const;
    std::string computeFileHash(const std::string& filePath) const;
    void saveCache(const std::string& videoPath, const AIAnalysisResult& result);

    std::string getCacheDir() const;
};

} // namespace VideoPlay

#endif // AIANALYZER_H
