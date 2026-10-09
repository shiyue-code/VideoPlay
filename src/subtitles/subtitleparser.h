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

    // 剥离 ASS/SSA {\...} 覆盖标签与 HTML <...> 标签（含 HTML 实体解码）
    static std::string removeFormatting(const std::string& text);
    // 从内封 ASS Dialogue 行中提取 Text 字段；不是 Dialogue 结构时原样返回
    static std::string extractAssDialogueText(const std::string& line);
    // 内封 ASS/SRT 文本转纯文本：提取 Text 字段、剥标签、\N 转换行
    static std::string embeddedTextToPlain(const std::string& raw);

private:
    bool parseSRT(const std::string& content);
    bool parseASS(const std::string& content);
    bool parseVTT(const std::string& content);
    int64_t parseTimecode(const std::string& timecode);

    std::vector<SubtitleEntry> m_entries;
    std::string m_filePath;
    bool m_loaded = false;
    int64_t m_offset = 0;
};

} // namespace VideoPlay

#endif // SUBTITLEPARSER_H
