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
#include <functional>
#include <string>
#include <string_view>
#include <vector>

struct PickItem {
    std::string text;
    std::string detail;
    size_t id = 0;
    std::string path;
    int line = 0, col = 0;
};

struct PickerSpec {
    std::string prompt;
    bool fuzzy = true;
    bool preview = false;
    bool paths = false;  // items are file paths: icon + dimmed directory
    int max_results = 200;
    std::string query;  // text the input starts with
    std::function<void(std::string_view)> on_query;
    std::function<void(const PickItem&)> on_accept;
    std::function<void()> on_close;
};

void picker_open(PickerSpec spec);
void picker_close();
bool picker_active();
unsigned picker_serial();
bool picker_add(std::vector<PickItem> items);
void picker_reset();

void picker_files();
void picker_grep();
void picker_recent();
void picker_register_commands();
