#ifndef SETTINGS_MAINTENANCE_H
#define SETTINGS_MAINTENANCE_H

#include <cstddef>
#include <functional>
#include <string>

#include <nlohmann/json.hpp>

namespace VideoPlay {

// 清理以本地文件路径为键的失效条目：网络 URL 与 UNC/网络路径保留
// （避免启动时访问不可达的网络盘阻塞）。返回删除的条目数。
size_t pruneMissingFileEntries(nlohmann::json& object,
                               const std::function<bool(const std::string&)>& fileExists);

} // namespace VideoPlay

#endif // SETTINGS_MAINTENANCE_H
