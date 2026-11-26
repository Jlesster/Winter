/*
 * Winter - A lightweight text editor
 * Copyright (C) 2026 Jessica Price
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#include <chrono>
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
#include "output.hpp"
#include "quickfix.hpp"
#include "term_panel.hpp"

// The editor side of build delegation: runs the plan from build_plan() as a
// job, feeds its output through the error parsers into the quickfix list.

namespace {

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

struct Run {
    JobId id = 0;
    std::unique_ptr<ErrChain> chain;
    std::vector<fs::path> bases;
    fs::path root;
    OutId out = 0;   // the run's OutputModel
    size_t fed = 0;  // model lines already given to the parsers
    bool nojump = false;
    bool have_diag = false;  // a Context line has a diagnostic to join
    char last_type = 'e';
    int t_pass = 0, t_fail = 0, t_skip = 0;
    std::string progress;
    Clock::time_point status_at{};
    QfCounts shown;  // counts last reported in the status line
    std::map<std::string, std::string>
        resolved;  // per run: tool path -> absolute
} g_run;

unsigned g_gen = 0;  // callbacks of a superseded run compare this and bail
std::string g_makeprg;

void handle(size_t idx, ErrEvent ev) {
    switch(ev.kind) {
        case ErrEvent::Kind::None: return;
        case ErrEvent::Kind::Diag: {
            QfEntry& d = ev.diag;
            auto it = g_run.resolved.find(d.file);
            if(it == g_run.resolved.end())
                it = g_run.resolved
                         .emplace(d.file,
                                  build_resolve_path(
                                      d.file, g_run.bases, g_run.root))
                         .first;
            d.file = it->second;
            const char type = d.type;
            qf_add(std::move(d));
            g_run.have_diag = true;
            g_run.last_type = type;
            if(qf_size() > 0) output_link(g_run.out, idx, qf_size() - 1, type);
            return;
        }
        case ErrEvent::Kind::Context:
            if(!g_run.have_diag || qf_size() == 0) return;
            qf_context(ev.text, ev.col, ev.end_col);
            output_link(g_run.out, idx, qf_size() - 1, g_run.last_type, false);
            return;
        case ErrEvent::Kind::Test:
            g_run.have_diag = false;
            if(ev.test.status == ErrTest::Status::Fail) {
                ++g_run.t_fail;
                output_mark(g_run.out, idx, 'e');
            } else if(ev.test.status == ErrTest::Status::Skip) {
                ++g_run.t_skip;
                output_mark(g_run.out, idx, 'n');
            } else {
                ++g_run.t_pass;
            }
            return;
        case ErrEvent::Kind::Progress:
            g_run.progress = std::move(ev.text);
            output_progress(g_run.out, g_run.progress);
            return;
        case ErrEvent::Kind::Summary:
            output_summary(g_run.out, ev.text);
            return;
    }
}

// Parsers read the model's lines: ANSI colour and \r progress are already gone.
void feed_ready() {
    const size_t done = output_complete(g_run.out);
    for(; g_run.fed < done; ++g_run.fed)
        handle(g_run.fed, g_run.chain->feed(output_line(g_run.out, g_run.fed)));
}

void run_data(std::string_view chunk) {
    output_append(g_run.out, chunk);
    feed_ready();

    const QfCounts n = qf_counts();
    const auto now = Clock::now();
    const bool changed =
        n.errors != g_run.shown.errors || n.warnings != g_run.shown.warnings;
    if((changed || !g_run.progress.empty()) &&
       now - g_run.status_at > std::chrono::milliseconds(80)) {
        g_run.shown = n;
        g_run.status_at = now;
        editor_set_status_message("make: {} errors, {} warnings{}{}",
                                  n.errors,
                                  n.warnings,
                                  g_run.progress.empty() ? "" : "  ",
                                  g_run.progress);
    }
}

void run_exit(int code) {
    output_end(g_run.out, OutState::Done, code);
    feed_ready();  // a last line without a newline
    while(ErrEvent ev = g_run.chain->finish())
        handle(output_lines(g_run.out) ? output_lines(g_run.out) - 1 : 0,
               std::move(ev));
    g_run.id = 0;
    const OutId out = g_run.out;
    const bool nojump = g_run.nojump;

    const QfCounts n = qf_counts();
    const bool failed = code != 0 || n.errors != 0;
    if(!failed) {
        if(n.warnings)
            editor_set_status_message(
                "make: ok ({} warnings, :cn to walk them)", n.warnings);
        else editor_set_status_message("make: ok");
        return;
    }
    output_auto(out, OutAuto::Fail);
    if(n.errors == 0) {  // failed, but nothing we could parse
        if(g_run.t_fail)
            editor_set_status_message(
                "make: {} tests failed, {} passed (exit {})",
                g_run.t_fail,
                g_run.t_pass,
                code);
        else editor_set_status_message("make: failed (exit {})", code);
        return;
    }
    if(nojump) {
        editor_set_status_message("make: {} errors, {} warnings (exit {})",
                                  n.errors,
                                  n.warnings,
                                  code);
        return;
    }
    qf_jump_first();  // its status line carries (i/n) and the message
}

void cmd_make(const CmdLine& c) {
    std::error_code ec;
    const bool has_file = !E.buf().nofile && !E.buf().filename.empty();
    const fs::path start =
        has_file ? fs::absolute(E.buf().filename, ec).parent_path()
                 : fs::current_path(ec);

    BuildCmd bc;
    fs::path root;
    std::string title;
    if(!g_makeprg.empty()) {
        root = fs::current_path(ec);
        bc = {g_makeprg + (c.args.empty() ? "" : " " + c.args),
              root,
              errparser_default_chain(),
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
        editor_set_status_message("No build system found above {} (tried: {})",
                                  start.string(),
                                  names);
        return;
    }

    if(g_run.id) {
        job_kill(g_run.id);
        output_end(g_run.out, OutState::Killed, -1);
    }
    const unsigned gen = ++g_gen;
    g_run = Run{};
    g_run.chain = std::make_unique<ErrChain>(bc.parsers);
    g_run.root = root;
    g_run.bases.push_back(bc.cwd);
    for(const fs::path& p : bc.resolve_from) g_run.bases.push_back(p);
    g_run.bases.push_back(root);
    g_run.nojump = c.bang;
    qf_begin("make: " + title);
    g_run.out =
        output_begin({.name = "make",
                      .title = "make: " + title + " (" + bc.cmd + ")",
                      .streamed = false,
                      .kill =
                          [gen] {
                              if(gen == g_gen && g_run.id) {
                                  job_kill(g_run.id);
                                  output_end(g_run.out, OutState::Killed, -1);
                              }
                          },
                      .rerun =
                          [args = c.args, bang = c.bang] {
                              CmdLine again;
                              again.args = args;
                              again.bang = bang;
                              cmd_make(again);
                          }});
    output_auto(g_run.out, OutAuto::Start);

    const std::string full =
        "cd " + build_shell_quote(bc.cwd.string()) + " && " + bc.cmd;
    const JobId id = job_start({.cmd = full,
                                .keep_out = false,
                                .on_data =
                                    [gen](std::string_view d) {
                                        if(gen == g_gen) run_data(d);
                                    },
                                .on_exit =
                                    [gen](JobResult r) {
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
    if(!output_current()) editor_set_status_message("No make output yet");
    else output_present(output_current(), OutMode::Full);
}

}  // namespace

void build_register_commands() {
    output_register_commands();
    term_register_commands();
    command_add("make", cmd_make);
    command_add("makeprg", cmd_makeprg);
    command_add("mout", cmd_mout);
}
