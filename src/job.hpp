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
