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
#include <optional>
#include <string>
#include <string_view>

using JobId = int;

struct JobResult {
    std::string out;
    int code = 0;
};

struct JobSpec {
    std::string cmd;
    std::optional<std::string> input;
    bool pipe_stdin = false;
    bool keep_out = true;
    bool split_err = false;
    std::function<void(std::string_view)> on_data;
    std::function<void(std::string_view)> on_err;
    std::function<void(JobResult)> on_exit;
};

void job_init();
JobId job_start(JobSpec spec);
bool job_write(JobId id, std::string_view data);
void job_close_stdin(JobId id);
void job_kill(JobId id);
void job_kill_all();
