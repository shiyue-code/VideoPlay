#include "subtitles/subtitleparser.h"
#include "utils/logger.h"
#include "utils/string_utils.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <regex>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__has_include)
#if __has_include(<iconv.h>)
#include <iconv.h>
#define VIDEOPLAY_HAVE_ICONV 1
#endif
#endif

namespace VideoPlay {

namespace {
Logger& logger() {
    static auto instance = Logger::get("subtitle");
    return *instance;
}

bool isValidUtf8(const std::string& s) {
    const auto* p = reinterpret_cast<const unsigned char*>(s.data());
    size_t n = s.size();
    for (size_t i = 0; i < n; ) {
        unsigned char c = p[i];
        if (c < 0x80) { ++i; continue; }
        int need;
        uint32_t cp;
        if ((c & 0xE0) == 0xC0) { need = 1; cp = c & 0x1F; if (cp < 2) return false; }
        else if ((c & 0xF0) == 0xE0) { need = 2; cp = c & 0x0F; }
        else if ((c & 0xF8) == 0xF0) { need = 3; cp = c & 0x07; }
        else return false;
        if (i + static_cast<size_t>(need) >= n) return false;
        for (int k = 1; k <= need; ++k) {
            if ((p[i + k] & 0xC0) != 0x80) return false;
            cp = (cp << 6) | (p[i + k] & 0x3F);
        }
        if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return false;
        if ((need == 2 && cp < 0x800) || (need == 3 && cp < 0x10000)) return false;
        i += need + 1;
    }
    return true;
}

void appendUtf8(std::string& out, uint32_t cp) {
    if (cp < 0x80) out.push_back(static_cast<char>(cp));
    else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

std::string utf16ToUtf8(const char* data, size_t size, bool bigEndian) {
    std::string out;
    out.reserve(size);
    size_t i = 0;
    while (i + 1 < size) {
        uint16_t w = bigEndian
            ? static_cast<uint16_t>((static_cast<unsigned char>(data[i]) << 8) |
                                    static_cast<unsigned char>(data[i + 1]))
            : static_cast<uint16_t>(static_cast<unsigned char>(data[i]) |
                                    (static_cast<unsigned char>(data[i + 1]) << 8));
        i += 2;
        uint32_t cp = w;
        if (w >= 0xD800 && w <= 0xDBFF && i + 1 < size) {
            uint16_t lo = bigEndian
                ? static_cast<uint16_t>((static_cast<unsigned char>(data[i]) << 8) |
                                        static_cast<unsigned char>(data[i + 1]))
                : static_cast<uint16_t>(static_cast<unsigned char>(data[i]) |
                                        (static_cast<unsigned char>(data[i + 1]) << 8));
            if (lo >= 0xDC00 && lo <= 0xDFFF) {
                cp = 0x10000u + ((w - 0xD800u) << 10) + (lo - 0xDC00u);
                i += 2;
            }
        }
        appendUtf8(out, cp);
    }
    return out;
}

std::string gbkToUtf8(const std::string& gbk) {
#if defined(_WIN32)
    int wideLen = MultiByteToWideChar(936 /*CP_GBK*/, 0, gbk.data(),
                                      static_cast<int>(gbk.size()), nullptr, 0);
    if (wideLen <= 0) return {};
    std::vector<wchar_t> wide(static_cast<size_t>(wideLen));
    MultiByteToWideChar(936, 0, gbk.data(), static_cast<int>(gbk.size()),
                        wide.data(), wideLen);
    int utf8Len = WideCharToMultiByte(CP_UTF8, 0, wide.data(), wideLen,
                                      nullptr, 0, nullptr, nullptr);
    if (utf8Len <= 0) return {};
    std::string utf8(static_cast<size_t>(utf8Len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), wideLen,
                        utf8.data(), utf8Len, nullptr, nullptr);
    return utf8;
#elif defined(VIDEOPLAY_HAVE_ICONV)
    iconv_t cd = iconv_open("UTF-8", "GB18030");
    if (cd == reinterpret_cast<iconv_t>(-1)) return {};
    size_t inLeft = gbk.size();
    size_t outCap = gbk.size() * 3 + 16;
    std::string out(outCap, '\0');
    char* inBuf = const_cast<char*>(gbk.data());
    char* outBuf = out.data();
    size_t outLeft = outCap;
    if (iconv(cd, &inBuf, &inLeft, &outBuf, &outLeft) == static_cast<size_t>(-1)) {
        iconv_close(cd);
        return {};
    }
    iconv_close(cd);
    out.resize(outCap - outLeft);
    return out;
#else
    return {};
#endif
}

// 把字幕文件原始字节转成 UTF-8 文本：UTF-8 BOM / UTF-16(带或不带 BOM) / GBK
std::string decodeToUtf8(std::string content) {
    if (content.empty()) return content;
    const auto* p = reinterpret_cast<const unsigned char*>(content.data());

    if (content.size() >= 3 && p[0] == 0xEF && p[1] == 0xBB && p[2] == 0xBF) {
        return content.substr(3); // UTF-8 BOM
    }
    if (content.size() >= 2 && p[0] == 0xFF && p[1] == 0xFE) {
        return utf16ToUtf8(content.data() + 2, content.size() - 2, false);
    }
    if (content.size() >= 2 && p[0] == 0xFE && p[1] == 0xFF) {
        return utf16ToUtf8(content.data() + 2, content.size() - 2, true);
    }
    // 无 BOM UTF-16：按 NUL 字节的奇偶分布判断
    if (content.size() >= 8) {
        size_t evenZero = 0, oddZero = 0;
        size_t probe = std::min<size_t>(content.size(), 512) & ~size_t(1);
        for (size_t i = 0; i < probe; i += 2) {
            if (p[i] == 0) ++evenZero;
            if (p[i + 1] == 0) ++oddZero;
        }
        if (oddZero > evenZero * 4 && oddZero > probe / 8) {
            return utf16ToUtf8(content.data(), content.size(), false);
        }
        if (evenZero > oddZero * 4 && evenZero > probe / 8) {
            return utf16ToUtf8(content.data(), content.size(), true);
        }
    }
    if (isValidUtf8(content)) return content;

    std::string converted = gbkToUtf8(content);
    if (!converted.empty()) {
        logger().info("Subtitle decoded as GBK/GB18030");
        return converted;
    }
    logger().warning("Subtitle encoding not recognized, using raw bytes");
    return content;
}
}

// 字幕解析器通常不涉及初始 STDOUT/STDERR，但若日志初始化失败，
// 使用 root logger 可能更安全；这里保持模块级 logger


SubtitleParser::SubtitleParser()
    : m_loaded(false), m_offset(0) {
}

SubtitleParser::~SubtitleParser() {
}

bool SubtitleParser::loadFile(const std::string& filePath) {
    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
        logger().warning("Cannot open subtitle file: " + filePath);
        return false;
    }
    
    // 读取文件内容并统一转成 UTF-8（UTF-8 BOM / UTF-16 / GBK）
    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
    file.close();

    content = decodeToUtf8(std::move(content));

    // 根据扩展名选择解析器
    std::string ext;
    size_t dotPos = filePath.find_last_of('.');
    if (dotPos != std::string::npos) {
        ext = filePath.substr(dotPos + 1);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    }
    
    bool success = false;
    if (ext == "srt") {
        success = parseSRT(content);
    } else if (ext == "ass" || ext == "ssa") {
        success = parseASS(content);
    } else if (ext == "vtt") {
        success = parseVTT(content);
    } else {
        // 尝试自动检测格式
        if (content.find("WEBVTT") != std::string::npos) {
            success = parseVTT(content);
        } else if (content.find("[Script Info]") != std::string::npos ||
                   content.find("[Events]") != std::string::npos) {
            success = parseASS(content);
        } else {
            success = parseSRT(content);
        }
    }

    if (!success) {
        logger().warning("All subtitle parsers failed for: " + filePath);
    }
    
    if (success) {
        // subtitleAt 用二分查找，要求条目按开始时间有序（ASS 事件顺序不保证）
        std::sort(m_entries.begin(), m_entries.end(),
                  [](const SubtitleEntry& a, const SubtitleEntry& b) {
                      return a.startTime < b.startTime;
                  });
        m_filePath = filePath;
        m_loaded = true;
        logger().info("Loaded subtitle: " + filePath +
                               " (" + std::to_string(m_entries.size()) + " entries)");
    }

    return success;
}

std::string SubtitleParser::subtitleAt(int64_t ms) const {
    if (m_entries.empty()) return "";

    int64_t adjustedMs = ms - m_offset;

    // 二分定位最后一个 startTime <= adjustedMs 的条目，再向前回看覆盖重叠对白。
    // 回看上限 64 条：正常字幕重叠极少，病态长字幕不至于拖慢每帧调用
    auto it = std::upper_bound(m_entries.begin(), m_entries.end(), adjustedMs,
        [](int64_t time, const SubtitleEntry& entry) {
            return time < entry.startTime;
        });

    std::string result;
    int scanned = 0;
    while (it != m_entries.begin() && scanned++ < 64) {
        --it;
        if (adjustedMs >= it->startTime && adjustedMs <= it->endTime) {
            result = result.empty() ? it->text : (it->text + "\n" + result);
        }
    }

    return result;
}

std::vector<SubtitleEntry> SubtitleParser::entries() const {
    return m_entries;
}

void SubtitleParser::clear() {
    m_entries.clear();
    m_filePath.clear();
    m_loaded = false;
}

bool SubtitleParser::isLoaded() const {
    return m_loaded;
}

void SubtitleParser::setOffset(int64_t ms) {
    m_offset = ms;
}

int64_t SubtitleParser::offset() const {
    return m_offset;
}

void SubtitleParser::adjustOffset(int64_t deltaMs) {
    m_offset += deltaMs;
}

std::string SubtitleParser::removeFormatting(const std::string& text) {
    // 移除 ASS/SSA 标签 {\...}
    std::string result = std::regex_replace(text, std::regex("\\{[^}]*\\}"), "");
    
    // 移除 HTML 标签 <...>
    result = std::regex_replace(result, std::regex("<[^>]*>"), "");
    
    // 替换常见的 HTML 实体
    struct Entity {
        const char* entity;
        char replacement;
    };
    static const Entity entities[] = {
        {"&amp;", '&'},
        {"&lt;", '<'},
        {"&gt;", '>'},
        {"&quot;", '\"'},
        {"&#39;", '\''},
        {"&nbsp;", ' '},
    };
    
    for (const auto& e : entities) {
        size_t pos = 0;
        while ((pos = result.find(e.entity, pos)) != std::string::npos) {
            result.replace(pos, strlen(e.entity), 1, e.replacement);
            ++pos;
        }
    }
    
    return result;
}

std::string SubtitleParser::extractAssDialogueText(const std::string& line) {
    // 标准 Dialogue: Layer,Start,End,Style,Name,MarginL,MarginR,MarginV,Effect,Text
    // （Text 前共 9 个逗号）；MKV 内封包可能缺 Layer 前缀字段（8 个逗号）。
    // 识别方式：第 2、3 个字段若形如时间码（H:MM:SS.CC）则视为标准布局。
    std::string s = trim(line);
    if (s.size() >= 9 &&
        (s.compare(0, 9, "Dialogue:") == 0 || s.compare(0, 9, "dialogue:") == 0)) {
        s = trim(s.substr(9));
    }

    // 定位第 8 和第 9 个逗号
    size_t comma8 = std::string::npos, comma9 = std::string::npos, pos = 0;
    int commas = 0;
    while (commas < 9 && (pos = s.find(',', pos)) != std::string::npos) {
        ++commas;
        ++pos;
        if (commas == 8) comma8 = pos;
        else if (commas == 9) comma9 = pos;
    }
    if (comma8 == std::string::npos) {
        return s; // 不足 8 个逗号，视为纯文本
    }

    auto looksLikeTimecode = [](const std::string& field) {
        std::string f = trim(field);
        return f.size() >= 7 && f.find(':') != std::string::npos &&
               std::isdigit(static_cast<unsigned char>(f[0]));
    };

    // 标准布局：fields[1]/fields[2] 为 Start/End 时间码
    size_t c1 = s.find(',');
    if (comma9 != std::string::npos && c1 != std::string::npos) {
        size_t c2 = s.find(',', c1 + 1);
        if (c2 != std::string::npos &&
            looksLikeTimecode(s.substr(c1 + 1, c2 - c1 - 1))) {
            return trim(s.substr(comma9));
        }
    }
    return trim(s.substr(comma8));
}

std::string SubtitleParser::embeddedTextToPlain(const std::string& raw) {
    std::string text = removeFormatting(extractAssDialogueText(raw));
    // \N / \n 转换行
    for (size_t pos = 0; (pos = text.find("\\N", pos)) != std::string::npos; ) {
        text.replace(pos, 2, "\n");
        ++pos;
    }
    for (size_t pos = 0; (pos = text.find("\\n", pos)) != std::string::npos; ) {
        text.replace(pos, 2, "\n");
        ++pos;
    }
    return trim(text);
}

int64_t SubtitleParser::parseTimecode(const std::string& timecode) {
    // 支持格式: HH:MM:SS,mmm / HH:MM:SS.mmm / HH:MM:SS.cc (ASS 百分之一秒)
    int hours = 0, minutes = 0, seconds = 0, millis = 0;

    std::string tc = trim(timecode);

    // 替换点号为逗号以统一处理
    std::replace(tc.begin(), tc.end(), '.', ',');

    // 解析 HH:MM:SS,mmm
    if (sscanf(tc.c_str(), "%d:%d:%d,%d", &hours, &minutes, &seconds, &millis) == 4) {
        // 根据小数位数调整毫秒（ASS 使用 2 位百分之一秒）
        size_t commaPos = tc.find(',');
        if (commaPos != std::string::npos && commaPos + 1 < tc.size()) {
            int fracDigits = static_cast<int>(tc.size() - commaPos - 1);
            if (fracDigits == 2) millis *= 10;       // centiseconds -> milliseconds
            else if (fracDigits == 1) millis *= 100; // deciseconds -> milliseconds
        }
        return (hours * 3600LL + minutes * 60LL + seconds) * 1000 + millis;
    }

    // 解析 MM:SS,mmm
    if (sscanf(tc.c_str(), "%d:%d,%d", &minutes, &seconds, &millis) == 3) {
        size_t commaPos = tc.find(',');
        if (commaPos != std::string::npos && commaPos + 1 < tc.size()) {
            int fracDigits = static_cast<int>(tc.size() - commaPos - 1);
            if (fracDigits == 2) millis *= 10;
            else if (fracDigits == 1) millis *= 100;
        }
        return (minutes * 60LL + seconds) * 1000 + millis;
    }

    return 0;
}

bool SubtitleParser::parseSRT(const std::string& content) {
    m_entries.clear();
    
    std::istringstream stream(content);
    std::string line;
    
    while (std::getline(stream, line)) {
        line = trim(line);
        
        // 跳过空行和序号
        if (line.empty() || std::all_of(line.begin(), line.end(), [](unsigned char c) { return std::isdigit(c); })) {
            continue;
        }
        
        // 查找时间码行 (格式: 00:00:00,000 --> 00:00:00,000)
        size_t arrowPos = line.find(" --> ");
        if (arrowPos != std::string::npos) {
            SubtitleEntry entry;
            entry.startTime = parseTimecode(line.substr(0, arrowPos));
            entry.endTime = parseTimecode(line.substr(arrowPos + 5));
            
            // 读取字幕文本（可能跨越多行）
            std::string text;
            while (std::getline(stream, line)) {
                line = trim(line);
                if (line.empty()) break;
                
                // 检查是否是下一个字幕的序号
                if (std::all_of(line.begin(), line.end(), [](unsigned char c) { return std::isdigit(c); })) {
                    // 将行放回？不，直接处理下一个
                    break;
                }
                
                if (!text.empty()) {
                    text += "\n";
                }
                text += line;
            }
            
            entry.text = removeFormatting(text);
            if (!entry.text.empty()) {
                m_entries.push_back(entry);
            }
        }
    }
    
    return !m_entries.empty();
}

bool SubtitleParser::parseASS(const std::string& content) {
    m_entries.clear();

    std::istringstream stream(content);
    std::string line;
    bool inEvents = false;
    int formatStartIdx = -1, formatEndIdx = -1, formatTextIdx = -1;
    int formatFieldCount = 0;

    logger().debug("[ASS] Starting parse, content size=" + std::to_string(content.size()));

    while (std::getline(stream, line)) {
        line = trim(line);

        // 查找 Events 部分
        if (line == "[Events]") {
            inEvents = true;
            logger().debug("[ASS] Found [Events]");
            continue;
        }

        // 其他 section 开始则退出 Events
        if (inEvents && !line.empty() && line.front() == '[' && line.back() == ']') {
            break;
        }

        if (!inEvents) continue;

        // 检查是否是 Format 行（大小写不敏感）
        if (line.find("Format:") == 0 || line.find("format:") == 0) {
            size_t colonPos = line.find(':');
            std::string format = line.substr(colonPos + 1);
            std::istringstream fmtStream(format);
            std::string field;
            int idx = 0;
            formatFieldCount = 0;

            while (std::getline(fmtStream, field, ',')) {
                field = trim(field);
                if (field == "Start") formatStartIdx = idx;
                else if (field == "End") formatEndIdx = idx;
                else if (field == "Text") formatTextIdx = idx;
                idx++;
                formatFieldCount++;
            }
            logger().debug("[ASS] Format parsed: Start=" + std::to_string(formatStartIdx) +
                                     " End=" + std::to_string(formatEndIdx) +
                                     " Text=" + std::to_string(formatTextIdx) +
                                     " Count=" + std::to_string(formatFieldCount));
            continue;
        }

        // 解析 Dialogue 行（大小写不敏感）
        if (line.find("Dialogue:") == 0 || line.find("dialogue:") == 0) {
            size_t colonPos = line.find(':');
            std::string dialogue = line.substr(colonPos + 1);

            // 如果 Format 中没有 Text 字段，默认 Text 是最后一个字段
            int targetTextIdx = formatTextIdx;
            if (targetTextIdx < 0 && formatFieldCount > 0) {
                targetTextIdx = formatFieldCount - 1;
            }
            // 如果仍然无法确定，尝试用 Start/End 推断
            if (targetTextIdx < 0 && formatStartIdx >= 0 && formatEndIdx >= 0) {
                targetTextIdx = std::max(formatStartIdx, formatEndIdx) + 1;
            }
            if (targetTextIdx < 0) {
                continue; // 无法确定格式，跳过
            }

            std::vector<std::string> fields;
            std::string remaining = dialogue;

            // 提取前 targetTextIdx 个字段（按逗号分割）
            for (int i = 0; i < targetTextIdx; ++i) {
                size_t pos = remaining.find(',');
                if (pos == std::string::npos) {
                    fields.push_back(trim(remaining));
                    remaining.clear();
                    break;
                }
                fields.push_back(trim(remaining.substr(0, pos)));
                remaining = remaining.substr(pos + 1);
            }

            // 剩余部分是 Text 字段（可能包含逗号）
            if (!remaining.empty() || fields.size() == static_cast<size_t>(targetTextIdx)) {
                fields.push_back(trim(remaining));
            }

            int fStart = formatStartIdx >= 0 ? formatStartIdx : 1;
            int fEnd   = formatEndIdx   >= 0 ? formatEndIdx   : 2;

            if (fStart < (int)fields.size() &&
                fEnd < (int)fields.size() &&
                targetTextIdx < (int)fields.size()) {

                SubtitleEntry entry;
                entry.startTime = parseTimecode(fields[fStart]);
                entry.endTime = parseTimecode(fields[fEnd]);
                entry.text = removeFormatting(fields[targetTextIdx]);

                // 替换 \N 和 \n 为换行
                size_t pos = 0;
                while ((pos = entry.text.find("\\N", pos)) != std::string::npos) {
                    entry.text.replace(pos, 2, "\n");
                    pos++;
                }
                pos = 0;
                while ((pos = entry.text.find("\\n", pos)) != std::string::npos) {
                    entry.text.replace(pos, 2, "\n");
                    pos++;
                }

                if (!entry.text.empty()) {
                    m_entries.push_back(entry);
                }
            }
        }
    }

    logger().debug("[ASS] Parse complete, entries=" + std::to_string(m_entries.size()));
    return !m_entries.empty();
}

bool SubtitleParser::parseVTT(const std::string& content) {
    m_entries.clear();
    
    std::istringstream stream(content);
    std::string line;
    
    // 跳过 WEBVTT 头部
    while (std::getline(stream, line)) {
        line = trim(line);
        if (line.empty()) break;
        if (line.find("WEBVTT") == 0) continue;
    }
    
    while (std::getline(stream, line)) {
        line = trim(line);
        
        // 跳过空行和 cue 标识符
        if (line.empty()) continue;
        
        // 检查是否是时间码行 (格式: 00:00:00.000 --> 00:00:00.000)
        size_t arrowPos = line.find(" --> ");
        if (arrowPos != std::string::npos) {
            SubtitleEntry entry;
            entry.startTime = parseTimecode(line.substr(0, arrowPos));
            entry.endTime = parseTimecode(line.substr(arrowPos + 5));
            
            // 移除时间码后的设置 (如 positioning)
            size_t spacePos = line.find(' ', arrowPos + 5);
            if (spacePos != std::string::npos) {
                // 有时间码设置，忽略
            }
            
            // 读取字幕文本
            std::string text;
            while (std::getline(stream, line)) {
                line = trim(line);
                if (line.empty()) break;
                
                if (!text.empty()) {
                    text += "\n";
                }
                text += line;
            }
            
            entry.text = removeFormatting(text);
            if (!entry.text.empty()) {
                m_entries.push_back(entry);
            }
        }
    }
    
    return !m_entries.empty();
}

} // namespace VideoPlay
