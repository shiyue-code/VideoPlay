#ifndef TRANSCRIBER_H
#define TRANSCRIBER_H

#include "core/common.h"
#include <string>
#include <vector>
#include <functional>

namespace VideoPlay {

class AIAnalyzer;

class Transcriber {
public:
    using ProgressCallback = std::function<void(float, const std::string&)>;

    explicit Transcriber(AIAnalyzer* analyzer);

    std::string extractAudio(const std::string& videoPath, ProgressCallback onProgress);
    std::vector<TranscriptSegment> transcribe(const std::string& audioPath, ProgressCallback onProgress);
    std::string extractVideoForAI(const std::string& videoPath,
                                  ProgressCallback onProgress,
                                  int64_t maxOutputBytes,
                                  int maxDurationSeconds);
    std::string fileToBase64(const std::string& filePath);
    std::string findSubtitleFile(const std::string& videoPath);
    std::vector<TranscriptSegment> loadSubtitleAsTranscript(const std::string& subtitlePath);

private:
    AIAnalyzer* m_analyzer;
};

} // namespace VideoPlay

#endif // TRANSCRIBER_H
