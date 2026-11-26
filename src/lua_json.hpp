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
#include <lua.hpp>
#include <optional>
#include <string>
#include "json.hpp"

struct lua_State;

void lua_push_json(lua_State* L, const Json& j);
bool lua_to_json(lua_State* L, int idx, Json& out, std::string& err);
std::optional<Json> lua_load_config_file(const std::string& path,
                                         std::string& err);
