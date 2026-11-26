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
#include <string_view>
#include <vector>

struct Buffer;

using EventHandler = std::function<void(const std::vector<std::string>&)>;
int events_on(std::string name, EventHandler fn);
bool events_off(int id);
bool events_has(std::string_view name);  // any subscriber? (cheap, for hot paths)
void events_emit(std::string_view name,
                 const std::vector<std::string>& args = {});

// The buffer an event is about, for handlers that touch buffer-local state.
// nullptr outside a scope: callers fall back to the current buffer.
Buffer* events_buf();
class EventBufScope {
    Buffer* prev_;

public:
    explicit EventBufScope(Buffer* b);
    ~EventBufScope();
    EventBufScope(const EventBufScope&) = delete;
    EventBufScope& operator=(const EventBufScope&) = delete;
};
