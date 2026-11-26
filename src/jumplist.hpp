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
#include <string>

// Ctrl-o / ctrl-i style position history, shared by every "go somewhere else"
// feature (LSP goto, pickers, quickfix).
void jump_push();            // record the current spot; call BEFORE moving away
void jump_older(int count);  // <C-o>
void jump_newer(int count);  // <C-i>

// '{x} (exact = false: the mark's line) and `{x} (its exact spot). Records the
// current spot first. False, with a message, when the mark is not set.
bool jump_to_mark(char name, bool exact);

// buffer_open + verify that `path` really is the current buffer afterwards.
bool jump_open(const std::string& path);
// open `path` and put the cursor at (0-based line, byte column). No history.
bool jump_goto(const std::string& path, size_t line, size_t col);
