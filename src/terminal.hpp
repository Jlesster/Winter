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
// Raw mode, key input, terminal size, SIGWINCH and cursor shape.

#include <cstdint>
#include <string>
#include "core.hpp"

struct TermCaps {
    bool scroll_region = false;  // DECSTBM scroll optimisation
    bool cursor_shapes =
        false;  // DECSCUSR (neither exists on the linux console)
};
const TermCaps& term_caps();  // valid after enable_raw_mode()

[[noreturn]] void die(const char* s);
void editor_quit();
void enable_raw_mode();
void disable_raw_mode();
int editor_read_key(bool want_mods = false);  // blocks; returns an EditorKey
                                              // (keys.hpp) or a byte
int editor_read_key_timeout(bool want_mods, int timeout_ms);
std::string editor_take_paste();

int get_window_size(int* rows, int* cols);
int query_window_size(int* rows, int* cols);  // ioctl only, never blocks

void install_winch_handler();
bool resize_pending();
void resize_ack();

void set_cursor_color(uint32_t rgb);
void set_cursor_for_mode(EditorMode m);  // no-op without DECSCUSR
