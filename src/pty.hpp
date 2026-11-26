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
// A child process on a pseudo-terminal, driven by the editor loop.
//
// pty_open() forks the child on a fresh PTY and watches the master with
// loop_watch(). Output arrives through on_data in bounded bursts, so one noisy
// process cannot starve the editor. on_exit fires once, after the last on_data.
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using PtyId = int;

struct PtySpec {
    std::vector<std::string> argv;  // empty = $SHELL, else /bin/sh
    std::string cwd;                // empty = inherit
    int rows = 24, cols = 80;       // initial window size
    std::vector<std::pair<std::string, std::string>> env;  // added or replaced
    std::function<void(std::string_view)> on_data;
    std::function<void(int)> on_exit;  // exit code, or 128 + signal
};

PtyId pty_open(PtySpec spec);  // 0 on failure
// Queued when the child is not reading; false when the backlog is full or the
// id is gone.
bool pty_write(PtyId id, std::string_view data);
void pty_resize(PtyId id, int rows, int cols);  // TIOCSWINSZ -> SIGWINCH
// Hangs up the child (SIGHUP, then SIGKILL if it lingers) and forgets it.
// on_exit is not called.
void pty_close(PtyId id);
void pty_kill_all();  // editor quit
