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
#include <string_view>
#include <string>
#include <cstddef>

enum class NavKind {
    Definition,
    Declaration,
    TypeDefinition,
    Implementation,
    References,
};

bool nav_kind_from_name(std::string_view name, NavKind& out);

void lsp_goto(NavKind kind);
void lsp_switch_source_header();
void lsp_nav_register_commands();

void lsp_show_location(const std::string& path,
                       size_t line, size_t ch, bool utf16);
