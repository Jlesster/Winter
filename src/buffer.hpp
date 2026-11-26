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
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct Style {
    uint32_t fg = 0xFFFFFFFF;
    uint32_t bg = 0xFFFFFFFF;
    uint16_t attrs = 0;

    uint32_t ul = 0xFFFFFFFF;

    bool operator==(const Style&) const = default;
};

struct Span {
    size_t start, end;
    Style st;
};

struct Cell {
    char32_t chr = U' ';
    uint8_t width = 1;
    Style style{};
    char32_t mark = 0;
    uint32_t tail = 0;  // interned rest of a grapheme cluster (grapheme.hpp)
    bool operator==(const Cell&) const = default;
};

enum : uint16_t {
    ATTR_BOLD = 1 << 0,
    ATTR_DIM = 1 << 1,
    ATTR_ITALIC = 1 << 2,
    ATTR_UNDERLINE = 1 << 3,
    ATTR_REVERSE = 1 << 4,
    ATTR_UNDERCURL = 1 << 5,
    ATTR_STRIKE = 1 << 6,
    ATTR_NODIM = 1 << 7,  // patch-only: clears ATTR_DIM of what is below
};

class Screen {
    int w = 0, h = 0;
    std::vector<Cell> front, back;
    std::string out;
    int cur_x = 0, cur_y = 0;
    int scroll_rows = 0;
    // While a viewport is set, at() / width() / height() are relative to it
    // and writes outside it land in a scratch cell: the window code draws
    // with its own coordinates and cannot touch its neighbours.
    bool vp_on = false;
    int vp_x = 0, vp_y = 0, vp_w = 0, vp_h = 0;
    Cell scratch;

    void try_scroll();

public:
    void set_scroll_rows(int n) { scroll_rows = n; }
    void resize(int nw, int nh) {
        w = nw;
        h = nh;
        back.assign(w * h, Cell{});
        front.assign(w * h, Cell{U'\0'});
    }
    Cell& at(int x, int y) {
        if(!vp_on) return back[y * w + x];
        if(x < 0 || y < 0 || x >= vp_w || y >= vp_h) return scratch = Cell{};
        return back[(y + vp_y) * w + x + vp_x];
    }
    void set_viewport(int x, int y, int vw, int vh) {
        vp_on = true;
        vp_x = x;
        vp_y = y;
        vp_w = std::max(vw, 0);
        vp_h = std::max(vh, 0);
    }
    void clear_viewport() { vp_on = false; }
    void cursor(int x, int y) {
        cur_x = x;
        cur_y = y;
    }
    int width() const { return vp_on ? vp_w : w; }
    int height() const { return vp_on ? vp_h : h; }
    void clear() { back.assign(w * h, Cell{}); }
    void invalidate() { front.assign(front.size(), Cell{U'\0'}); }
    void flush();
};
