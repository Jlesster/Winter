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
#include "motion.hpp"
#include "registry.hpp"

void ext_operator(const std::string& name, Operator fn, std::string_view keys);
void ext_motion(const std::string& name, motion::Fn fn, std::string_view keys, bool wants_char);
void ext_object(const std::string& name, ObjectFn fn, std::string_view keys);
