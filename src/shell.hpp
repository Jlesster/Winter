#pragma once
#include <string>

struct ShellResult {
    std::string out;
    int code = 0;
    bool ok = true;
};
ShellResult shell_run(const std::string& cmd, const std::string* input);
