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
