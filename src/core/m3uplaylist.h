#ifndef M3UPLAYLIST_H
#define M3UPLAYLIST_H

#include <string>
#include <vector>

namespace VideoPlay {

// M3U/M3U8 播放列表条目
struct M3uEntry {
    std::string title;  // #EXTINF 标题，可空
    std::string uri;    // URL 或本地路径（相对路径保持原样，由 resolveM3uUri 解析）
};

struct M3uPlaylist {
    std::vector<M3uEntry> entries;
    // 含 #EXT-X- 标签的是 HLS 清单（媒体/主播放列表），不展开，交给 FFmpeg 直接播
    bool isHlsManifest = false;
};

// 路径/URL 扩展名是否为 .m3u / .m3u8（大小写不敏感，忽略 ? 与 # 后缀）
bool isM3uPlaylistPath(const std::string& path);

// 解析 M3U 文本。content 须已解码为 UTF-8（BOM 已剥除外无所谓，内部再兜底一次）。
// 支持 #EXTINF 行提取标题（逗号分隔，引号内逗号不算）；其它 # 指令忽略；
// 非 # 非空行即条目 URI，按出现顺序排列。
M3uPlaylist parseM3uPlaylist(const std::string& content);

// 把条目 URI 解析成可打开的路径/URL：
// - 已是 http(s)/rtsp/rtmp 或本地绝对路径 → 原样返回
// - file:// 前缀 → 还原为本地路径
// - 相对路径 → 相对清单自身位置解析（本地清单按目录，远程清单按 URL 目录）
// 返回空串表示无法解析。
std::string resolveM3uUri(const std::string& playlistSource, const std::string& uri);

} // namespace VideoPlay

#endif // M3UPLAYLIST_H
