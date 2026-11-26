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
#include <string_view>
#include "indent_model.hpp"
#include "piece.hpp"
#include "syntax.hpp"

namespace indent {
std::string unit();
std::string_view leading_ws(std::string_view line);
std::string ws_for_cols(int cols);
std::string for_newline(std::string_view before);
std::string for_open_above(std::string_view line);
void reindent_lines(Model& m, Syntax& syn, PieceTable& pt, bool eol, size_t l0,
                    size_t l1);
std::optional<std::string> from_tree(Model& m, Syntax& syn, PieceTable& pt,
                                     bool eol, size_t line);
bool is_label(std::string_view tok);
}  // namespace indent
