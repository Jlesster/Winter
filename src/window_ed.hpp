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
// Editor side of the window layout (window.hpp): moving the view registers
// (E.cx, E.cy, E.row_offset ...) in and out of the window being drawn or
// focused, focus travel across the tree, the output dock and tmux, and the
// :split family.
#include "window.hpp"

struct Buffer;

void windows_ensure();  // the first window, from the editor's current view
void win_enter_view(win::Win* w);        // window -> E (cursor, scroll, buffer)
void win_leave_view(win::Win* w);        // E -> window
void win_apply_rect(const win::Win* w);  // E.term_rows / E.term_cols
void win_clamp_view();  // keep a parked window's cursor inside its (edited)
                        // buffer
bool win_resize(win::Dir d, int count);
void win_focus_window(win::Win* w);  // a user-level focus change
bool win_navigate(win::Dir d);     // CTRL-W h j k l: windows, tree, dock, tmux
bool win_do(char key, int count);  // the rest of CTRL-W
bool win_split(bool vertical, bool before = true);  // the new window takes focus
bool win_close_current(bool force);  // false when it was the last window
void win_only();  // close every window but the focused one

void tmux_announce(bool on);

// Called by buflist.cpp
void win_buffer_changed();        // the focused window got another buffer
void win_buffer_gone(Buffer* b);  // about to be erased
void win_buffer_replaced(Buffer* from, Buffer* to);

// Called by render.cpp each frame
void win_set_area_rows(int rows);
int win_area_rows();              // rows above the output dock and message bar
void win_set_hint(int x, int y);  // the cursor's screen cell

void windows_register_commands();
