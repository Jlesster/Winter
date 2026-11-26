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

void hover_show();
void completion_post_key(int c);
void completion_close();
bool completion_active();
void completion_register_commands();

// The K hover popup keeps one real window (own read-only buffer) per pane, so
// a focused pane is driven by ordinary normal mode. render.cpp draws them.
#include <string>
#include <vector>
#include "overlay.hpp"
#include "window.hpp"

// A styled run of one pane line, in cells.
struct HoverSeg {
    int c0 = 0, c1 = 0;
    Style st;
    std::string url;  // a link's target; empty = plain text
};
// Runs of line `line` of hover pane `p`; null if `p` isn't a pane.
const std::vector<HoverSeg>* hover_line_segs(const win::Win* p, int line);

win::Win* hover_live();  // the focused pane while the popup has focus
void hover_dismiss();    // close the popup (window and buffer commands call it)
void hover_tick();       // once per frame, before drawing
// Panes of the open popup with their screen rects set; empty if none.
std::vector<win::Win*> hover_layout(const Viewport& vp);
