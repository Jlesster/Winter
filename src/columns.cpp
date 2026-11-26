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

#include "columns.hpp"

#include "core.hpp"
#include "grapheme.hpp"
#include "options.hpp"
#include "utf8.hpp"

bool is_ctl(char32_t cp) { return cp < 0x20 || (cp >= 0x7F && cp < 0xA0); }

int cell_width(char32_t cp, int rx) {
    if(cp == '\t') return opt_ts() - (rx % opt_ts());
    if(is_ctl(cp)) return 1;
    return utf8_width(cp);
}

int cluster_width(std::string_view line, size_t at, size_t end, int rx) {
    const Decoded d = utf8_decode(line, at);
    if(d.cp == '\t' || is_ctl(d.cp)) return cell_width(d.cp, rx);
    if(end - at == d.len) return utf8_width(d.cp);  // single code point
    return gr_width(line.substr(at, end - at));
}

// Everything below steps by grapheme cluster, the unit the terminal draws
// and the cursor sits on: "e" + accent, a flag, a ZWJ family or a Hangul
// syllable is one cursor stop and one run of cells.

int snap_cx(std::string_view line, int cx) {
    if(cx <= 0) return 0;
    if(cx >= static_cast<int>(line.size()))
        return static_cast<int>(line.size());
    return static_cast<int>(gr_snap(line, static_cast<size_t>(cx)));
}

int cx_to_rx(std::string_view line, int cx) {
    int rx = 0;
    for(size_t j = 0; j < line.size() && static_cast<int>(j) < cx;) {
        const size_t e = gr_next(line, j);
        rx += cluster_width(line, j, e, rx);
        j = e;
    }
    return rx;
}

int rx_to_cx(std::string_view line, int rx) {
    int cur = 0;
    size_t i = 0;
    while(i < line.size()) {
        const size_t e = gr_next(line, i);
        cur += cluster_width(line, i, e, cur);
        if(cur > rx) return static_cast<int>(i);
        i = e;
    }
    return static_cast<int>(i);
}

int text_width(std::string_view s) {
    int w = 0;
    for(size_t i = 0; i < s.size();) {
        const size_t e = gr_next(s, i);
        w += cluster_width(s, i, e, w);
        i = e;
    }
    return w;
}
