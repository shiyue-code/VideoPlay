#include "subtitles/subtitleparser.h"
#include "utils/logger.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <regex>

namespace VideoPlay {

SubtitleParser::SubtitleParser()
    : m_loaded(false) {
}

SubtitleParser::~SubtitleParser() {
}

bool SubtitleParser::loadFile(const std::string& filePath) {
    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
        Logger::instance().warning("Cannot open subtitle file: " + filePath);
        return false;
    }
    
    // 读取文件内容
    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
    file.close();
    
    // 检测 BOM 并移除
    if (content.size() >= 3 && 
        static_cast<unsigned char>(content[0]) == 0xEF &&
        static_cast<unsigned char>(content[1]) == 0xBB &&
        static_cast<unsigned char>(content[2]) == 0xBF) {
        content = content.substr(3);
    }
    
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
        } else if (content.find("[Script Info]") != std::string::npos) {
            success = parseASS(content);
        } else {
            success = parseSRT(content);
        }
    }
    
    if (success) {
        m_filePath = filePath;
        m_loaded = true;
        Logger::instance().info("Loaded subtitle: " + filePath + 
                               " (" + std::to_string(m_entries.size()) + " entries)");
    }
    
    return success;
}

std::string SubtitleParser::subtitleAt(int64_t ms) const {
    for (const auto& entry : m_entries) {
        if (ms >= entry.startTime && ms <= entry.endTime) {
            return entry.text;
        }
    }
    return "";
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

std::string SubtitleParser::trim(const std::string& str) {
    auto start = std::find_if_not(str.begin(), str.end(), ::isspace);
    auto end = std::find_if_not(str.rbegin(), str.rend(), ::isspace).base();
    return (start < end) ? std::string(start, end) : "";
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

int64_t SubtitleParser::parseTimecode(const std::string& timecode) {
    // 支持格式: HH:MM:SS,mmm 或 HH:MM:SS.mmm
    int hours = 0, minutes = 0, seconds = 0, millis = 0;
    
    std::string tc = trim(timecode);
    
    // 替换点号为逗号以统一处理
    std::replace(tc.begin(), tc.end(), '.', ',');
    
    // 解析 HH:MM:SS,mmm
    if (sscanf(tc.c_str(), "%d:%d:%d,%d", &hours, &minutes, &seconds, &millis) == 4) {
        return (hours * 3600LL + minutes * 60LL + seconds) * 1000 + millis;
    }
    
    // 解析 MM:SS,mmm
    if (sscanf(tc.c_str(), "%d:%d,%d", &minutes, &seconds, &millis) == 3) {
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
        if (line.empty() || std::all_of(line.begin(), line.end(), ::isdigit)) {
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
                if (std::all_of(line.begin(), line.end(), ::isdigit)) {
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
    
    while (std::getline(stream, line)) {
        line = trim(line);
        
        // 查找 Events 部分
        if (line == "[Events]") {
            inEvents = true;
            continue;
        }
        
        if (!inEvents) continue;
        
        // 检查是否是 Format 行
        if (line.find("Format:") == 0) {
            // 解析格式: Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text
            std::string format = line.substr(7);
            std::istringstream fmtStream(format);
            std::string field;
            int idx = 0;
            
            while (std::getline(fmtStream, field, ',')) {
                field = trim(field);
                if (field == "Start") formatStartIdx = idx;
                else if (field == "End") formatEndIdx = idx;
                else if (field == "Text") formatTextIdx = idx;
                idx++;
            }
            continue;
        }
        
        // 解析 Dialogue 行
        if (line.find("Dialogue:") == 0) {
            std::string dialogue = line.substr(9);
            std::istringstream dlgStream(dialogue);
            std::string field;
            std::vector<std::string> fields;
            
            // 注意：Text 字段可能包含逗号，需要特殊处理
            size_t textStart = 0;
            int commaCount = 0;
            for (size_t i = 0; i < dialogue.size() && commaCount < formatTextIdx; i++) {
                if (dialogue[i] == ',') {
                    commaCount++;
                    if (commaCount == formatTextIdx) {
                        textStart = i + 1;
                    }
                }
            }
            
            // 提取前 N-1 个字段
            std::string prefix = dialogue.substr(0, textStart > 0 ? textStart - 1 : 0);
            std::istringstream prefixStream(prefix);
            while (std::getline(prefixStream, field, ',')) {
                fields.push_back(trim(field));
            }
            
            // Text 字段是剩余部分
            if (textStart > 0 && textStart < dialogue.size()) {
                fields.push_back(trim(dialogue.substr(textStart)));
            }
            
            if (formatStartIdx >= 0 && formatEndIdx >= 0 && 
                formatStartIdx < (int)fields.size() && 
                formatEndIdx < (int)fields.size() &&
                formatTextIdx < (int)fields.size()) {
                
                SubtitleEntry entry;
                entry.startTime = parseTimecode(fields[formatStartIdx]);
                entry.endTime = parseTimecode(fields[formatEndIdx]);
                entry.text = removeFormatting(fields[formatTextIdx]);
                
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
