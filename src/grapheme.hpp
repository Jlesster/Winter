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

#pragma once
// Extended grapheme clusters (UAX #29) and their terminal cell width.
//
// The editor stores bytes and addresses text by byte offset, so everything
// here is byte-based: a cluster is the run [i, gr_next(s, i)). Cursor motion,
// deletion and column math should step by clusters, never by code points,
// or an emoji ZWJ sequence / flag / Hangul syllable / "e" + accent turns
// into several cursor stops and the screen column drifts from the terminal's.
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

// End of the cluster starting at byte `i` (a cluster start). s.size() if i is
// at or past the end. Invalid UTF-8 bytes are one cluster each.
size_t gr_next(std::string_view s, size_t i);

// Start of the cluster that ends at or contains byte `i - 1`. 0 for i == 0.
// `i` need not be a boundary: the result is the start of the cluster `i`
// falls in (or the cluster before `i` when it is one).
size_t gr_prev(std::string_view s, size_t i);

// Snap any byte offset down to the start of its cluster.
size_t gr_snap(std::string_view s, size_t i);

// Cells a cluster occupies (0, 1 or 2). Not tab-aware, not control-aware:
// the caller owns '\t' and C0/C1 characters.
int gr_width(std::string_view cluster);

// Width of one code point on its own (0 for marks / zero-width format
// characters, 2 for East Asian Wide/Fullwidth, else 1).
int gr_cp_width(char32_t cp);

// Cluster pieces for the screen: first code point, and the rest of the
// cluster (marks, ZWJ chain, VS16 ...) as UTF-32, empty for a plain char.
struct GrCell {
    char32_t base = U' ';
    std::u32string tail;
    int width = 1;
    size_t len = 0;  // bytes consumed
};
GrCell gr_cell(std::string_view s, size_t i);

// Interned cluster tails, so a screen Cell can name one with a plain integer
// (and equal cells still compare equal frame to frame). 0 = no tail. The
// table is bounded; once full, new tails come back as 0 and are dropped.
uint32_t gr_tail_intern(std::u32string_view tail);
std::u32string_view gr_tail_get(uint32_t id);
