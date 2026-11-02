#include <cstddef>
#include <filesystem>
#include <format>
#include <map>
#include <memory>
#include <string>
#include <system_error>
#include <utility>

#include "buflist.hpp"
#include "build.hpp"
#include "command.hpp"
#include "editor.hpp"
#include "errparse.hpp"
#include "job.hpp"
#include "quickfix.hpp"

// The editor side of build delegation: runs the plan from build_plan() as a
// job, feeds its output through the error parsers into the quickfix list.

namespace {

namespace fs = std::filesystem;

constexpr size_t kMaxOut = 4u << 20;     // raw output kept for :mout
constexpr size_t kMaxCarry = 64u << 10;  // longest line we keep waiting for a \n

struct Run {
    JobId id = 0;
    std::unique_ptr<ErrChain> chain;
    std::vector<fs::path> bases;
    fs::path root;
    std::string carry;  // trailing partial line
    std::string out;
    bool truncated = false;
    bool nojump = false;
    QfCounts shown;  // counts last reported in the status line
    std::map<std::string, std::string> resolved;  // per run: tool path -> absolute
} g_run;

unsigned g_gen = 0;  // callbacks of a superseded run compare this and bail
std::string g_makeprg;
std::string g_last_out;

void feed_line(std::string_view l) {
    auto hit = g_run.chain->feed(l);
    if(!hit) return;
    auto it = g_run.resolved.find(hit->file);
    if(it == g_run.resolved.end())
        it = g_run.resolved
                 .emplace(hit->file,
                          build_resolve_path(hit->file, g_run.bases, g_run.root))
                 .first;
    hit->file = it->second;
    qf_add(std::move(*hit));
}

void run_data(std::string_view chunk) {
    const size_t room = kMaxOut > g_run.out.size() ? kMaxOut - g_run.out.size() : 0;
    g_run.out.append(chunk.substr(0, room));
    if(chunk.size() > room) g_run.truncated = true;

    g_run.carry.append(chunk);
    size_t start = 0;
    for(size_t nl; (nl = g_run.carry.find('\n', start)) != std::string::npos;
        start = nl + 1)
        feed_line(std::string_view(g_run.carry).substr(start, nl - start));
    g_run.carry.erase(0, start);
    if(g_run.carry.size() > kMaxCarry) {  // no newline in sight: give up on it
        feed_line(g_run.carry);
        g_run.carry.clear();
    }

    const QfCounts n = qf_counts();
    if(n.errors != g_run.shown.errors || n.warnings != g_run.shown.warnings) {
        g_run.shown = n;
        editor_set_status_message(
            "make: running... {} errors, {} warnings", n.errors, n.warnings);
    }
}

void run_exit(int code) {
    if(!g_run.carry.empty()) feed_line(g_run.carry);
    g_run.id = 0;
    g_last_out = std::move(g_run.out);
    if(g_run.truncated) g_last_out += "\n[output truncated]\n";
    const bool nojump = g_run.nojump;

    const QfCounts n = qf_counts();
    if(code == 0 && n.errors == 0) {
        if(n.warnings) editor_set_status_message("make: ok ({} warnings, :cn to walk them)", n.warnings);
        else editor_set_status_message("make: ok");
        return;
    }
    if(n.errors == 0) {  // failed, but nothing we could parse: show what it said
        editor_set_status_message("make: failed (exit {})", code);
        buffer_show_text("[make]", g_last_out);
        return;
    }
    if(nojump) {
        editor_set_status_message("make: {} errors, {} warnings (exit {})",
                                  n.errors, n.warnings, code);
        return;
    }
    qf_jump_first();  // its status line carries (i/n) and the message
}

void cmd_make(const CmdLine& c) {
    std::error_code ec;
    const bool has_file = !E.buf().nofile && !E.buf().filename.empty();
    const fs::path start = has_file ? fs::absolute(E.buf().filename, ec).parent_path()
                                    : fs::current_path(ec);

    BuildCmd bc;
    fs::path root;
    std::string title;
    if(!g_makeprg.empty()) {
        root = fs::current_path(ec);
        bc = {g_makeprg + (c.args.empty() ? "" : " " + c.args),
              root,
              {"gcc", "rustc", "go", "generic"},
              {}};
        title = g_makeprg;
    } else if(auto plan = build_plan(start, c.args)) {
        root = plan->root.dir;
        bc = std::move(plan->cmd);
        title = plan->provider->name;
    } else {
        std::string names;
        for(const std::string& n : build_provider_names())
            names += (names.empty() ? "" : ", ") + n;
        editor_set_status_message(
            "No build system found above {} (tried: {})", start.string(), names);
        return;
    }

    if(g_run.id) job_kill(g_run.id);
    const unsigned gen = ++g_gen;
    g_run = Run{};
    g_run.chain = std::make_unique<ErrChain>(bc.parsers);
    g_run.root = root;
    g_run.bases.push_back(bc.cwd);
    for(const fs::path& p : bc.resolve_from) g_run.bases.push_back(p);
    g_run.bases.push_back(root);
    g_run.nojump = c.bang;
    qf_begin("make: " + title);

    const std::string full =
        "cd " + build_shell_quote(bc.cwd.string()) + " && " + bc.cmd;
    const JobId id = job_start({.cmd = full,
                                .keep_out = false,
                                .on_data = [gen](std::string_view d) {
                                    if(gen == g_gen) run_data(d);
                                },
                                .on_exit = [gen](JobResult r) {
                                    if(gen == g_gen) run_exit(r.code);
                                }});
    if(!id) {
        editor_set_status_message("Can't run: {}", bc.cmd);
        return;
    }
    g_run.id = id;
    editor_set_status_message("make: {}", bc.cmd);
}

void cmd_makeprg(const CmdLine& c) {
    if(c.bang) {
        g_makeprg.clear();
        editor_set_status_message("makeprg cleared, auto-detecting");
    } else if(!c.args.empty()) {
        g_makeprg = c.args;
        editor_set_status_message("makeprg={}", g_makeprg);
    } else {
        editor_set_status_message(
            "makeprg={}", g_makeprg.empty() ? "(auto-detect)" : g_makeprg);
    }
}

void cmd_mout(const CmdLine&) {
    if(g_last_out.empty()) editor_set_status_message("No make output yet");
    else buffer_show_text("[make]", g_last_out);
}

}  // namespace

void build_register_commands() {
    command_add("make", cmd_make);
    command_add("makeprg", cmd_makeprg);
    command_add("mout", cmd_mout);
}
