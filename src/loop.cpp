#include "loop.hpp"
#include <fcntl.h>
#include <unistd.h>
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <deque>
#include <functional>
#include <vector>
#include "terminal.hpp"

namespace {
using Clock = std::chrono::steady_clock;
struct Timer {
    TimerId id;
    Clock::time_point due;
    std::function<void()> fn;
};
struct Watch {
    int fd;
    short events;
    std::function<void(short)> cb;
};

int g_wake[2] = {-1, -1};
TimerId g_next_timer = 0;
std::vector<Timer> g_timers;
std::vector<Watch> g_watch;
std::deque<std::function<void()>> g_tasks;
std::function<void()> g_wake_hook;
}  // namespace

void loop_init() {
    if(pipe2(g_wake, O_NONBLOCK | O_CLOEXEC) == -1) die("pipe2");
}

void loop_wake() {
    const int saved = errno;
    const char b = 1;
    (void)!write(g_wake[1], &b, 1);
    errno = saved;
}

void loop_on_wake(std::function<void()> fn) { g_wake_hook = std::move(fn); }

void loop_post(std::function<void()> fn) {
    g_tasks.push_back(std::move(fn));
    loop_wake();
}

TimerId loop_after(int ms, std::function<void()> fn) {
    g_timers.push_back({++g_next_timer,
                        Clock::now() + std::chrono::milliseconds(ms),
                        std::move(fn)});
    return g_next_timer;
}

void loop_cancel(TimerId id) {
    std::erase_if(g_timers, [id](const Timer& t) { return t.id == id; });
}

void loop_watch(int fd, short events, std::function<void(short)> cb) {
    loop_unwatch(fd);
    g_watch.push_back({fd, events, std::move(cb)});
}

void loop_unwatch(int fd) {
    std::erase_if(g_watch, [fd](const Watch& w) { return w.fd == fd; });
}

namespace {

int next_timer_ms() {
    if(g_timers.empty()) return -1;
    const auto first = std::ranges::min_element(g_timers, {}, &Timer::due);
    const auto us = std::chrono::duration_cast<std::chrono::microseconds>(
                        first->due - Clock::now())
                        .count();
    return us <= 0 ? 0 : static_cast<int>((us + 999) / 1000);
}

bool run_timers() {
    const auto now = Clock::now();
    std::vector<Timer> due;
    for(auto it = g_timers.begin(); it != g_timers.end();) {
        if(it->due <= now) {
            due.push_back(std::move(*it));
            it = g_timers.erase(it);
        } else ++it;
    }
    std::ranges::sort(due, {}, &Timer::due);
    for(auto& t : due) t.fn();
    return !due.empty();
}

bool run_tasks() {
    std::deque<std::function<void()>> batch;
    batch.swap(g_tasks);
    for(auto& t : batch) t();
    return !batch.empty();
}

}  // namespace

bool loop_wait(int timeout_ms) {
    for(;;) {
        int t = timeout_ms;
        if(const int n = next_timer_ms(); n >= 0 && (t < 0 || n < t)) t = n;
        if(!g_tasks.empty()) t = 0;
        std::vector<pollfd> fds;
        fds.push_back({STDIN_FILENO, POLLIN, 0});
        fds.push_back({g_wake[0], POLLIN, 0});
        for(const Watch& w : g_watch) fds.push_back({w.fd, w.events, 0});
        const int r = poll(fds.data(), fds.size(), t);
        if(r < 0 && errno != EINTR) die("poll");
        bool fired = false;
        if(r > 0) {
            if(fds[1].revents) {
                char junk[64];
                while(read(g_wake[0], junk, sizeof junk) > 0) {}
                if(g_wake_hook) g_wake_hook();
                fired = true;
            }
            for(size_t i = 2; i < fds.size(); ++i) {
                if(!fds[i].revents) continue;
                const int fd = fds[i].fd;
                const auto it = std::ranges::find(g_watch, fd, &Watch::fd);
                if(it == g_watch.end()) continue;
                const auto cb = it->cb;
                cb(fds[i].revents);
                fired = true;
            }
        }
        fired |= run_tasks();
        fired |= run_timers();
        if(r > 0 && (fds[0].revents & (POLLIN | POLLHUP | POLLERR)))
            return true;
        if(fired) return false;
        if(r == 0 && timeout_ms >= 0 && t == timeout_ms) return false;
    }
}
