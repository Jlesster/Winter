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
#include <string_view>
#include "options.hpp"

struct Icons {
    std::string_view dir, dir_open, file;
};

struct BorderSet {
    std::string_view lt, t, tr, l, r, bl, b, br;
};

const Icons& icons();
const BorderSet& border_set(Border);
std::string_view file_icon(const std::string& ext);
std::optional<uint32_t> file_icon_color(const std::string& ext);
void tree_icon_set(std::string_view ext, std::string_view glyph);
