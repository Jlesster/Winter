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
#include <poll.h>
#include <functional>

using TimerId = int;

void loop_init();
void loop_wake();
void loop_on_wake(std::function<void()> fn);
void loop_post(std::function<void()> fn);

TimerId loop_after(int ms, std::function<void()> fn);
void loop_cancel(TimerId id);

void loop_watch(int fd, short events, std::function<void(short)> cb);
void loop_unwatch(int fd);

bool loop_wait(int timeout_ms);

// Nested wait for code that must block briefly (format-on-save): runs timers,
// tasks and watchers but never looks at stdin, so typed-ahead keys can't make
// it spin. True if done() became true before the timeout.
bool loop_pump_until(const std::function<bool()>& done, int timeout_ms);
