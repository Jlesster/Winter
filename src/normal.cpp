#include "normal.hpp"

#include <algorithm>
#include <climits>
#include <string>

#include "columns.hpp"
#include "editor.hpp"
#include "keys.hpp"
#include "motion.hpp"
#include "registry.hpp"
#include "utf8.hpp"

void editor_move_cursor(int key) {
    if(key == ARROW_UP || key == 'k') return move_vertical(-1, 1);
    if(key == ARROW_DOWN || key == 'j') return move_vertical(+1, 1);
    const int last = E.num_rows() - 1;
    std::string line = E.pt.line_text(E.cy);
    switch(key) {
        case 'h':
        case ARROW_LEFT:
            if(E.cx > 0) {
                E.cx = static_cast<int>(utf8_prev(line, E.cx));
            } else if(E.cy > 0) {
                E.cy--;
                E.cx = static_cast<int>(line_len(E.cy));
            }
            break;
        case 'l':
        case ARROW_RIGHT:
            if(E.cx < static_cast<int>(line.size())) {
                E.cx = static_cast<int>(utf8_next(line, E.cx));
            } else if(E.cy < last) {
                E.cy++;
                E.cx = 0;
            }
            break;
        case 'k':
        case ARROW_UP:
            if(E.cy > 0) E.cy--;
            break;
        case 'j':
        case ARROW_DOWN:
            if(E.cy < last) E.cy++;
            break;
    }
    line = E.pt.line_text(E.cy);
    E.cx = snap_cx(line, std::min(E.cx, static_cast<int>(line.size())));
    E.want_rx = cx_to_rx(line, E.cx);
}

void update_want() { E.want_rx = cx_to_rx(E.pt.line_text(E.cy), E.cx); }

// j / k keep a goal column, so passing over a short line doesn't lose it
void move_vertical(int dir, int count) {
    const int target =
        std::clamp(E.cy + dir * std::max(count, 1), 0, E.num_rows() - 1);
    if(target == E.cy) return;
    E.cy = target;
    const std::string line = E.pt.line_text(E.cy);
    const int size = static_cast<int>(line.size());
    int cx = E.want_rx == INT_MAX ? size : rx_to_cx(line, E.want_rx);
    if(cx >= size && size > 0)  // normal mode: stop on the last char
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

size_t utf8_seq_len(char lead) {
    const auto b = static_cast<unsigned char>(lead);
    return b >= 0xF0 ? 4 : b >= 0xE0 ? 3 : b >= 0xC0 ? 2 : 1;
}

struct NormalState {
    int count = 0;           // 0 = nothing typed yet
    std::string keys;        // chord typed so far: "g", "gg", ...
    bool want_char = false;  // waiting for the char argument of `action`
    std::string action;      // the wants_char action that is waiting
    std::string arg;         // that char's bytes (UTF-8 arrives 1 byte/key)
    bool shown = false;      // pending input is on the status line

    void reset() {
        count = 0;
        keys.clear();
        want_char = false;
        action.clear();
        arg.clear();
        if(shown) E.status_message.clear();
        shown = false;
    }
};
NormalState N;

void show_pending() {
    editor_set_status_message(
        "{}{}", N.count ? std::to_string(N.count) : std::string(), N.keys);
    N.shown = true;
}

}  // namespace

void normal_reset() { N.reset(); }

// Every normal-mode key lands here. What a key *does* is looked up in the
// registry: keys -> action name -> behaviour.
void normal_feed(int c) {
    if(c == '\x1b' || c >= 0x100) {  // Esc, arrows, page keys cancel a chord
        N.reset();
        return;
    }

    if(N.want_char) {  // second half of f/t/r...: collect one UTF-8 character
        if(N.arg.empty() && c < 32) {  // <CR>, <Tab>... cancel it
            N.reset();
            return;
        }
        N.arg += static_cast<char>(c);
        if(N.arg.size() < utf8_seq_len(N.arg[0])) return;
        Ctx ctx{N.count, N.arg};
        const std::string action = N.action;
        N.reset();  // reset first, so the action can set its own status message
        if(!registry().run_action(action, ctx))
            editor_set_status_message("No such action: {}", action);
        return;
    }

    // a count: 1-9 starts one, 0 only continues one (bare 0 is a motion)
    if(N.keys.empty() &&
       ((c >= '1' && c <= '9') || (c == '0' && N.count > 0))) {
        N.count = std::min(N.count * 10 + (c - '0'), 1000000);
        show_pending();
        return;
    }

    N.keys += static_cast<char>(c);
    const Hit hit = registry().lookup(EditorMode::NORMAL, N.keys);
    switch(hit.kind) {
        case Lookup::None:  // not bound to anything: swallow it
            N.reset();
            return;
        case Lookup::Prefix:  // "g" is the start of "gg": wait for more
            show_pending();
            return;
        case Lookup::Exact:
            // also the start of a longer chord ("<space>" vs "<space>f"):
            // wait. There is no timeout yet, so Esc cancels.
            if(hit.longer) {
                show_pending();
                return;
            }
            if(registry().wants_char(hit.action)) {
                N.action = hit.action;
                N.want_char = true;
                show_pending();
                return;
            }
            Ctx ctx{N.count, {}};
            N.reset();
            if(!registry().run_action(hit.action, ctx))
                editor_set_status_message("No such action: {}", hit.action);
            return;
    }
}
