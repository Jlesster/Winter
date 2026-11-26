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

#include "normal.hpp"

#include <algorithm>
#include <climits>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include "columns.hpp"
#include "core.hpp"
#include "editor.hpp"
#include "input.hpp"
#include "keys.hpp"
#include "motion.hpp"
#include "multicursor.hpp"
#include "normal.hpp"
#include "reg.hpp"
#include "registry.hpp"
#include "utf8.hpp"

void editor_move_cursor(int key) {
    if(key == ARROW_UP) { return move_vertical(-1, 1); }
    if(key == ARROW_DOWN) { return move_vertical(+1, 1); }
    if(key == 'k') {
        if(insertish(E.mode)) return;
        if(E.mode == NORMAL || E.mode == VISUAL) return move_vertical(-1, 1);
        return;
    }
    if(key == 'j') {
        if(insertish(E.mode)) return;
        if(E.mode == NORMAL || E.mode == VISUAL) return move_vertical(+1, 1);
        return;
    }
    const int last = E.num_rows() - 1;
    std::string line = E.buf().pt.line_text(E.cy);
    switch(key) {
        case 'h':
        case ARROW_LEFT:
            if(E.cx > 0) {
                E.cx = static_cast<int>(gr_prev(line, E.cx));
            } else if(!insertish(E.mode) && E.cy > 0) {
                E.cy--;
                line = E.buf().pt.line_text(E.cy);
                E.cx = static_cast<int>(line_len(E.cy));
            }
            break;
        case 'l':
        case ARROW_RIGHT:
            if(E.cx < static_cast<int>(line.size())) {
                E.cx = static_cast<int>(gr_next(line, E.cx));
            } else if(!insertish(E.mode) && E.cy < last) {
                E.cy++;
                E.cx = 0;
            }
            break;
    }
    line = E.buf().pt.line_text(E.cy);
    E.cx = snap_cx(line, std::min(E.cx, static_cast<int>(line.size())));
    E.want_rx = cx_to_rx(line, E.cx);
}

void update_want() { E.want_rx = cx_to_rx(E.buf().pt.line_text(E.cy), E.cx); }

// j / k keep a goal column, so passing over a short line doesn't lose it
void move_vertical(int dir, int count) {
    // visible lines: a closed fold counts as one, and we land on its head
    const PieceTable& pt = E.buf().pt;
    const size_t from = static_cast<size_t>(E.cy);
    const int n = std::max(count, 1);
    const int target =
        static_cast<int>(dir > 0 ? motion::lines_down(pt, from, n)
                                 : motion::lines_up(pt, from, n));
    if(target == E.cy) return;
    E.cy = target;
    const std::string line = E.buf().pt.line_text(E.cy);
    const int size = static_cast<int>(line.size());
    int cx = E.want_rx == INT_MAX ? size : rx_to_cx(line, E.want_rx);
    if(E.mode == NORMAL && cx >= size &&
       size > 0)  // normal mode: stop on the last char
        cx = static_cast<int>(gr_prev(line, size));
    E.cx = cx;
}

void apply_motion(const Target& t) {
    if(!t.ok) return;  // vim beeps, we just stay put
    set_cursor_offset(t.pos);
    if(t.to_eol) E.want_rx = INT_MAX;
    else update_want();
}

namespace {

struct NormalState {
    int count = 0;           // 0 = nothing typed yet
    KeySeq keys;             // chord typed so far: "g", "gg", ... (parse_keys)
    bool want_char = false;  // waiting for the char argument of `action`
    std::string action;      // the wants_char action that is waiting
    std::string arg;         // that char's bytes (UTF-8 arrives 1 byte/key)
    bool shown = false;      // pending input is on the status line
    std::string op;
    int op_count = 0;
    char reg = 0;           // "x typed: the register for this command
    bool want_reg = false;  // " typed, its register name is next

    void reset() {
        count = 0;
        op_count = 0;
        reg = 0;
        want_reg = false;
        op.clear();
        keys.clear();
        want_char = false;
        action.clear();
        arg.clear();
        if(shown) E.status_message.clear();
        shown = false;
    }
};
NormalState N;

struct DotState {
    std::vector<int> last;
    std::vector<int> cur;
    bool in_insert = false;
    bool replaying = false;
    bool skip = false;
};
DotState D;

bool normal_pending() {
    return N.count || !N.keys.empty() || N.want_char || !N.op.empty() ||
           N.reg || N.want_reg;
}

// FILES has its own keymap. Only <space>... chords (leader maps) fall through
// to NORMAL; any other unmapped key is dropped so i, v, dd ... can't reach
// the buffer from the tree.
Hit lookup_keys(std::u32string_view keys) {
    if(E.mode == FILES) {
        const Hit h = registry().lookup(FILES, keys);
        if(h.kind != Lookup::None || keys.empty() || keys.front() != U' ')
            return h;
        return registry().lookup(NORMAL, keys);
    }
    const EditorMode m =
        E.mode == VISUAL ? VISUAL : (N.op.empty() ? NORMAL : OPPEND);
    return registry().lookup(m, keys, E.buf().id);  // this buffer's own maps too
}

void show_pending() {
    const std::string reg =
        N.reg ? std::string{'"', N.reg} : (N.want_reg ? "\"" : "");
    editor_set_status_message("{}{}{}",
                              reg,
                              N.count ? std::to_string(N.count) : std::string(),
                              format_keys(N.keys));
    N.shown = true;
}

// Actions that change the text, for a read-only buffer. The piece table
// refuses the edit itself; this stops them earlier, before a register is
// filled, a cursor moved or an operator left pending.
bool changes_text(std::string_view a) {
    if(a.starts_with("edit.")) return true;
    if(a.starts_with("op.")) return a != "op.yank";
    if(a.starts_with("visual.")) {
        static constexpr std::string_view kEdits[] = {"visual.change",
                                                      "visual.change_s",
                                                      "visual.comment",
                                                      "visual.decrement",
                                                      "visual.decrement_int",
                                                      "visual.dedent",
                                                      "visual.delete",
                                                      "visual.delete_x",
                                                      "visual.dup",
                                                      "visual.increment",
                                                      "visual.increment_int",
                                                      "visual.indent",
                                                      "visual.paste",
                                                      "visual.paste_keep",
                                                      "visual.surround",
                                                      "visual.toggle_case"};
        return std::ranges::find(kEdits, a) != std::end(kEdits);
    }
    return a == "mode.replace";
}

void dispatch(const std::string& action, Ctx ctx) {
    if(E.buf().pt.locked && changes_text(action)) {
        N.reset();
        buffer_refused_edit();
        return;
    }
    // N is reset before anything runs, so the register typed with "x travels
    // in RegScope; an operator that starts here (the d of "adw) gets it back
    // for its second half.
    const char reg = N.reg;
    if(N.op.empty()) {
        N.reset();
        {
            RegScope scope(reg);
            const auto run = [&] {
                Ctx c = ctx;
                if(!registry().run_action(action, c))
                    editor_set_status_message("No such action: {}", action);
            };
            if(mc_action_multi(action)) mc_each(run);
            else run();
        }
        if(!N.op.empty()) N.reg = reg;
        return;
    }
    const std::string op = N.op;
    const int count = (N.op_count | ctx.count)
                          ? std::max(N.op_count, 1) * std::max(ctx.count, 1)
                          : 0;
    N.reset();
    RegScope scope(reg);
    mc_each([&] { run_operator(op, action, count, ctx.arg); });
}

}  // namespace

void dot_supress() { D.skip = true; }
bool dot_replaying() { return D.replaying; }
bool dot_set_replaying(bool on) {
    const bool was = D.replaying;
    D.replaying = on;
    return was;
}

void dot_after_key(int c, EditorMode before, uint64_t rev_before) {
    if(D.replaying) return;
    if(insertish(before)) {
        if(!D.in_insert) return;
        if(c >= 0x100) {
            D.in_insert = false;
            D.cur.clear();
            return;
        }
        D.cur.push_back(c);
        if(insertish(E.mode)) {
            D.last = std::move(D.cur);
            D.cur.clear();
            D.in_insert = false;
        }
        return;
    }
    if(before != NORMAL) return;
    if(c == '\x1b') {
        D.cur.clear();
        return;
    }
    D.cur.push_back(c);
    if(normal_pending()) return;
    const bool changed =
        E.buf().pt.revision() != rev_before || insertish(E.mode);
    if(D.skip || !changed) {
        D.skip = false;
        D.cur.clear();
        return;
    }
    if(insertish(E.mode)) {
        D.in_insert = true;
        return;
    }
    D.last = std::move(D.cur);
    D.cur.clear();
}

void dot_replay() {
    if(D.last.empty()) return;
    const std::vector<int> keys = D.last;
    D.replaying = true;
    for(int k : keys) editor_handle_key(k);
    D.replaying = false;
    D.skip = true;
}

namespace {
static void run_exact(const Hit& hit) {
    if(registry().wants_char(hit.action)) {
        N.action = hit.action;
        N.want_char = true;
        show_pending();
        return;
    }
    dispatch(hit.action, Ctx{N.count, {}});
}
}  // namespace

void normal_reset() { N.reset(); }

bool normal_chord_pending() { return !N.keys.empty() && !N.want_char; }
bool normal_input_pending() { return normal_pending(); }

// would feeding `c` continue or finish a mapping? (editor_handle_key asks this
// before letting a modified key through)
bool normal_accepts(int c) {
    if(N.want_char || N.want_reg) return false;
    KeySeq probe = N.keys;
    probe += static_cast<char32_t>(c);
    return lookup_keys(probe).kind != Lookup::None;
}

void normal_chord_timeout() {
    if(!normal_chord_pending()) return;
    const Hit hit = lookup_keys(N.keys);
    D.cur.clear();
    if(hit.kind == Lookup::Exact) run_exact(hit);
    else N.reset();
}

void normal_begin_operator(std::string_view name, int count) {
    N.op = name;
    N.op_count = count;
}

void normal_feed(int c) {
    if(N.want_reg) {  // the register name after "
        N.want_reg = false;
        if(c < 0x100 && reg_name_ok(static_cast<char>(c))) {
            N.reg = static_cast<char>(c);
            show_pending();
        } else {
            N.reset();
        }
        return;
    }
    if(N.want_char) {  // the argument of f / t / r ...: plain bytes only
        if(c == '\x1b' || c >= 0x100) {
            N.reset();
            return;
        }
        if(N.arg.empty() && c < 32) {
            N.reset();
            return;
        }
        N.arg += static_cast<char>(c);
        if(N.arg.size() < utf8_seq_len(N.arg[0])) return;
        const std::string action = N.action;
        dispatch(action, Ctx{N.count, N.arg});
        return;
    }

    // arrows, Home, PageUp ... are handled by editor_handle_key and aren't
    // mappable (yet); a modified key (Alt-x, Ctrl-Left) is.
    if(c >= 0x100 && !key_mods(c)) {
        N.reset();
        return;
    }
    // Esc cancels anything pending; with nothing pending it is a normal key
    // that can be mapped
    if(c == '\x1b' && (N.count || !N.keys.empty() || !N.op.empty())) {
        N.reset();
        return;
    }

    // "x picks a register for the next command ("ayy, 3"_dd, "+p). Unless the
    // user mapped a key starting with ", which then wins.
    if(c == '"' && N.keys.empty() && N.op.empty() && !N.reg &&
       (E.mode == NORMAL || E.mode == VISUAL) &&
       lookup_keys(std::u32string_view(U"\"")).kind == Lookup::None) {
        N.want_reg = true;
        show_pending();
        return;
    }

    if(N.keys.empty() &&
       ((c >= '1' && c <= '9') || (c == '0' && N.count > 0))) {
        N.count = std::min(N.count * 10 + (c - '0'), 1000000);
        show_pending();
        return;
    }

    N.keys += static_cast<char32_t>(c);
    const Hit hit = lookup_keys(N.keys);
    switch(hit.kind) {
        case Lookup::None: N.reset(); return;
        case Lookup::Prefix: show_pending(); return;
        case Lookup::Exact:
            if(hit.longer) {
                show_pending();
                return;
            }
            run_exact(hit);
            return;
    }
}
