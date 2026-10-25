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
