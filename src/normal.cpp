#include "normal.hpp"

#include <algorithm>
#include <climits>
#include <map>
#include <string>

#include "columns.hpp"
#include "editor.hpp"
#include "keys.hpp"
#include "motion.hpp"
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

// <normal-keys>
// normal mode input: [count] motion, multi-key chords, f/t arguments

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

static void apply_motion(const Target& t) {
    if(!t.ok) return;  // vim beeps, we just stay put
    set_cursor_offset(t.pos);
    if(t.to_eol) E.want_rx = INT_MAX;
    else update_want();
}

static size_t utf8_seq_len(char lead) {
    const auto b = static_cast<unsigned char>(lead);
    return b >= 0xF0 ? 4 : b >= 0xE0 ? 3 : b >= 0xC0 ? 2 : 1;
}

struct FindMemory {
    bool valid = false;
    motion::Find kind = motion::Find::f;
    std::string ch;
};

static FindMemory last_find;

struct NormalState {
    int count = 0;           // 0 = nothing typed yet
    std::string keys;        // chord typed so far: "g", "gg", ...
    bool want_char = false;  // next key is the target of f/F/t/T
    motion::Find find = motion::Find::f;
    std::string arg;     // that target's bytes (UTF-8 arrives 1 byte/key)
    bool shown = false;  // pending input is on the status line

    void reset() {
        count = 0;
        keys.clear();
        want_char = false;
        arg.clear();
        if(shown) E.status_message.clear();
        shown = false;
    }
};

static NormalState N;

// motions that only need the text
static const std::map<std::string, motion::Fn> kMotions = {
    {"h", motion::left},
    {"l", motion::right},
    {"0", motion::to_line_start},
    {"^", motion::to_first_nonblank},
    {"$", motion::to_line_end},
    {"w", motion::next_word},
    {"W", motion::next_WORD},
    {"b", motion::prev_word},
    {"B", motion::prev_WORD},
    {"e", motion::end_word},
    {"E", motion::end_WORD},
    {"gg", motion::to_file_start},
    {"G", motion::to_file_end},
    {"{", motion::para_back},
    {"}", motion::para_fwd},
    {"%", motion::match_pair},
};

// commands that need editor state (the next step adds d, c, y, p, x here)
using CmdFn = void (*)(int count);

static const std::map<std::string, CmdFn> kCommands = {
    {"j", +[](int n) { move_vertical(+1, n); }},
    {"k", +[](int n) { move_vertical(-1, n); }},
};

template <class M>
static bool has_longer(const M& m, const std::string& keys) {
    const auto it = m.lower_bound(keys);
    return it != m.end() && it->first.starts_with(keys);
}

static void show_pending() {
    editor_set_status_message(
        "{}{}", N.count ? std::to_string(N.count) : std::string(), N.keys);
    N.shown = true;
}

static void do_find(motion::Find k, const std::string& ch, int count,
                    bool repeat) {
    apply_motion(
        motion::find_char(E.pt, cursor_offset(), count, k, ch, repeat));
}

// returns true if the key was consumed here
bool normal_feed(int c) {
    if(c == '\x1b') {
        N.reset();
        return true;
    }
    if(c >= 0x100) {  // arrows, page keys: not ours, but they cancel a chord
        N.reset();
        return false;
    }

    if(N.want_char) {                  // second half of f/F/t/T
        if(N.arg.empty() && c < 32) {  // <CR>, <Tab>... cancel it
            N.reset();
            return true;
        }
        N.arg += static_cast<char>(c);
        if(N.arg.size() < utf8_seq_len(N.arg[0])) return true;
        last_find = {true, N.find, N.arg};
        do_find(N.find, N.arg, N.count, false);
        N.reset();
        return true;
    }

    // a count: 1-9 starts one, 0 only continues one (bare 0 is a motion)
    if(N.keys.empty() &&
       ((c >= '1' && c <= '9') || (c == '0' && N.count > 0))) {
        N.count = std::min(N.count * 10 + (c - '0'), 1000000);
        show_pending();
        return true;
    }

    N.keys += static_cast<char>(c);

    if(const auto m = kMotions.find(N.keys); m != kMotions.end()) {
        apply_motion(m->second(E.pt, cursor_offset(), N.count));
        N.reset();
        return true;
    }
    if(const auto m = kCommands.find(N.keys); m != kCommands.end()) {
        m->second(N.count);
        N.reset();
        return true;
    }
    if(N.keys.size() == 1) {
        switch(c) {
            case 'f':
                N.find = motion::Find::f;
                N.want_char = true;
                break;
            case 'F':
                N.find = motion::Find::F;
                N.want_char = true;
                break;
            case 't':
                N.find = motion::Find::t;
                N.want_char = true;
                break;
            case 'T':
                N.find = motion::Find::T;
                N.want_char = true;
                break;
            case ';':
            case ',':
                if(last_find.valid) {
                    const auto k = c == ',' ? motion::flip(last_find.kind)
                                            : last_find.kind;
                    do_find(k, last_find.ch, N.count, true);
                }
                N.reset();
                return true;
            default: break;
        }
        if(N.want_char) {
            show_pending();
            return true;
        }
    }

    // "g" alone is the start of "gg", so wait for the next key
    if(has_longer(kMotions, N.keys) || has_longer(kCommands, N.keys)) {
        show_pending();
        return true;
    }

    if(N.keys.size() == 1) {  // not a chord at all: let the old switch have it,
        N.keys.clear();       // and keep the count for it to read
        return false;
    }
    N.reset();  // "gz": a dead chord, swallow it
    return true;
}

void normal_reset() { N.reset(); }
