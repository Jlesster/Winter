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
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include "buffer.hpp"

// Style overrides set from Lua. Every consumer passes its built-in look as
// the default, so with no overrides the editor looks exactly as before.
std::optional<Style> theme_override(std::string_view group);
Style theme_ui(std::string_view group, Style dflt);
uint32_t theme_fg(std::string_view group, uint32_t dflt);  // fg of a group

void theme_set(std::string_view group, const Style& st);
bool theme_unset(std::string_view group);  // true if it existed
void theme_clear();                        // drop every override + rainbow

void theme_set_rainbow(std::vector<uint32_t> colors);
std::span<const uint32_t> theme_rainbow();  // empty = built-in palette

void theme_link(std::string_view group, std::string_view target);
bool theme_unlink(std::string_view group);
const std::vector<std::string_view>& theme_ui_groups();
uint64_t theme_rev();  // bumps on every change
