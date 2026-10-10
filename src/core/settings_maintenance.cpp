#include "core/settings_maintenance.h"

#include <vector>

namespace VideoPlay {

size_t pruneMissingFileEntries(nlohmann::json& object,
                               const std::function<bool(const std::string&)>& fileExists)
{
    if (!object.is_object()) {
        return 0;
    }

    std::vector<std::string> stale;
    for (const auto& item : object.items()) {
        const std::string& key = item.key();
        // 网络 URL 与 UNC/网络路径不做本地存在性检查
        if (key.find("://") != std::string::npos) {
            continue;
        }
        if (key.size() >= 2 &&
            (key.compare(0, 2, "\\\\") == 0 || key.compare(0, 2, "//") == 0)) {
            continue;
        }
        if (!fileExists(key)) {
            stale.push_back(key);
        }
    }

    for (const auto& key : stale) {
        object.erase(key);
    }
    return stale.size();
}

} // namespace VideoPlay
