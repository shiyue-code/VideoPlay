#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <string>

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
