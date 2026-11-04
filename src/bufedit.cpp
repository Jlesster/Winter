#include "bufedit.hpp"

#include "editor.hpp"
#include "motion.hpp"
#include "replace.hpp"

bool buffer_replace_text(Buffer& b, std::string_view text, Origin o) {
    const bool cur = &b == &E.buf();
    Caret c = cur ? Caret{static_cast<size_t>(E.cy), static_cast<size_t>(E.cx)}
                  : Caret{static_cast<size_t>(b.cy), static_cast<size_t>(b.cx)};
    const bool changed = pt_replace(b.pt, b.eol, text, o, &c);
    if(!changed) return false;
    if(cur) {
        size_t off = b.pt.from_linecol(c.line, c.col);
        if(E.mode == NORMAL || E.mode == FILES)
            off = motion::clamp_normal(b.pt, off);
        set_cursor_offset(off);
    } else {
        b.cy = static_cast<int>(c.line);
        b.cx = static_cast<int>(c.col);
    }
    return true;
}
