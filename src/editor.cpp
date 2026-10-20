#include "editor.hpp"

EditorConfig E;

size_t line_len(size_t y) { return E.pt.line_end(y) - E.pt.line_start(y); }

size_t cursor_offset() { return E.pt.from_linecol(E.cy, E.cx); }

void set_cursor_offset(size_t pos) {
    const LineCol lc = E.pt.to_linecol(pos);
    E.cy = static_cast<int>(lc.line);
    E.cx = static_cast<int>(lc.col);
}

void set_mode(EditorMode m) {
    if(E.mode == m) return;
    if(E.mode == INSERT) E.pt.end_group();
    if(m == INSERT) E.pt.begin_group();
    E.mode = m;
}
