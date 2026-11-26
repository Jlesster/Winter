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
#include <vector>

struct FmtOut {
    bool ok = false;       // exited 0 in time
    bool timeout = false;
    int code = 0;
    std::string out, err;
};

// stdin -> stdout filter, run in `cwd` (empty = inherit). Blocks (pumping the
// loop, not stdin) for at most timeout_ms, then kills the process group.
FmtOut run_formatter(const std::vector<std::string>& argv,
                     const std::string& cwd, const std::string& input,
                     int timeout_ms);
