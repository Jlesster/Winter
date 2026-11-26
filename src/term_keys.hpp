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
// Winter key ints (keys.hpp) -> the bytes a program on a PTY expects.
//
// Pure function, no editor state: the panel passes in the two emulator modes
// that change the encoding. An empty result means "nothing to send" (NONE,
// PASTE, a key with no encoding).
#include <string>

struct TermKeyMode {
    bool app_cursor =
        false;  // DECCKM: arrows/Home/End send ESC O x, not ESC [ x
};

std::string term_encode_key(int key, TermKeyMode mode = {});
