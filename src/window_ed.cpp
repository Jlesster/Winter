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

#include "window_ed.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "buflist.hpp"
#include "command.hpp"
#include "complete.hpp"
#include "dock.hpp"
#include "editor.hpp"
#include "events.hpp"
#include "motion.hpp"
#include "normal.hpp"
#include "options.hpp"
#include "tree.hpp"
#include "window.hpp"

using win::Dir;
using win::Win;

namespace {

int g_area_rows = 0;
int g_hint_x = 0, g_hint_y = 0;

size_t index_of(const Buffer* b) {
    for(size_t i = 0; i < E.buffers.size(); ++i)
        if(E.buffers[i].get() == b) return i;
    return E.cur;
}

// The buffer may have changed under a window that was not looking.
void clamp_cursor() {
    const PieceTable& pt = E.buf().pt;
    E.cy = std::clamp(E.cy, 0, std::max(E.num_rows() - 1, 0));
    const auto line = static_cast<size_t>(E.cy);
    const int len = static_cast<int>(pt.line_end(line) - pt.line_start(line));
    E.cx = std::clamp(E.cx, 0, std::max(len, 0));
    set_cursor_offset(motion::clamp_normal(pt, cursor_offset()));
}

std::string run_capture(const std::string& cmd) {
    std::string out;
    if(FILE* f = popen(cmd.c_str(), "r")) {
        char buf[64];
        while(std::fgets(buf, sizeof buf, f) && out.size() < 256) out += buf;
        pclose(f);
    }
    return out;
}

bool valid_pane(const char* p) {
    if(!p || p[0] != '%' || !p[1]) return false;
    for(const char* c = p + 1; *c; ++c)
        if(*c < '0' || *c > '9') return false;
    return true;
}

bool tmux_resize(Dir d, int n) {
    if(!opts.tmux_nav) return false;
    const char* pane = std::getenv("TMUX_PANE");
    if(!std::getenv("TMUX") || !valid_pane(pane)) return false;
    static constexpr const char* kFlag[] = {"-L", "-D", "-U", "-R"};
    run_capture(std::string("tmux resize-pane -t ") + pane + " " +
                kFlag[static_cast<size_t>(d)] + " " + std::to_string(n) +
                " >/dev/null 2>&1");
    return true;
}

// smart-splits style: past the last winter window, go to the tmux pane in
// that direction. At the edge of the tmux window nothing happens, unless
// opts.tmux_wrap is set (tmux then wraps around by itself).
bool tmux_navigate(Dir d) {
    if(!opts.tmux_nav) return false;
    const char* tm = std::getenv("TMUX");
    const char* pane = std::getenv("TMUX_PANE");
    if(!tm || !*tm || !valid_pane(pane)) return false;
    static constexpr const char* kEdge[] = {"#{pane_at_left}",
                                            "#{pane_at_bottom}",
                                            "#{pane_at_top}",
                                            "#{pane_at_right}"};
    static constexpr const char* kFlag[] = {"-L", "-D", "-U", "-R"};
    const auto i = static_cast<size_t>(d);
    if(!opts.tmux_wrap) {
        const std::string at =
            run_capture(std::string("tmux display-message -p -t ") + pane +
                        " '" + kEdge[i] + "' 2>/dev/null");
        if(!at.empty() && at[0] == '1') return false;
    }
    run_capture(std::string("tmux select-pane -t ") + pane + " " + kFlag[i] +
                " >/dev/null 2>&1");
    return true;
}

}  // namespace

bool win_resize(Dir d, int count) {
    windows_ensure();
    const int n = std::max(count, 1) * opts.resize_step;
    return win::resize_dir(d, n) || tmux_resize(d, n);
}

void tmux_announce(bool on) {
    const char* pane = std::getenv("TMUX_PANE");
    if(!opts.tmux_nav || !std::getenv("TMUX") || !valid_pane(pane)) return;
    run_capture(std::string("tmux set-option -p -t ") + pane +
                (on ? " @pane-is-winter 1" : " -u @pane-is-winter") +
                " >/dev/null 2>&1");
}

void windows_ensure() {
    if(win::inited()) return;
    win::init(&E.buf());
    win_leave_view(win::focus());
}

void win_enter_view(Win* w) {
    E.cx = w->cx;
    E.cy = w->cy;
    E.want_rx = w->want_rx;
    E.row_offset = w->row_offset;
    E.row_sub = w->row_sub;
    E.col_offset = w->col_offset;
    E.cur = index_of(w->buf);
}

void win_leave_view(Win* w) {
    w->cx = E.cx;
    w->cy = E.cy;
    w->want_rx = E.want_rx;
    w->row_offset = E.row_offset;
    w->row_sub = E.row_sub;
    w->col_offset = E.col_offset;
}

void win_clamp_view() { clamp_cursor(); }

void win_apply_rect(const Win* w) {
    E.term_rows = std::max(w->rect.h - 1, 1);  // the last row is the status bar
    E.term_cols = std::max(w->rect.w, 1);
}

void win_set_area_rows(int rows) { g_area_rows = rows; }
int win_area_rows() { return g_area_rows; }
void win_set_hint(int x, int y) {
    g_hint_x = x;
    g_hint_y = y;
}

void win_focus_window(Win* target) {
    windows_ensure();
    hover_dismiss();  // a hover pane owns the view registers
    Win* from = win::focus();
    if(!target || target == from) return;
    set_mode(EditorMode::NORMAL);
    normal_reset();
    clamp_cursor();
    events_emit("win_leave", {std::to_string(from->id)});
    win_leave_view(from);
    Buffer* old = &E.buf();
    win::set_focus(target);
    win_enter_view(target);
    win_apply_rect(target);
    clamp_cursor();
    if(&E.buf() != old) {
        E.alt = old;
        buffer_entered(E.buf());
    }
    events_emit("win_enter", {std::to_string(target->id)});
}

bool win_navigate(Dir d) {
    windows_ensure();
    hover_dismiss();  // a hover pane owns the view registers
    if(tree_focused()) {
        if(d == Dir::Right) {
            tree_focus(false);
            return true;
        }
        return tmux_navigate(d);
    }
    if(dock_focused()) {
        const bool bottom = dock_rows() > 0;
        if(d == Dir::Down && bottom) return tmux_navigate(d);
        dock_focus(false);
        if(d == Dir::Up) return true;
    }
    if(Win* n = win::neighbor(d, g_hint_x, g_hint_y)) {
        win_focus_window(n);
        return true;
    }
    if(d == Dir::Left && tree_visible()) {
        tree_focus(true);
        return true;
    }
    if(d == Dir::Down && dock_rows() > 0) {
        dock_focus(true);
        return true;
    }
    return tmux_navigate(d);
}

bool win_split(bool vertical, bool before) {
    windows_ensure();
    hover_dismiss();  // a hover pane owns the view registers
    set_mode(EditorMode::NORMAL);
    normal_reset();
    win_leave_view(win::focus());  // the new window copies the view
    Win* made = win::split(vertical, before);
    if(made) events_emit("win_new", {std::to_string(made->id)});
    return made != nullptr;
}

void win_only() {
    windows_ensure();
    hover_dismiss();  // a hover pane owns the view registers
    std::vector<int> gone;
    for(const Win* w : win::all())
        if(w != win::focus()) gone.push_back(w->id);
    for(const int id : gone) events_emit("win_close", {std::to_string(id)});
    win::only();
}

bool win_close_current(bool force) {
    windows_ensure();
    hover_dismiss();  // a hover pane owns the view registers
    if(win::count() <= 1) return false;
    Win* w = win::focus();
    Buffer& b = E.buf();
    const bool shown_elsewhere = win::showing(&b).size() > 1;
    if(!force && !shown_elsewhere && !b.nofile && b.pt.modified()) {
        editor_set_status_message(
            "No write since last change (add ! to close anyway)");
        return true;
    }
    set_mode(EditorMode::NORMAL);
    normal_reset();
    win_leave_view(w);
    Buffer* old = &b;
    events_emit("win_close", {std::to_string(w->id)});
    Win* next = win::close(w);
    if(!next) return false;
    win_enter_view(next);
    win_apply_rect(next);
    clamp_cursor();
    if(&E.buf() != old) buffer_entered(E.buf());
    return true;
}

void win_buffer_changed() {
    if(win::inited() && win::focus()) win::focus()->buf = &E.buf();
}

void win_buffer_gone(Buffer* b) {
    if(!win::inited()) return;
    for(Win* w : win::showing(b)) {
        if(w == win::focus()) {  // switch_to() already moved it; be safe
            w->buf = &E.buf();
            continue;
        }
        if(win::count() > 1) {
            events_emit("win_close", {std::to_string(w->id)});
            win::close(w);
        } else {
            w->buf = &E.buf();
        }
    }
}

void win_buffer_replaced(Buffer* from, Buffer* to) {
    if(!win::inited()) return;
    for(Win* w : win::showing(from)) w->buf = to;
}

bool win_do(char key, int count) {
    windows_ensure();
    hover_dismiss();  // a hover pane owns the view registers
    const int n = std::max(count, 1);
    const auto& ws = win::all();
    const auto at =
        static_cast<size_t>(std::ranges::find(ws, win::focus()) - ws.begin());
    switch(key) {
        case 'h': return win_navigate(Dir::Left);
        case 'j': return win_navigate(Dir::Down);
        case 'k': return win_navigate(Dir::Up);
        case 'l': return win_navigate(Dir::Right);
        case 's': return win_split(false);
        case 'v': return win_split(true);
        case 'c':
        case 'q': return win_close_current(false);
        case 'o': win_only(); return true;
        case 'w': win_focus_window(ws[(at + 1) % ws.size()]); return true;
        case 'W':
            win_focus_window(ws[(at + ws.size() - 1) % ws.size()]);
            return true;
        case 'p':
            if(win::previous()) win_focus_window(win::previous());
            return true;
        case 'x': {
            win_leave_view(win::focus());
            if(!win::swap_next()) return false;
            win_enter_view(win::focus());  // the position keeps the cursor
            win_apply_rect(win::focus());
            clamp_cursor();
            return true;
        }
        case 'r': return win::rotate(true);
        case 'R': return win::rotate(false);
        case 'H':
            win_leave_view(win::focus());
            win::move_far(Dir::Left);
            return true;
        case 'J':
            win_leave_view(win::focus());
            win::move_far(Dir::Down);
            return true;
        case 'K':
            win_leave_view(win::focus());
            win::move_far(Dir::Up);
            return true;
        case 'L':
            win_leave_view(win::focus());
            win::move_far(Dir::Right);
            return true;
        case '=': win::equalize(); return true;
        case '+': win::resize(false, n); return true;
        case '-': win::resize(false, -n); return true;
        case '>': win::resize(true, n); return true;
        case '<': win::resize(true, -n); return true;
        case '_': win::maximize(false); return true;
        case '|': win::maximize(true); return true;
        default: return false;
    }
}

namespace {

void split_cmd(const CmdLine& c, bool vertical, bool empty) {
    if(!win_split(vertical)) return;
    if(empty) {
        E.buffers.push_back(std::make_unique<Buffer>());
        buffer_switch(E.buffers.size() - 1);
    } else if(!c.argv.empty()) {
        buffer_open(c.argv[0]);
    }
}

}  // namespace

void windows_register_commands() {
    command_add({.name = "split",
                 .aliases = {"sp"},
                 .desc = "Split the window, stacked",
                 .usage = ":sp [file]",
                 .nargs = Nargs::Opt,
                 .run = [](const CmdLine& c) { split_cmd(c, false, false); }});
    command_add({.name = "vsplit",
                 .aliases = {"vs"},
                 .desc = "Split the window, side by side",
                 .usage = ":vs [file]",
                 .nargs = Nargs::Opt,
                 .run = [](const CmdLine& c) { split_cmd(c, true, false); }});
    command_add({.name = "new",
                 .desc = "Split with an empty buffer",
                 .nargs = Nargs::None,
                 .run = [](const CmdLine& c) { split_cmd(c, false, true); }});
    command_add({.name = "vnew",
                 .desc = "Vertical split with an empty buffer",
                 .nargs = Nargs::None,
                 .run = [](const CmdLine& c) { split_cmd(c, true, true); }});
    command_add({.name = "close",
                 .aliases = {"clo"},
                 .desc = "Close this window",
                 .bang = true,
                 .nargs = Nargs::None,
                 .run = [](const CmdLine& c) {
                     if(!win_close_current(c.bang))
                         editor_set_status_message(
                             "Cannot close the last window");
                 }});
    command_add({.name = "only",
                 .aliases = {"on"},
                 .desc = "Close every other window",
                 .nargs = Nargs::None,
                 .run = [](const CmdLine&) {
                     win_only();
                 }});
    command_add({.name = "wincmd",
                 .aliases = {"winc"},
                 .desc = "Run a CTRL-W command",
                 .usage = ":[count]winc {key}",
                 .nargs = Nargs::One,
                 .run = [](const CmdLine& c) {
                     if(c.argv.empty() || c.argv[0].size() != 1 ||
                        !win_do(c.argv[0][0],
                                c.has_range ? static_cast<int>(c.last) : 0))
                         editor_set_status_message("wincmd: unknown key");
                 }});
}
