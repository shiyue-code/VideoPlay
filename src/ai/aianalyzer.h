#ifndef AIANALYZER_H
#define AIANALYZER_H

#include "core/common.h"
#include "core/settings.h"
#include "ai/httpclient.h"
#include <string>
#include <functional>
#include <atomic>
#include <mutex>
#include <thread>
#include <vector>
#include <memory>

namespace VideoPlay {

class Transcriber;
class CacheManager;
class GeminiClient;
class MimoClient;

class AIAnalyzer {
public:
    using ProgressCallback = std::function<void(float, const std::string&)>;
    using CompleteCallback = std::function<void(const AIAnalysisResult&)>;
    using ErrorCallback = std::function<void(const std::string&)>;
    using QuestionCallback = std::function<void(const std::string& answer)>;

    AIAnalyzer();
    ~AIAnalyzer();

    void configure(const AIConfig& config);
    bool isConfigured() const;

    void analyze(const std::string& videoPath,
                 CompleteCallback onComplete,
                 ProgressCallback onProgress = nullptr,
                 ErrorCallback onError = nullptr);

    void askQuestion(const std::string& question, const AIAnalysisResult& context, QuestionCallback onComplete, ErrorCallback onError = nullptr);
    void askVideoDirect(const std::string& videoPath,
                        const std::string& question,
                        QuestionCallback onComplete,
                        ProgressCallback onProgress = nullptr,
                        ErrorCallback onError = nullptr);

    void cancel();

    bool hasCache(const std::string& videoPath) const;
    AIAnalysisResult loadCache(const std::string& videoPath) const;

    void clearCache(const std::string& videoPath);
    void clearAllCache();

private:
    friend class Transcriber;
    friend class CacheManager;
    friend class GeminiClient;
    friend class MimoClient;

    HttpClient m_http;
    AIConfig m_config;
    std::atomic<bool> m_cancelled{false};
    mutable std::mutex m_mutex;
    std::vector<std::thread> m_workers;

    std::unique_ptr<Transcriber> m_transcriber;
    std::unique_ptr<CacheManager> m_cacheManager;
    std::unique_ptr<GeminiClient> m_geminiClient;
    std::unique_ptr<MimoClient> m_mimoClient;

    AIConfig snapshotConfig() const;
};

} // namespace VideoPlay

#endif // AIANALYZER_H
