#ifndef EPISODEDETECTOR_H
#define EPISODEDETECTOR_H

#include <string>
#include <vector>
#include <optional>
#include <tuple>

namespace VideoPlay {

struct EpisodeInfo
{
    std::string path;
    std::string title;
    int episodeNumber = 0;
    int seasonNumber = 0;
};

struct SeriesGroup
{
    std::string seriesName;
    int seasonNumber = 0;
    std::vector<EpisodeInfo> episodes;
    size_t currentIndex = 0;
};

class EpisodeDetector
{
public:
    static std::optional<SeriesGroup> detectFromFile(const std::string& filePath);

private:
    static std::optional<std::tuple<std::string, int, int>> parseEpisodeInfo(
        const std::string& filename);
    static std::string normalizeSeriesName(const std::string& name);
    static bool isMediaFile(const std::string& path);
};

} // namespace VideoPlay

#endif // EPISODEDETECTOR_H
