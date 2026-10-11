#pragma once

#include <string>

namespace VideoPlay {

// VideoPlay 在 HKCU\Software\Classes 下注册的 ProgId
inline const char* fileAssocProgId() { return "VideoPlay.Video"; }

// 当前平台是否支持文件关联（仅 Windows 实现）
inline bool fileAssocSupported() {
#ifdef _WIN32
    return true;
#else
    return false;
#endif
}

// 注册扩展名关联时能否接管扩展键默认值（纯函数，便于单测）：
// 默认值为空或已指向 VideoPlay 的 ProgId 才允许覆盖；
// 被其它应用占用的默认值不抢占（仍通过 OpenWithProgids 出现在「打开方式」中）
inline bool fileAssocMayTakeDefault(const std::string& existingDefault) {
    return existingDefault.empty() || existingDefault == fileAssocProgId();
}

// 当前用户的文件关联是否已指向本 exe：
// ProgId 存在且其 shell\open\command 指向当前程序路径
bool isFileAssocRegistered();

// 写入 HKCU 文件关联（ProgId / Capabilities / RegisteredApplications /
// 各扩展名 OpenWithProgids 与默认值）。全部步骤成功返回 true，失败步骤记日志
bool registerFileAssociations();

// 移除 VideoPlay 的文件关联：删除 ProgId 树与 RegisteredApplications 项，
// 清掉各扩展名 OpenWithProgids 中的本应用项；扩展名默认值仅当仍指向
// 本 ProgId 时清空，不删除扩展名键本身
bool unregisterFileAssociations();

// 打开 Windows「默认应用」设置页
bool openDefaultAppsSettings();

} // namespace VideoPlay
