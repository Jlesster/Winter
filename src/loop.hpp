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
