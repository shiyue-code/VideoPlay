#include "core/episodedetector.h"
#include "utils/logger.h"

#include <filesystem>
#include <regex>
#include <algorithm>
#include <cctype>

namespace VideoPlay {

namespace {
    const std::vector<std::string> kMediaExtensions = {
        ".mp4", ".mkv", ".avi", ".mov", ".wmv", ".flv", ".webm",
        ".mp3", ".aac", ".wav", ".flac", ".ogg", ".m4v", ".ts"
    };
}

bool EpisodeDetector::isMediaFile(const std::string& path)
{
    std::string ext = std::filesystem::path(path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return std::find(kMediaExtensions.begin(), kMediaExtensions.end(), ext) != kMediaExtensions.end();
}

std::string EpisodeDetector::normalizeSeriesName(const std::string& name)
{
    std::string result;
    for (char c : name)
    {
        if (std::isalnum(static_cast<unsigned char>(c)))
        {
            result += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
    }
    return result;
}

std::optional<std::tuple<std::string, int, int>> EpisodeDetector::parseEpisodeInfo(
    const std::string& filename)
{
    std::string name = std::filesystem::path(filename).stem().string();
    int season = 0;
    int episode = 0;
    std::string seriesName = name;

    // 1. 匹配 S01E02 / s01e02 / S1E02
    {
        std::regex re(R"((.*?)\s*[Ss](\d+)[Ee](\d+)(?:\s+.*)?)", std::regex::ECMAScript);
        std::smatch match;
        if (std::regex_match(name, match, re))
        {
            season = std::stoi(match[2].str());
            episode = std::stoi(match[3].str());
            seriesName = match[1].str();
            return std::make_tuple(seriesName, season, episode);
        }
    }

    // 2. 匹配 1x02 / 01x02
    {
        std::regex re(R"((.*?)\s*(\d+)[Xx](\d+)(?:\s+.*)?)", std::regex::ECMAScript);
        std::smatch match;
        if (std::regex_match(name, match, re))
        {
            season = std::stoi(match[2].str());
            episode = std::stoi(match[3].str());
            seriesName = match[1].str();
            return std::make_tuple(seriesName, season, episode);
        }
    }

    // 3. 匹配 EP02 / ep02 / Ep02
    {
        std::regex re(R"((.*?)\s*[Ee][Pp](\d+)(?:\s+.*)?)", std::regex::ECMAScript);
        std::smatch match;
        if (std::regex_match(name, match, re))
        {
            episode = std::stoi(match[2].str());
            seriesName = match[1].str();
            return std::make_tuple(seriesName, season, episode);
        }
    }

    // 4. 匹配 第02集 / 第2话 / 第02回
    {
        std::regex re(R"((.*?)\s*第\s*(\d+)\s*[集话回](?:\s+.*)?)", std::regex::ECMAScript);
        std::smatch match;
        if (std::regex_match(name, match, re))
        {
            episode = std::stoi(match[2].str());
            seriesName = match[1].str();
            return std::make_tuple(seriesName, season, episode);
        }
    }

    // 5. 匹配 [02] / (02) / 【02】 在开头或中间
    {
        std::regex re(R"((.*?)\s*[\[\(【]\s*(\d+)\s*[\]\)】](?:\s+.*)?)", std::regex::ECMAScript);
        std::smatch match;
        if (std::regex_match(name, match, re))
        {
            episode = std::stoi(match[2].str());
            seriesName = match[1].str();
            return std::make_tuple(seriesName, season, episode);
        }
    }

    // 6. 匹配末尾纯数字 (至少两位数，避免与年份混淆)
    // 要求前面有非数字内容，且数字不在最后4位（避免是年份如 2024）
    {
        std::regex re(R"((.*?)\s+(\d{2,3})(?:\s+.*)?)", std::regex::ECMAScript);
        std::smatch match;
        if (std::regex_match(name, match, re))
        {
            std::string numStr = match[2].str();
            int num = std::stoi(numStr);
            // 排除年份 1900-2030
            if (num < 1900 || num > 2030)
            {
                episode = num;
                seriesName = match[1].str();
                return std::make_tuple(seriesName, season, episode);
            }
        }
    }

    // 7. 匹配 - 02 / _02 分隔符后的数字
    {
        std::regex re(R"((.*?)\s*[\-_]\s*(\d{2,3})(?:\s+.*)?)", std::regex::ECMAScript);
        std::smatch match;
        if (std::regex_match(name, match, re))
        {
            int num = std::stoi(match[2].str());
            if (num < 1900 || num > 2030)
            {
                episode = num;
                seriesName = match[1].str();
                return std::make_tuple(seriesName, season, episode);
            }
        }
    }

    // 8. 匹配末尾 .01 / _01 / -01（录屏、剪辑导出常见格式）
    {
        std::regex re(R"((.*?)\s*[\.\-_]\s*(\d{2,3})\s*$)", std::regex::ECMAScript);
        std::smatch match;
        if (std::regex_match(name, match, re))
        {
            int num = std::stoi(match[2].str());
            if (num < 1900 || num > 2030)
            {
                episode = num;
                seriesName = match[1].str();
                // 清理末尾残留的空格或分隔符
                while (!seriesName.empty() &&
                       (seriesName.back() == ' ' || seriesName.back() == '.' ||
                        seriesName.back() == '-' || seriesName.back() == '_'))
                {
                    seriesName.pop_back();
                }
                return std::make_tuple(seriesName, season, episode);
            }
        }
    }

    return std::nullopt;
}

std::optional<SeriesGroup> EpisodeDetector::detectFromFile(const std::string& filePath)
{
    if (!std::filesystem::exists(filePath))
    {
        return std::nullopt;
    }

    std::filesystem::path path(filePath);
    std::string filename = path.filename().string();

    auto selfInfo = parseEpisodeInfo(filename);
    if (!selfInfo)
    {
        return std::nullopt;
    }

    std::string selfSeriesName = std::get<0>(*selfInfo);
    int selfSeason = std::get<1>(*selfInfo);
    int selfEpisode = std::get<2>(*selfInfo);
    std::string selfNormalized = normalizeSeriesName(selfSeriesName);

    if (selfNormalized.empty())
    {
        return std::nullopt;
    }

    SeriesGroup group;
    group.seriesName = selfSeriesName;
    group.seasonNumber = selfSeason;

    std::filesystem::path parentDir = path.parent_path();
    if (parentDir.empty())
    {
        parentDir = ".";
    }

    try
    {
        for (const auto& entry : std::filesystem::directory_iterator(parentDir))
        {
            if (!entry.is_regular_file())
            {
                continue;
            }

            std::string otherPath = entry.path().string();
            if (!isMediaFile(otherPath))
            {
                continue;
            }

            std::string otherName = entry.path().filename().string();
            auto otherInfo = parseEpisodeInfo(otherName);
            if (!otherInfo)
            {
                continue;
            }

            std::string otherSeriesName = std::get<0>(*otherInfo);
            int otherSeason = std::get<1>(*otherInfo);
            int otherEpisode = std::get<2>(*otherInfo);
            std::string otherNormalized = normalizeSeriesName(otherSeriesName);

            // 季数必须相同，系列名称必须匹配
            if (otherSeason != selfSeason)
            {
                continue;
            }

            // 名称匹配：完全匹配，或子串匹配时长度差异不超过 20%
            bool nameMatches = (selfNormalized == otherNormalized);
            if (!nameMatches) {
                if (selfNormalized.find(otherNormalized) != std::string::npos) {
                    nameMatches = (otherNormalized.length() >= selfNormalized.length() * 0.8);
                } else if (otherNormalized.find(selfNormalized) != std::string::npos) {
                    nameMatches = (selfNormalized.length() >= otherNormalized.length() * 0.8);
                }
            }

            if (!nameMatches)
            {
                continue;
            }

            EpisodeInfo info;
            info.path = otherPath;
            info.episodeNumber = otherEpisode;
            info.seasonNumber = otherSeason;
            info.title = otherSeriesName + " - " + std::to_string(otherEpisode);
            if (otherSeason > 0)
            {
                info.title = "S" + std::to_string(otherSeason) + "E" + std::to_string(otherEpisode);
            }
            else
            {
                info.title = "第 " + std::to_string(otherEpisode) + " 集";
            }
            group.episodes.push_back(info);
        }
    }
    catch (const std::exception& e)
    {
        Logger::instance().warning("Failed to scan directory for episodes: " + std::string(e.what()));
    }

    if (group.episodes.size() < 2)
    {
        // 至少需要2集才算一个剧集组
        return std::nullopt;
    }

    // 按集数排序
    std::sort(group.episodes.begin(), group.episodes.end(),
              [](const EpisodeInfo& a, const EpisodeInfo& b)
    {
        return a.episodeNumber < b.episodeNumber;
    });

    // 去重（按路径）
    auto last = std::unique(group.episodes.begin(), group.episodes.end(),
                            [](const EpisodeInfo& a, const EpisodeInfo& b)
    {
        return a.path == b.path;
    });
    group.episodes.erase(last, group.episodes.end());

    // 找到当前文件对应的索引（使用 filesystem 比较，避免路径分隔符差异）
    for (size_t i = 0; i < group.episodes.size(); ++i)
    {
        try {
            if (std::filesystem::equivalent(group.episodes[i].path, filePath))
            {
                group.currentIndex = i;
                break;
            }
        } catch (...) {
            // fallback: 字符串比较
            if (group.episodes[i].path == filePath)
            {
                group.currentIndex = i;
                break;
            }
        }
    }

    Logger::instance().info("Detected series: \"" + group.seriesName + "\" with " +
                            std::to_string(group.episodes.size()) + " episodes");

    return group;
}

} // namespace VideoPlay
