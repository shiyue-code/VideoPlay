#ifndef GEMINI_CLIENT_H
#define GEMINI_CLIENT_H

#include "core/common.h"
#include "core/settings.h"
#include <string>
#include <functional>
#include <cstdint>

namespace VideoPlay {

class AIAnalyzer;

struct GeminiVideoFile {
    bool valid = false;
    std::string name;
    std::string uri;
    std::string mimeType = "video/mp4";
    int64_t expiresAt = 0;
};

class GeminiClient {
public:
    using ProgressCallback = std::function<void(float, const std::string&)>;
    using ErrorCallback = std::function<void(const std::string&)>;

    explicit GeminiClient(AIAnalyzer* analyzer);

    AIAnalysisResult analyzeWithGeminiVideoUnderstanding(const std::string& videoPath,
                                                         const AIConfig& config,
                                                         ProgressCallback onProgress);
    AIAnalysisResult analyzeWithGPT(const std::vector<TranscriptSegment>& transcript,
                                    const std::string& videoPath,
                                    ProgressCallback onProgress);
    std::string askGeminiVideoDirect(const std::string& videoPath,
                                     const std::string& question,
                                     const AIConfig& config,
                                     ProgressCallback onProgress,
                                     ErrorCallback onError);
    GeminiVideoFile ensureGeminiVideoFile(const std::string& videoPath,
                                          const AIConfig& config,
                                          ProgressCallback onProgress,
                                          ErrorCallback onError);
    GeminiVideoFile uploadGeminiVideoFile(const std::string& mp4Path,
                                          const std::string& cachePath,
                                          const AIConfig& config,
                                          ProgressCallback onProgress,
                                          ErrorCallback onError);
    bool waitForGeminiFileActive(const GeminiVideoFile& file,
                                 const AIConfig& config,
                                 ProgressCallback onProgress);

private:
    AIAnalyzer* m_analyzer;
};

} // namespace VideoPlay

#endif // GEMINI_CLIENT_H
