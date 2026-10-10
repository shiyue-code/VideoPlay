#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace VideoPlay {

inline std::string trim(const std::string& value) {
    auto start = std::find_if_not(value.begin(), value.end(), [](unsigned char c) {
        return std::isspace(c);
    });
    auto end = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char c) {
        return std::isspace(c);
    }).base();
    return start < end ? std::string(start, end) : std::string();
}

inline std::string toLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

inline bool startsWith(const std::string& value, const std::string& prefix) {
    return value.size() >= prefix.size() &&
           std::equal(prefix.begin(), prefix.end(), value.begin());
}

inline bool endsWith(const std::string& value, const std::string& suffix) {
    return value.size() >= suffix.size() &&
           std::equal(suffix.rbegin(), suffix.rend(), value.rbegin());
}

// 解析 "#RRGGBB" 或 "RRGGBB"（大小写均可），失败返回 false
inline bool parseHexColor(const std::string& s, uint8_t& r, uint8_t& g, uint8_t& b) {
    std::string hex = s;
    if (!hex.empty() && hex.front() == '#') {
        hex.erase(hex.begin());
    }
    if (hex.size() != 6) {
        return false;
    }
    const auto hexDigit = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    int v[6];
    for (int i = 0; i < 6; ++i) {
        v[i] = hexDigit(hex[i]);
        if (v[i] < 0) {
            return false;
        }
    }
    r = static_cast<uint8_t>(v[0] * 16 + v[1]);
    g = static_cast<uint8_t>(v[2] * 16 + v[3]);
    b = static_cast<uint8_t>(v[4] * 16 + v[5]);
    return true;
}

// 解析 AI 分析文本里的括号时间戳 "[mm:ss]" / "[hh:mm:ss]"（毫秒）
inline bool parseBracketTimestampMs(const std::string& text, int64_t& timestampMs) {
    int first = 0;
    int second = 0;
    int third = 0;
    int colonCount = static_cast<int>(std::count(text.begin(), text.end(), ':'));

    if (colonCount == 1 && sscanf(text.c_str(), "[%d:%d]", &first, &second) == 2) {
        timestampMs = static_cast<int64_t>(first * 60 + second) * 1000;
        return true;
    }
    if (colonCount == 2 && sscanf(text.c_str(), "[%d:%d:%d]", &first, &second, &third) == 3) {
        timestampMs = static_cast<int64_t>(first * 3600 + second * 60 + third) * 1000;
        return true;
    }
    return false;
}

// 一行文本拆分出的片段：普通文本或 "[mm:ss]" 时间戳
struct TimestampToken {
    std::string text;
    bool isTimestamp = false;
    int64_t ms = 0;
    int width = 0;  // 渲染侧填充的像素宽度（折行时测得）
};

// 把一行文本拆成普通片段与 [mm:ss]/[hh:mm:ss] 时间戳片段；
// 解析失败的 "[...]" 与未闭合的 "[" 都保留为普通文本
inline std::vector<TimestampToken> splitTimestampTokens(const std::string& line) {
    std::vector<TimestampToken> tokens;
    std::string pending;
    auto flushPending = [&]() {
        if (!pending.empty()) {
            tokens.push_back({std::move(pending), false, 0});
            pending.clear();
        }
    };

    size_t pos = 0;
    while (pos < line.size()) {
        size_t start = line.find('[', pos);
        if (start == std::string::npos) {
            pending += line.substr(pos);
            break;
        }
        pending += line.substr(pos, start - pos);
        size_t end = line.find(']', start + 1);
        if (end == std::string::npos) {
            pending += line.substr(start);
            break;
        }
        int64_t ms = 0;
        if (parseBracketTimestampMs(line.substr(start, end - start + 1), ms)) {
            flushPending();
            tokens.push_back({line.substr(start, end - start + 1), true, ms});
            pos = end + 1;
        } else {
            // "[" 当作普通字符，继续找下一个可能的 "["
            pending += line[start];
            pos = start + 1;
        }
    }
    flushPending();
    return tokens;
}

// 解析 "mm:ss" / "hh:mm:ss"（容忍首尾非数字字符与 '*' 标记）
inline bool parseHmsTimestampMs(std::string text, int& valueMs) {
    text.erase(std::remove(text.begin(), text.end(), '*'), text.end());
    auto isDigit = [](char c) { return c >= '0' && c <= '9'; };
    while (!text.empty() && !isDigit(text.front())) {
        text.erase(text.begin());
    }
    while (!text.empty() && !isDigit(text.back())) {
        text.pop_back();
    }

    int h = 0;
    int m = 0;
    int s = 0;
    char tail = '\0';
    if (std::sscanf(text.c_str(), "%d:%d:%d%c", &h, &m, &s, &tail) == 3) {
        valueMs = (h * 3600 + m * 60 + s) * 1000;
        return true;
    }
    if (std::sscanf(text.c_str(), "%d:%d%c", &m, &s, &tail) == 2) {
        valueMs = (m * 60 + s) * 1000;
        return true;
    }
    return false;
}

} // namespace VideoPlay
