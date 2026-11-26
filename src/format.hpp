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
#include <string>

struct Buffer;

void format_init();  // after script_init(), before script_load_config()

// Run the plan for this buffer now. verbose = report in the status line.
// true if the text changed.
bool format_buffer(Buffer& b, bool verbose);

// Save path: never blocks a save; failures are returned as a note.
void format_on_save(Buffer& b);
std::string format_take_note();  // "" or ", format failed: ..." (clears it)
