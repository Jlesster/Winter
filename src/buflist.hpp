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
#include <optional>
#include <string>
#include <string_view>

struct Buffer;

void buffer_switch(size_t idx);
void buffer_entered(
    Buffer& b);  // buf_enter event + recent files, for window focus
size_t buffer_open(const std::string& path);
Buffer* buffer_get(const std::string& path);
bool buffer_close(size_t idx, bool force);
void buffer_cycle(int step);
void buffer_goto_alt();
bool buffer_goto(std::string_view arg);
std::optional<size_t> buffer_first_modified();
std::string buffer_name(size_t i);
std::string buffer_list();
void buffer_remember_cursor();
void buffer_show_text(const std::string& name, const std::string& text);
