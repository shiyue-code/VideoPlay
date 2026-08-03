#ifndef MIMO_CLIENT_H
#define MIMO_CLIENT_H

#include "core/common.h"
#include "core/settings.h"
#include <string>
#include <functional>

namespace VideoPlay {

class AIAnalyzer;

class MimoClient {
public:
    using ProgressCallback = std::function<void(float, const std::string&)>;
    using ErrorCallback = std::function<void(const std::string&)>;

    explicit MimoClient(AIAnalyzer* analyzer);

    AIAnalysisResult analyzeWithMimoVideoUnderstanding(const std::string& videoPath,
                                                       const AIConfig& config,
                                                       ProgressCallback onProgress);
    std::string askMimoVideoDirect(const std::string& videoPath,
                                   const std::string& question,
                                   const AIConfig& config,
                                   ProgressCallback onProgress,
                                   ErrorCallback onError);

private:
    AIAnalyzer* m_analyzer;
};

} // namespace VideoPlay

#endif // MIMO_CLIENT_H
