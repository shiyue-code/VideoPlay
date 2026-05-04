#ifndef SEARCHENGINE_H
#define SEARCHENGINE_H

#include "core/common.h"
#include "subtitles/subtitleparser.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>

namespace VideoPlay {

class SearchEngine {
public:
    SearchEngine();
    ~SearchEngine();

    void buildIndex(const std::string& videoPath,
                    const std::vector<TranscriptSegment>& transcript,
                    const std::vector<ChapterInfo>& chapters = {});

    void addSubtitleEntries(const std::vector<SubtitleEntry>& entries);

    void clearIndex();

    std::vector<SearchResult> search(const std::string& query, int maxResults = 50) const;

    bool hasIndex() const;

private:
    struct IndexEntry {
        int64_t timestamp;
        std::string text;
        std::string context;
        int source; // 0=transcript, 1=subtitle, 2=chapter
    };

    mutable std::mutex m_mutex;
    std::vector<IndexEntry> m_entries;
    std::unordered_map<std::string, std::vector<size_t>> m_invertedIndex;
    std::string m_videoPath;
    bool m_hasIndex = false;

    std::vector<std::string> tokenize(const std::string& text) const;
    std::string toLower(const std::string& str) const;
    float calculateRelevance(const std::string& query, const std::string& text) const;
};

} // namespace VideoPlay

#endif // SEARCHENGINE_H
