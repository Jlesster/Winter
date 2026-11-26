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

struct CommentCont {
    std::string prefix;
    bool lone;
};

struct CommentStyle {
    std::string line, open, close;
};

void comment_register(std::string lang, CommentStyle s);
const CommentStyle* comment_style(std::string_view lang);
void comment_toggle(size_t l0, size_t l1);
std::optional<CommentCont> comment_continue(size_t line, size_t cx, bool above);
