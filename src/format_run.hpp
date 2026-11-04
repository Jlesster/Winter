#pragma once
#include <string>
#include <vector>

struct FmtOut {
    bool ok = false;       // exited 0 in time
    bool timeout = false;
    int code = 0;
    std::string out, err;
};

// stdin -> stdout filter, run in `cwd` (empty = inherit). Blocks (pumping the
// loop, not stdin) for at most timeout_ms, then kills the process group.
FmtOut run_formatter(const std::vector<std::string>& argv,
                     const std::string& cwd, const std::string& input,
                     int timeout_ms);
