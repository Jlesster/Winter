#include "format_run.hpp"

#include <memory>

#include "job.hpp"
#include "loop.hpp"

namespace {
std::string sq(const std::string& s) {
    std::string o = "'";
    for(const char c : s) {
        if(c == '\'') o += "'\\''";
        else o += c;
    }
    return o + "'";
}
struct St {
    bool done = false;
    FmtOut r;
};
}  // namespace

FmtOut run_formatter(const std::vector<std::string>& argv,
                     const std::string& cwd, const std::string& input,
                     int timeout_ms) {
    FmtOut fail;
    if(argv.empty()) {
        fail.err = "empty command";
        return fail;
    }
    std::string cmd;
    if(!cwd.empty()) cmd = "cd " + sq(cwd) + " && ";
    cmd += "exec";
    for(const std::string& a : argv) cmd += " " + sq(a);

    const auto st = std::make_shared<St>();
    const JobId id = job_start({
        .cmd = cmd,
        .input = input,
        .keep_out = true,
        .split_err = true,
        .on_err =
            [st](std::string_view c) {
                if(st->r.err.size() < 64 * 1024) st->r.err.append(c);
            },
        .on_exit =
            [st](JobResult r) {
                st->r.out = std::move(r.out);
                st->r.code = r.code;
                st->done = true;
            },
    });
    if(!id) {
        fail.err = "cannot spawn";
        return fail;
    }
    if(!loop_pump_until([&] { return st->done; }, timeout_ms)) {
        job_kill(id);
        FmtOut r;
        r.timeout = true;
        r.err = "timed out after " + std::to_string(timeout_ms) + "ms";
        return r;
    }
    st->r.ok = st->r.code == 0;
    return st->r;
}
