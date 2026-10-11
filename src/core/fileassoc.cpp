#include "core/fileassoc.h"

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#endif

#include "core/common.h"
#include "utils/logger.h"

namespace VideoPlay {

#ifdef _WIN32

namespace {

Logger& logger() {
    static auto logger = Logger::get("fileassoc");
    return *logger;
}

// UTF-8 → UTF-16
std::wstring utf8ToWide(const std::string& text) {
    if (text.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                                  nullptr, 0);
    if (len <= 0) return {};
    std::wstring wide(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                        wide.data(), len);
    return wide;
}

// UTF-16 → UTF-8
std::string wideToUtf8(const std::wstring& text) {
    if (text.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                                  nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string utf8(static_cast<size_t>(len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                        utf8.data(), len, nullptr, nullptr);
    return utf8;
}

const std::wstring& progIdW() {
    static const std::wstring id = utf8ToWide(fileAssocProgId());
    return id;
}

// 当前 exe 的完整路径（处理超长路径）
std::wstring currentExePath() {
    std::wstring buffer(MAX_PATH, L'\0');
    DWORD len = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    while (len >= buffer.size() && buffer.size() < 32768) {
        buffer.resize(buffer.size() * 2);
        len = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    }
    buffer.resize(len);
    return buffer;
}

// 创建/打开 HKCU 子键并写 REG_SZ；valueName 为 nullptr 表示键默认值
bool writeRegString(const std::wstring& subKey, const wchar_t* valueName,
                    const std::wstring& data) {
    HKEY hKey = nullptr;
    LSTATUS st = RegCreateKeyExW(HKEY_CURRENT_USER, subKey.c_str(), 0, nullptr, 0,
                                 KEY_SET_VALUE, nullptr, &hKey, nullptr);
    if (st != ERROR_SUCCESS) {
        logger().error("打开/创建注册表键失败: " + wideToUtf8(subKey) +
                       " err=" + std::to_string(st));
        return false;
    }
    st = RegSetValueExW(hKey, valueName, 0, REG_SZ,
                        reinterpret_cast<const BYTE*>(data.c_str()),
                        static_cast<DWORD>((data.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(hKey);
    if (st != ERROR_SUCCESS) {
        logger().error("写注册表值失败: " + wideToUtf8(subKey) +
                       " err=" + std::to_string(st));
        return false;
    }
    return true;
}

// 写一个空数据值（REG_NONE），用于 OpenWithProgids 下的 ProgId 名单
bool writeRegEmptyValue(const std::wstring& subKey, const wchar_t* valueName) {
    HKEY hKey = nullptr;
    LSTATUS st = RegCreateKeyExW(HKEY_CURRENT_USER, subKey.c_str(), 0, nullptr, 0,
                                 KEY_SET_VALUE, nullptr, &hKey, nullptr);
    if (st != ERROR_SUCCESS) {
        logger().error("打开/创建注册表键失败: " + wideToUtf8(subKey) +
                       " err=" + std::to_string(st));
        return false;
    }
    st = RegSetValueExW(hKey, valueName, 0, REG_NONE, nullptr, 0);
    RegCloseKey(hKey);
    if (st != ERROR_SUCCESS) {
        logger().error("写注册表空值失败: " + wideToUtf8(subKey) +
                       " err=" + std::to_string(st));
        return false;
    }
    return true;
}

// 读 REG_SZ 值；键或值不存在、类型不符时返回 false（不算错误）
bool readRegString(const std::wstring& subKey, const wchar_t* valueName,
                   std::wstring& out) {
    HKEY hKey = nullptr;
    LSTATUS st = RegOpenKeyExW(HKEY_CURRENT_USER, subKey.c_str(), 0,
                               KEY_QUERY_VALUE, &hKey);
    if (st != ERROR_SUCCESS) return false;

    DWORD type = 0;
    DWORD size = 0;
    st = RegQueryValueExW(hKey, valueName, nullptr, &type, nullptr, &size);
    if (st != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ)) {
        RegCloseKey(hKey);
        return false;
    }
    out.assign(size / sizeof(wchar_t), L'\0');
    st = RegQueryValueExW(hKey, valueName, nullptr, nullptr,
                          reinterpret_cast<BYTE*>(out.data()), &size);
    RegCloseKey(hKey);
    if (st != ERROR_SUCCESS) return false;
    while (!out.empty() && out.back() == L'\0') out.pop_back();
    return true;
}

// 从 open command（形如 "\"<exe>\" \"%1\""）中提取 exe 路径
std::wstring commandExePath(const std::wstring& command) {
    const size_t first = command.find(L'"');
    if (first == std::wstring::npos) {
        const size_t space = command.find(L' ');
        return space == std::wstring::npos ? command : command.substr(0, space);
    }
    const size_t second = command.find(L'"', first + 1);
    if (second == std::wstring::npos) return command.substr(first + 1);
    return command.substr(first + 1, second - first - 1);
}

void notifyAssocChanged() {
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
}

} // namespace

bool isFileAssocRegistered() {
    const std::wstring commandKey =
        L"Software\\Classes\\" + progIdW() + L"\\shell\\open\\command";
    std::wstring command;
    if (!readRegString(commandKey, nullptr, command)) {
        return false;
    }
    const std::wstring exe = currentExePath();
    const std::wstring registered = commandExePath(command);
    return !exe.empty() && !registered.empty() && _wcsicmp(registered.c_str(), exe.c_str()) == 0;
}

bool registerFileAssociations() {
    bool ok = true;
    const std::wstring& progId = progIdW();
    const std::wstring exe = currentExePath();
    if (exe.empty()) {
        logger().error("获取当前 exe 路径失败");
        return false;
    }

    const std::wstring progKey = L"Software\\Classes\\" + progId;

    // ProgId 主体：友好名 / 图标 / 打开命令
    ok &= writeRegString(progKey, nullptr, L"VideoPlay Video");
    ok &= writeRegString(progKey + L"\\DefaultIcon", nullptr, L"\"" + exe + L"\",0");
    ok &= writeRegString(progKey + L"\\shell\\open\\command", nullptr,
                         L"\"" + exe + L"\" \"%1\"");

    // Capabilities：让 VideoPlay 出现在「默认应用」候选中
    ok &= writeRegString(progKey + L"\\Capabilities", L"ApplicationName", L"VideoPlay");
    ok &= writeRegString(progKey + L"\\Capabilities", L"ApplicationDescription",
                         L"VideoPlay 视频播放器");
    const std::wstring fileAssocKey = progKey + L"\\Capabilities\\FileAssociations";
    for (const std::string& ext : mediaExtensions()) {
        ok &= writeRegString(fileAssocKey, utf8ToWide(ext).c_str(), progId);
    }

    // RegisteredApplications 指向 Capabilities
    ok &= writeRegString(L"Software\\RegisteredApplications", L"VideoPlay",
                         L"Software\\Classes\\" + progId + L"\\Capabilities");

    // 每个扩展名：OpenWithProgids 始终加入；默认值仅在没有归属时接管
    for (const std::string& ext : mediaExtensions()) {
        const std::wstring extKey = L"Software\\Classes\\" + utf8ToWide(ext);
        ok &= writeRegEmptyValue(extKey + L"\\OpenWithProgids", progId.c_str());

        std::wstring existing;
        readRegString(extKey, nullptr, existing);
        if (fileAssocMayTakeDefault(wideToUtf8(existing))) {
            ok &= writeRegString(extKey, nullptr, progId);
        }
    }

    if (ok) {
        notifyAssocChanged();
    }
    return ok;
}

bool unregisterFileAssociations() {
    bool ok = true;
    const std::wstring& progId = progIdW();
    const std::wstring progKey = L"Software\\Classes\\" + progId;

    LSTATUS st = RegDeleteTreeW(HKEY_CURRENT_USER, progKey.c_str());
    if (st != ERROR_SUCCESS && st != ERROR_FILE_NOT_FOUND) {
        logger().error("删除 ProgId 注册表树失败 err=" + std::to_string(st));
        ok = false;
    }

    st = RegDeleteKeyValueW(HKEY_CURRENT_USER, L"Software\\RegisteredApplications",
                            L"VideoPlay");
    if (st != ERROR_SUCCESS && st != ERROR_FILE_NOT_FOUND) {
        logger().error("删除 RegisteredApplications 项失败 err=" + std::to_string(st));
        ok = false;
    }

    // 各扩展名：移除 OpenWithProgids 名单项；默认值仍指向本应用才清空
    for (const std::string& ext : mediaExtensions()) {
        const std::wstring extKey = L"Software\\Classes\\" + utf8ToWide(ext);

        st = RegDeleteKeyValueW(HKEY_CURRENT_USER,
                                (extKey + L"\\OpenWithProgids").c_str(), progId.c_str());
        if (st != ERROR_SUCCESS && st != ERROR_FILE_NOT_FOUND) {
            logger().error("删除 OpenWithProgids 项失败: " + ext +
                           " err=" + std::to_string(st));
            ok = false;
        }

        std::wstring existing;
        if (readRegString(extKey, nullptr, existing) && existing == progId) {
            ok &= writeRegString(extKey, nullptr, L"");
        }
    }

    if (ok) {
        notifyAssocChanged();
    }
    return ok;
}

bool openDefaultAppsSettings() {
    const HINSTANCE result = ShellExecuteW(nullptr, L"open", L"ms-settings:defaultapps",
                                           nullptr, nullptr, SW_SHOWNORMAL);
    const bool ok = reinterpret_cast<intptr_t>(result) > 32;
    if (!ok) {
        logger().error("打开「默认应用」设置页失败");
    }
    return ok;
}

#else // !_WIN32

// 非 Windows 平台暂无文件关联实现，全部为 no-op
bool isFileAssocRegistered() { return false; }
bool registerFileAssociations() { return false; }
bool unregisterFileAssociations() { return false; }
bool openDefaultAppsSettings() { return false; }

#endif // _WIN32

} // namespace VideoPlay
