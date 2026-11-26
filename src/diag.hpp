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
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include "json.hpp"
#include "piece.hpp"
#include "virt.hpp"

struct Diag {
    size_t start = 0, end = 0;
    int severity = 1;
    std::string msg, source;
    Json raw;
};

size_t lsp_offset(const PieceTable& pt, int64_t line, int64_t character,
                  bool utf16);
// inverse lsp offset
size_t lsp_col(const PieceTable& pt, size_t offset, bool utf16);
std::vector<Diag> diag_from_lsp(const PieceTable& pt, const Json& list,
                                bool utf16);
void diag_shift(std::vector<Diag>& v, const TextEdit& e);
void diag_spans(const std::vector<Diag>& v, size_t ls, size_t len,
                std::vector<LineSpan>& out);
const Diag* diag_worst_on_line(const std::vector<Diag>& v, size_t ls,
                               size_t len);
std::string diag_summary(const std::vector<Diag>& v);
uint32_t diag_color(int sev);
const char* diag_name(int sev);
std::string_view diag_first_line(const Diag& d);
void diag_jump(int dir, int count);
// Picker over the diagnostics of the current buffer, or (workspace) of every
// open buffer plus whatever the servers published for files that are not open.
void diag_picker(bool workspace);
