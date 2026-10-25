#include "editor.hpp"
#include <cstddef>
#include "core.hpp"
#include "events.hpp"
#include "loop.hpp"
#include "motion.hpp"

EditorConfig E;

// row helpers

size_t line_len(size_t y) {
    return E.buf().pt.line_end(y) - E.buf().pt.line_start(y);
}

// cursor offset modes

size_t cursor_offset() { return E.buf().pt.from_linecol(E.cy, E.cx); }

void set_cursor_offset(size_t pos) {
    const LineCol lc = E.buf().pt.to_linecol(pos);
    E.cy = static_cast<int>(lc.line);
    E.cx = static_cast<int>(lc.col);
}

const char* mode_name(EditorMode m) {
    switch(m) {
        case INSERT: return "insert";
        case NORMAL: return "normal";
        case VISUAL: return "visual";
        case SEARCH: return "search";
        case COMMAND: return "command";
        case OPPEND: return "operator";
    }
    return "?";
}

void set_mode(EditorMode m) {
    if(E.mode == m) return;
    if(E.mode == VISUAL && m == NORMAL) {
        const size_t a = std::min(E.v_anchor, cursor_offset());
        const size_t b = std::max(E.v_anchor, cursor_offset());
        E.vm_first = E.buf().pt.line_of(a);
        E.vm_last = E.buf().pt.line_of(b);
        E.vm_valid = true;
        const size_t p = motion::clamp_normal(E.buf().pt, cursor_offset());
        set_cursor_offset(p);
    }
    if(E.mode == INSERT) E.buf().pt.end_group();
    if(m == INSERT) E.buf().pt.begin_group();
    const EditorMode old = E.mode;
    E.mode = m;
    events_emit("mode_changed", {mode_name(old), mode_name(m)});
}

void editor_status_touched() {
    static TimerId id = 0;
    if(id) loop_cancel(id);
    id = loop_after(5100, [] {});
}
