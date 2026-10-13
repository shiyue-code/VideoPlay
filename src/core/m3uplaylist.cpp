#include "core/m3uplaylist.h"
#include "utils/string_utils.h"
#include "core/common.h"
#include <cctype>
#include <filesystem>

namespace VideoPlay {

namespace {

// EXTINF 行中提取标题：格式 "#EXTINF:<时长> [属性...],<标题>"，
// 分隔逗号取引号外第一个（tvg-* 属性值里可能带逗号）
std::string extinfTitle(const std::string& line) {
    bool inQuotes = false;
    for (size_t i = 8; i < line.size(); ++i) {
        const char c = line[i];
        if (c == '"') {
            inQuotes = !inQuotes;
        } else if (c == ',' && !inQuotes) {
            return trim(line.substr(i + 1));
        }
    }
    return {};
}

} // namespace

bool isM3uPlaylistPath(const std::string& path) {
    std::string p = path;
    size_t suffix = p.find_first_of("?#");
    if (suffix != std::string::npos) {
        p = p.substr(0, suffix);
    }
    const std::string ext = toLower(std::filesystem::u8path(p).extension().u8string());
    return ext == ".m3u" || ext == ".m3u8";
}

M3uPlaylist parseM3uPlaylist(const std::string& content) {
    M3uPlaylist result;
    std::string pendingTitle;

    size_t pos = 0;
    while (pos <= content.size()) {
        size_t eol = content.find('\n', pos);
        std::string line = content.substr(
            pos, eol == std::string::npos ? std::string::npos : eol - pos);
        pos = eol == std::string::npos ? content.size() + 1 : eol + 1;
        line = trim(line);
        if (line.empty()) continue;

        if (line.front() == '#') {
            // #EXT-X-* 是 HLS 标签（含 #EXT-X-STREAM-INF / #EXT-X-TARGETDURATION 等）；
            // #EXTM3U 是表头不是 HLS 标签，注意前缀边界
            if (startsWith(line, "#EXT-X")) {
                result.isHlsManifest = true;
            } else if (startsWith(line, "#EXTINF:")) {
                pendingTitle = extinfTitle(line);
            }
            continue;
        }

        result.entries.push_back({std::move(pendingTitle), std::move(line)});
        pendingTitle.clear();
    }
    return result;
}

std::string resolveM3uUri(const std::string& playlistSource, const std::string& uriIn) {
    std::string uri = trim(uriIn);
    if (uri.empty()) return {};
    if (isNetworkUrl(uri)) return uri;

    // file:// URI 还原为本地路径；file:///C:/x 去掉多余前导斜杠
    if (startsWith(toLower(uri), "file://")) {
        std::string local = uri.substr(7);
        while (startsWith(local, "/") && local.size() > 2 &&
               std::isalpha(static_cast<unsigned char>(local[1])) && local[2] == ':') {
            local.erase(local.begin());
        }
        return std::filesystem::u8path(local).lexically_normal().generic_u8string();
    }

    const std::filesystem::path uriPath = std::filesystem::u8path(uri);
    if (uriPath.is_absolute()) return uriPath.generic_u8string();

    if (isNetworkUrl(playlistSource)) {
        // 远程清单：相对条目按清单 URL 目录拼接
        size_t query = playlistSource.find_first_of("?#");
        std::string base = query == std::string::npos
            ? playlistSource : playlistSource.substr(0, query);
        size_t slash = base.find_last_of('/');
        if (slash == std::string::npos) return uri;
        std::string resolved = base.substr(0, slash + 1) + uri;
        // 处理 ../ 段
        size_t dotdot;
        while ((dotdot = resolved.find("/../")) != std::string::npos) {
            size_t prev = resolved.find_last_of('/', dotdot - 1);
            if (prev == std::string::npos || prev < resolved.find("://") + 2) break;
            resolved.erase(prev, dotdot + 3 - prev);
        }
        return resolved;
    }

    const std::filesystem::path base =
        std::filesystem::u8path(playlistSource).parent_path();
    return (base / uriPath).lexically_normal().generic_u8string();
}

} // namespace VideoPlay
