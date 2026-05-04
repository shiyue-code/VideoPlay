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

    // 字幕时间同步偏移（毫秒，正数=延后，负数=提前）
    void setOffset(int64_t ms);
    int64_t offset() const;
    void adjustOffset(int64_t deltaMs);

private:
    bool parseSRT(const std::string& content);
    bool parseASS(const std::string& content);
    bool parseVTT(const std::string& content);
    int64_t parseTimecode(const std::string& timecode);
    std::string removeFormatting(const std::string& text);
    std::string trim(const std::string& str);

    std::vector<SubtitleEntry> m_entries;
    std::string m_filePath;
    bool m_loaded = false;
    int64_t m_offset = 0;
};

} // namespace VideoPlay

#endif // SUBTITLEPARSER_H
