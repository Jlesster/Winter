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
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "errparse.hpp"

struct QfCounts {
    size_t errors = 0, warnings = 0, notes = 0;
};

// One global list. Producers (:make, :cbuffer, later :grep / LSP) call
// qf_begin() and then qf_add() as results arrive, or qf_set() with all of them.
void qf_begin(std::string title);
void qf_add(QfEntry e);  // drops an entry identical to the previous one
void qf_set(std::string title, std::vector<QfEntry> entries);

// Attaches a context line to the newest entry. A caret line (col > 0) also
// fills the column a header lacked (javac) and the end column.
void qf_context(std::string_view text, int col, int end_col);

size_t qf_size();
QfCounts qf_counts();
bool qf_jump(size_t idx);  // opens the file, moves the cursor, reports in the status line
bool qf_jump_first();      // first entry that isn't a note; false if there is none

// `file` as a tool printed it, made absolute against `base` (the directory the
// tool ran in) and normalised. The file may not exist.
std::string qf_resolve_path(std::string_view file, const std::filesystem::path& base);

void qf_register_commands();  // :cn :cp :cc :cfirst :clast :copen :cclose :cbuffer
