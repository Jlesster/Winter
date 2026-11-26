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
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

std::string state_dir();
bool state_append(std::string_view name, std::string_view line);
bool state_rewrite(std::string_view name,
                   const std::vector<std::string>& lines);
std::vector<std::string> state_read_lines(std::string_view name);
uint64_t state_hash(std::string_view s, uint64_t h = 0xcbf29ce484222325ULL);

using HistoryMap = std::map<std::string, std::vector<std::string>>;
HistoryMap state_history_load();
void state_history_append(std::string_view prompt, std::string_view entry);

std::string state_file_key(const std::string& file);
bool state_write_blob(std::string_view name, std::string_view data);
std::optional<std::string> state_read_blob(std::string_view name);
void state_remove(std::string_view name);

// recently entered files, newest first (absolute keys, existing files only)
void recent_touch(const std::string& file);
std::vector<std::string> recent_list(size_t max);

void cursor_remember(const std::string& file, int line, int col);
bool cursor_recall(const std::string& file, int& line, int& col);
