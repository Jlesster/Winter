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
#include <string_view>

bool is_ctl(char32_t cp);
int cell_width(char32_t cp, int rx);        // rx = column the char starts at
// cells of the grapheme cluster line[at, end) drawn at column rx (tab / control aware)
int cluster_width(std::string_view line, size_t at, size_t end, int rx);
int snap_cx(std::string_view line, int cx); // snap a byte offset to a char start
int cx_to_rx(std::string_view line, int cx);
int rx_to_cx(std::string_view line, int rx);
int text_width(std::string_view s);
