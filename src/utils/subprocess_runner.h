#pragma once

#include <string>

namespace VideoPlay {

class SubprocessRunner {
public:
    // 运行低优先级子进程，返回退出码；stderr 输出写入 stdErr
    static int run(const std::string& command, std::string& stdErr);
};

} // namespace VideoPlay
