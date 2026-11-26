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

bool script_init();
void script_shutdown();

std::string script_config_dir();
void script_load_config();  // startup: remember the built-ins, run init.lua,
                            // watch it

bool script_source(std::string_view path);  // run one Lua file, nothing reset
bool script_reload();        // reset what the config set up, re-run init.lua
void script_reload_later();  // same, from the event loop (safe from any
                             // callback)

std::string script_eval(std::string_view code);
const std::string& script_last_error();
