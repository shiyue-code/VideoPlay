#include "utils/subprocess_runner.h"
#include <array>
#include <memory>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cstdio>
#endif

namespace VideoPlay {

int SubprocessRunner::run(const std::string& command, std::string& stdErr) {
#ifdef _WIN32
    int wideLen = MultiByteToWideChar(CP_UTF8, 0, command.c_str(), -1, nullptr, 0);
    if (wideLen <= 0) {
        return -1;
    }
    std::vector<wchar_t> wcommand(wideLen);
    MultiByteToWideChar(CP_UTF8, 0, command.c_str(), -1, wcommand.data(), wideLen);

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    HANDLE hRead = nullptr;
    HANDLE hWrite = nullptr;
    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) {
        return -1;
    }
    SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW startupInfo{};
    startupInfo.cb = sizeof(startupInfo);
    startupInfo.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
    startupInfo.wShowWindow = SW_HIDE;
    startupInfo.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startupInfo.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    startupInfo.hStdError = hWrite;

    PROCESS_INFORMATION processInfo{};
    BOOL ok = CreateProcessW(
        nullptr,
        wcommand.data(),
        nullptr,
        nullptr,
        TRUE,
        CREATE_NO_WINDOW | IDLE_PRIORITY_CLASS,
        nullptr,
        nullptr,
        &startupInfo,
        &processInfo);

    CloseHandle(hWrite);

    if (!ok) {
        CloseHandle(hRead);
        return static_cast<int>(GetLastError());
    }

    while (WaitForSingleObject(processInfo.hProcess, 100) == WAIT_TIMEOUT) {
        DWORD bytesAvailable = 0;
        while (PeekNamedPipe(hRead, nullptr, 0, nullptr, &bytesAvailable, nullptr) && bytesAvailable > 0) {
            std::vector<char> buf(bytesAvailable);
            DWORD bytesRead = 0;
            if (ReadFile(hRead, buf.data(), bytesAvailable, &bytesRead, nullptr)) {
                stdErr.append(buf.data(), bytesRead);
            }
        }
    }

    while (true) {
        DWORD bytesAvailable = 0;
        if (!PeekNamedPipe(hRead, nullptr, 0, nullptr, &bytesAvailable, nullptr) || bytesAvailable == 0) {
            break;
        }
        std::vector<char> buf(bytesAvailable);
        DWORD bytesRead = 0;
        if (!ReadFile(hRead, buf.data(), bytesAvailable, &bytesRead, nullptr)) {
            break;
        }
        stdErr.append(buf.data(), bytesRead);
    }

    CloseHandle(hRead);

    DWORD exitCode = 1;
    GetExitCodeProcess(processInfo.hProcess, &exitCode);
    CloseHandle(processInfo.hThread);
    CloseHandle(processInfo.hProcess);
    return static_cast<int>(exitCode);
#else
    std::array<char, 1024> buffer;
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen((command + " 2>&1").c_str(), "r"), pclose);
    if (!pipe) return -1;
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        stdErr += buffer.data();
    }
    return pclose(pipe.get());
#endif
}

} // namespace VideoPlay
