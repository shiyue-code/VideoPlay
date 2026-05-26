#include "ai/searchengine.h"
#include "utils/logger.h"
#include <algorithm>
#include <sstream>
#include <cctype>

namespace VideoPlay {

namespace {
Logger& logger() {
    static auto logger = Logger::get("search");
    return *logger;
}
}


SearchEngine::SearchEngine() = default;
SearchEngine::~SearchEngine() = default;

void SearchEngine::buildIndex(const std::string& videoPath,
                               const std::vector<TranscriptSegment>& transcript,
                               const std::vector<ChapterInfo>& chapters) {
    std::unique_lock<std::shared_mutex> lock(m_mutex);
    m_entries.clear();
    m_invertedIndex.clear();
    m_videoPath = videoPath;

    for (const auto& seg : transcript) {
        IndexEntry entry;
        entry.timestamp = seg.startTime;
        entry.text = seg.text;
        entry.source = 0;

        size_t startIdx = (seg.startTime > 5000) ? seg.startTime - 5000 : 0;
        entry.context = "[" + formatTime(seg.startTime) + "] " + seg.text;

        m_entries.push_back(entry);
    }

    for (const auto& chapter : chapters) {
        IndexEntry entry;
        entry.timestamp = chapter.startTime;
        entry.text = chapter.title;
        entry.source = 2;
        entry.context = "章节: " + chapter.title + " [" + formatTime(chapter.startTime) + "]";
        m_entries.push_back(entry);
    }

    for (size_t i = 0; i < m_entries.size(); i++) {
        auto tokens = tokenize(m_entries[i].text);
        for (const auto& token : tokens) {
            m_invertedIndex[token].push_back(i);
        }
    }

    m_hasIndex = true;
    logger().info("[Search] Index built: " + std::to_string(m_entries.size()) + " entries");
}

void SearchEngine::addSubtitleEntries(const std::vector<SubtitleEntry>& entries) {
    std::unique_lock<std::shared_mutex> lock(m_mutex);

    for (const auto& sub : entries) {
        IndexEntry entry;
        entry.timestamp = sub.startTime;
        entry.text = sub.text;
        entry.source = 1;
        entry.context = "[" + formatTime(sub.startTime) + "] " + sub.text;
        m_entries.push_back(entry);
    }

    for (size_t i = 0; i < m_entries.size(); i++) {
        if (m_entries[i].source == 1) {
            auto tokens = tokenize(m_entries[i].text);
            for (const auto& token : tokens) {
                m_invertedIndex[token].push_back(i);
            }
        }
    }

    logger().info("[Search] Added " + std::to_string(entries.size()) + " subtitle entries");
}

void SearchEngine::clearIndex() {
    std::unique_lock<std::shared_mutex> lock(m_mutex);
    m_entries.clear();
    m_invertedIndex.clear();
    m_hasIndex = false;
}

bool SearchEngine::hasIndex() const {
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    return m_hasIndex;
}

std::vector<std::string> SearchEngine::tokenize(const std::string& text) const {
    std::vector<std::string> tokens;
    std::string lower = toLower(text);

    std::string current;
    for (size_t i = 0; i < lower.length(); ) {
        unsigned char c = static_cast<unsigned char>(lower[i]);
        if (c < 0x80) { // ASCII
            if (std::isalnum(c)) {
                current += static_cast<char>(c);
            } else {
                if (!current.empty()) {
                    tokens.push_back(current);
                    current.clear();
                }
            }
            i++;
        } else {
            if (!current.empty()) {
                tokens.push_back(current);
                current.clear();
            }
            // UTF-8 decoding length
            size_t seqLen = 1;
            if ((c & 0xE0) == 0xC0) seqLen = 2;
            else if ((c & 0xF0) == 0xE0) seqLen = 3;
            else if ((c & 0xF8) == 0xF0) seqLen = 4;

            if (i + seqLen <= lower.length()) {
                tokens.push_back(lower.substr(i, seqLen));
                i += seqLen;
            } else {
                i++; // Invalid sequence
            }
        }
    }
    if (!current.empty()) {
        tokens.push_back(current);
    }
    return tokens;
}

std::string SearchEngine::toLower(const std::string& str) const {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return result;
}

float SearchEngine::calculateRelevance(const std::string& query, const std::string& text) const {
    std::string lowerQuery = toLower(query);
    std::string lowerText = toLower(text);

    if (lowerText.find(lowerQuery) != std::string::npos) {
        return 1.0f;
    }

    auto queryTokens = tokenize(lowerQuery);
    auto textTokens = tokenize(lowerText);

    int matchCount = 0;
    for (const auto& qt : queryTokens) {
        for (const auto& tt : textTokens) {
            if (tt.find(qt) != std::string::npos || qt.find(tt) != std::string::npos) {
                matchCount++;
                break;
            }
        }
    }

    if (queryTokens.empty()) return 0.0f;
    return static_cast<float>(matchCount) / queryTokens.size();
}

std::vector<SearchResult> SearchEngine::search(const std::string& query, int maxResults) const {
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    std::vector<SearchResult> results;

    if (!m_hasIndex || query.empty()) {
        return results;
    }

    std::string lowerQuery = toLower(query);
    auto queryTokens = tokenize(lowerQuery);

    std::unordered_map<size_t, float> scoreMap;

    for (const auto& token : queryTokens) {
        auto it = m_invertedIndex.find(token);
        if (it != m_invertedIndex.end()) {
            for (size_t idx : it->second) {
                scoreMap[idx] += 1.0f;
            }
        }

        for (const auto& [key, indices] : m_invertedIndex) {
            if (key.find(token) != std::string::npos || token.find(key) != std::string::npos) {
                for (size_t idx : indices) {
                    scoreMap[idx] += 0.5f;
                }
            }
        }
    }

    for (const auto& [idx, score] : scoreMap) {
        if (idx < m_entries.size()) {
            const auto& entry = m_entries[idx];
            float relevance = calculateRelevance(query, entry.text);

            if (relevance > 0.1f) {
                SearchResult result;
                result.timestamp = entry.timestamp;
                result.text = entry.text;
                result.context = entry.context;
                result.relevance = score * relevance;
                result.source = entry.source;
                results.push_back(result);
            }
        }
    }

    std::sort(results.begin(), results.end(),
              [](const SearchResult& a, const SearchResult& b) {
                  return a.relevance > b.relevance;
              });

    if (results.size() > static_cast<size_t>(maxResults)) {
        results.resize(maxResults);
    }

    return results;
}

} // namespace VideoPlay
