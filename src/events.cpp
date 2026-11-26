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

#include "events.hpp"
#include <algorithm>

namespace {
struct Sub {
    int id;
    std::string name;
    EventHandler fn;
};
std::vector<Sub> g_subs;
Buffer* g_event_buf = nullptr;
int g_next_id = 1;
}  // namespace

int events_on(std::string name, EventHandler fn) {
    g_subs.push_back({g_next_id, std::move(name), std::move(fn)});
    return g_next_id++;
}

bool events_off(int id) {
    const auto it = std::find_if(g_subs.begin(),
                                 g_subs.end(),
                                 [id](const Sub& s) { return s.id == id; });
    if(it == g_subs.end()) return false;
    g_subs.erase(it);
    return true;
}

bool events_has(std::string_view name) {
    return std::any_of(g_subs.begin(), g_subs.end(),
                       [&](const Sub& s) { return s.name == name; });
}

void events_emit(std::string_view name, const std::vector<std::string>& args) {
    const std::vector<Sub> snapshot = g_subs;
    for(const Sub& s : snapshot) {
        if(s.name != name) continue;
        const bool alive =
            std::any_of(g_subs.begin(), g_subs.end(), [&](const Sub& t) {
                return t.id == s.id;
            });
        if(alive) s.fn(args);
    }
}

Buffer* events_buf() { return g_event_buf; }
EventBufScope::EventBufScope(Buffer* b) : prev_(g_event_buf) { g_event_buf = b; }
EventBufScope::~EventBufScope() { g_event_buf = prev_; }
