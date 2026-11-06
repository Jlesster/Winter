#include "normal.hpp"

#include <algorithm>
#include <climits>
#include <string>
#include <vector>

#include "columns.hpp"
#include "core.hpp"
#include "editor.hpp"
#include "input.hpp"
#include "keys.hpp"
#include "motion.hpp"
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
                E.cx = static_cast<int>(utf8_prev(line, E.cx));
            } else if(!insertish(E.mode) && E.cy > 0) {
                E.cy--;
                line = E.buf().pt.line_text(E.cy);
                E.cx = static_cast<int>(line_len(E.cy));
            }
            break;
        case 'l':
        case ARROW_RIGHT:
            if(E.cx < static_cast<int>(line.size())) {
                E.cx = static_cast<int>(utf8_next(line, E.cx));
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
    const int target =
        std::clamp(E.cy + dir * std::max(count, 1), 0, E.num_rows() - 1);
    if(target == E.cy) return;
    E.cy = target;
    const std::string line = E.buf().pt.line_text(E.cy);
    const int size = static_cast<int>(line.size());
    int cx = E.want_rx == INT_MAX ? size : rx_to_cx(line, E.want_rx);
    if(E.mode == NORMAL && cx >= size &&
       size > 0)  // normal mode: stop on the last char
        cx = static_cast<int>(utf8_prev(line, size));
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

    void reset() {
        count = 0;
        op_count = 0;
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
    return N.count || !N.keys.empty() || N.want_char || !N.op.empty();
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
    return registry().lookup(m, keys);
}

void show_pending() {
    editor_set_status_message("{}{}",
                              N.count ? std::to_string(N.count) : std::string(),
                              format_keys(N.keys));
    N.shown = true;
}

void dispatch(const std::string& action, Ctx ctx) {
    if(N.op.empty()) {
        N.reset();
        if(!registry().run_action(action, ctx))
            editor_set_status_message("No such action: {}", action);
        return;
    }
    const std::string op = N.op;
    const int count = (N.op_count | ctx.count)
                          ? std::max(N.op_count, 1) * std::max(ctx.count, 1)
                          : 0;
    N.reset();
    run_operator(op, action, count, ctx.arg);
}

}  // namespace

void dot_supress() { D.skip = true; }

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
    if(N.want_char) return false;
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
