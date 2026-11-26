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
#include <tree_sitter/api.h>
#include <cstddef>
#include <cstdint>
#include <functional>
#include "piece.hpp"

struct Pos16 {
    uint32_t line;
    uint32_t col;
};

struct EditEvent {
    size_t pos;
    size_t old_len;
    size_t new_len;
    TSPoint start_pt, old_end_pt, new_end_pt;
    Pos16 start16, old_end16;
    uint64_t version;
    Origin origin;
    const PieceTable* src = nullptr;
};

namespace prio {
constexpr int treesitter = 10;
constexpr int snippet = 20;
constexpr int decor = 30;
}  // namespace prio

using EditFn = std::function<void(const EditEvent&)>;
using GroupFn = std::function<void()>;

void edit_subscribe(int priority, EditFn fn);
void edit_group_end_subscribe(GroupFn fn);
void edit_defer(std::function<void()> fn);
void edit_emit(const EditEvent& ev);
void edit_group_end();
EditEvent make_event(const PieceTable& pt, const TextEdit& e, Origin o);
