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
#include <functional>
#include <string>

using WatchId = int;

enum : unsigned {
    WATCH_STRUCT = 1,   // entry created / deleted / renamed in the dir
    WATCH_CONTENT = 2,  // entry written (closed after write) or attrs changed
    WATCH_SELF = 4,     // the watched dir itself went away
};

using WatchFn = std::function<void(const std::string& name, unsigned kind)>;

void watch_init();  // after loop_init()
WatchId watch_dir(const std::string& dir, WatchFn fn);  // 0 on failure
void watch_remove(WatchId id);
