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
#include "piece.hpp"

struct Caret {
    size_t line = 0, col = 0;  // col in bytes
};

// Make `pt` hold `text` (file bytes: one trailing '\n' is folded into `eol`).
// Applied as minimal LINE hunks inside one undo group, bottom-up, so
// tree-sitter/LSP/decos/anchors see small edits and the caret stays put.
// `caret` (optional) is mapped through the diff. True if the text changed.
bool pt_replace(PieceTable& pt, bool& eol, std::string_view text, Origin o,
                Caret* caret = nullptr);
