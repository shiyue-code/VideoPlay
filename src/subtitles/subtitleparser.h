#ifndef SUBTITLEPARSER_H
#define SUBTITLEPARSER_H

#include <string>
#include <vector>
#include <cstdint>

namespace VideoPlay {

struct SubtitleEntry {
    int64_t startTime = 0;  // 毫秒
    int64_t endTime = 0;    // 毫秒
    std::string text;
};

class SubtitleParser {
public:
    SubtitleParser();
    ~SubtitleParser();

    bool loadFile(const std::string& filePath);
    std::string subtitleAt(int64_t ms) const;
    std::vector<SubtitleEntry> entries() const;
    void clear();
    bool isLoaded() const;

private:
    bool parseSRT(const std::string& content);
    bool parseASS(const std::string& content);
    bool parseVTT(const std::string& content);
    int64_t parseTimecode(const std::string& timecode);
    std::string removeFormatting(const std::string& text);
    std::string trim(const std::string& str);

    std::vector<SubtitleEntry> m_entries;
    std::string m_filePath;
    bool m_loaded;
};

} // namespace VideoPlay

#endif // SUBTITLEPARSER_H
