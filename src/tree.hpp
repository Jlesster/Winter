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
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include "buffer.hpp"

struct TreeSeg {
    std::string text;
    Style st;
};

struct TreeLine {
    std::vector<TreeSeg> segs;
    bool selected = false;
    bool visual = false;  // inside the v range (cursor row is `selected`)
};

void tree_toggle();
bool tree_visible();
bool tree_focused();
void tree_focus(bool on);  // no-op while the tree is hidden
bool tree_visual();         // a v range is active in the tree
bool tree_visual_cancel();  // ends it; false if there was none
void tree_layout(int total_cols);
int tree_cols();
bool tree_key(int c);
int tree_cursor_row();
const std::vector<TreeLine>& tree_view(int rows);
void tree_move(long d);    // relative, clamped
void tree_goto(long row);  // absolute 0-based row, clamped
