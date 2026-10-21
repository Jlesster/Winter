#include "editor.hpp"
#include "events.hpp"

EditorConfig E;

// row helpers

size_t line_len(size_t y) { return E.pt.line_end(y) - E.pt.line_start(y); }

// cursor offset modes

size_t cursor_offset() { return E.pt.from_linecol(E.cy, E.cx); }

void set_cursor_offset(size_t pos) {
    const LineCol lc = E.pt.to_linecol(pos);
    E.cy = static_cast<int>(lc.line);
    E.cx = static_cast<int>(lc.col);
}

const char* mode_name(EditorMode m) {
    switch(m) {
        case INSERT: return "insert";
        case NORMAL: return "normal";
        case VISUAL: return "visual";
        case COMMAND: return "command";
        case SEARCH: return "search";
    }
    return "?";
}

void set_mode(EditorMode m) {
    if(E.mode == m) return;
    if(E.mode == INSERT) E.pt.end_group();
    if(m == INSERT) E.pt.begin_group();
    const EditorMode old = E.mode;
    E.mode = m;
    events_emit("mode_changed", {mode_name(old), mode_name(m)});
}
