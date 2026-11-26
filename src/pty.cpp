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

#include "pty.hpp"

#include <fcntl.h>
#include <poll.h>
#include <pty.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <memory>

#include "loop.hpp"

extern char** environ;

namespace {

using Clock = std::chrono::steady_clock;

constexpr size_t kReadBudget = 256 * 1024;  // bytes per loop turn
constexpr size_t kMaxBacklog = 4 * 1024 * 1024;

struct Pty {
    PtyId id = 0;
    pid_t pid = -1;
    int fd = -1;  // master
    bool want_out = false;
    bool eof = false;  // the child side is gone; the master stays open
    int reap_ms = 10;
    std::string wbuf;
    PtySpec spec;
};

struct Dying {
    pid_t pid;
    Clock::time_point kill_at;
    bool killed = false;
};

std::vector<std::unique_ptr<Pty>> g_ptys;
std::vector<Dying> g_dying;
PtyId g_next = 1;
TimerId g_dying_timer = 0;

Pty* find(PtyId id) {
    for(auto& p : g_ptys)
        if(p->id == id) return p.get();
    return nullptr;
}

void on_event(PtyId id, short ev);

void watch(Pty& p) {
    const PtyId id = p.id;
    loop_watch(p.fd,
               static_cast<short>(POLLIN | (p.want_out ? POLLOUT : 0)),
               [id](short ev) { on_event(id, ev); });
}

// ---- environment ----------------------------------------------------------

std::vector<std::string> make_env(const PtySpec& s) {
    std::vector<std::string> env;
    const auto drop = [](const std::string& e) {
        for(const char* k :
            {"TERM=", "COLORTERM=", "TERM_PROGRAM=", "LINES=", "COLUMNS="})
            if(e.starts_with(k)) return true;
        return false;
    };
    for(char** e = environ; e && *e; ++e)
        if(!drop(*e)) env.emplace_back(*e);
    env.emplace_back("TERM=xterm-256color");
    env.emplace_back("COLORTERM=truecolor");
    env.emplace_back("TERM_PROGRAM=winter");
    for(const auto& [k, v] : s.env) {
        const std::string pre = k + "=";
        std::erase_if(env,
                      [&](const std::string& e) { return e.starts_with(pre); });
        env.push_back(pre + v);
    }
    return env;
}

// ---- reaping --------------------------------------------------------------

int exit_code(int st) {
    return WIFEXITED(st) ? WEXITSTATUS(st) : 128 + WTERMSIG(st);
}

void arm_dying();

void reap_dying() {
    g_dying_timer = 0;
    const auto now = Clock::now();
    for(auto it = g_dying.begin(); it != g_dying.end();) {
        int st = 0;
        pid_t r;
        do r = waitpid(it->pid, &st, WNOHANG);
        while(r == -1 && errno == EINTR);
        if(r == it->pid || (r == -1 && errno == ECHILD)) {
            it = g_dying.erase(it);
            continue;
        }
        if(!it->killed && now >= it->kill_at) {
            kill(it->pid, SIGKILL);
            it->killed = true;
        }
        ++it;
    }
    arm_dying();
}

void arm_dying() {
    if(g_dying.empty() || g_dying_timer) return;
    g_dying_timer = loop_after(50, reap_dying);
}

void bury(pid_t pid) {
    g_dying.push_back({pid, Clock::now() + std::chrono::milliseconds(1000)});
    arm_dying();
}

// The child's tty is gone; collect its status. A child that closed the tty
// but keeps running is polled with a growing delay.
void try_reap(PtyId id) {
    Pty* p = find(id);
    if(!p) return;
    int st = 0;
    pid_t r;
    do r = waitpid(p->pid, &st, WNOHANG);
    while(r == -1 && errno == EINTR);
    if(r == 0) {
        const int ms = p->reap_ms;
        p->reap_ms = std::min(ms * 2, 250);
        loop_after(ms, [id] { try_reap(id); });
        return;
    }
    const int code = r == -1 ? 0 : exit_code(st);
    // Only now: closing the master hangs the tty up, and a child that is
    // still mid-exit would be killed by that SIGHUP (status 129).
    if(p->fd >= 0) close(p->fd);
    auto done = std::move(p->spec.on_exit);
    std::erase_if(g_ptys, [id](const auto& q) { return q->id == id; });
    if(done) done(code);
}

// ---- io -------------------------------------------------------------------

// True once the child side is gone. Stops after kReadBudget bytes and lets
// the (level-triggered) watch call again next turn.
bool drain(PtyId id) {
    char buf[16384];
    for(size_t got = 0; got < kReadBudget;) {
        Pty* p = find(id);
        if(!p || p->eof) return false;
        const ssize_t n = read(p->fd, buf, sizeof buf);
        if(n > 0) {
            got += static_cast<size_t>(n);
            if(p->spec.on_data) p->spec.on_data({buf, static_cast<size_t>(n)});
            continue;  // on_data may have closed it: find() again
        }
        if(n < 0 && errno == EINTR) continue;
        if(n < 0 && errno == EAGAIN) return false;
        return true;  // 0 or EIO: every slave end is closed
    }
    return false;
}

void flush(Pty& p) {
    while(!p.wbuf.empty()) {
        const ssize_t n = write(p.fd, p.wbuf.data(), p.wbuf.size());
        if(n > 0) {
            p.wbuf.erase(0, static_cast<size_t>(n));
            continue;
        }
        if(n < 0 && errno == EINTR) continue;
        if(n < 0 && errno == EAGAIN) break;
        p.wbuf.clear();  // EIO: the read side reports the exit
        break;
    }
    const bool want = !p.wbuf.empty();
    if(want != p.want_out) {
        p.want_out = want;
        watch(p);
    }
}

void on_event(PtyId id, short ev) {
    if(Pty* p = find(id); p && (ev & POLLOUT)) flush(*p);
    if(!(ev & (POLLIN | POLLHUP | POLLERR))) return;
    if(!drain(id)) return;
    Pty* p = find(id);
    if(!p) return;
    p->eof = true;
    loop_unwatch(p->fd);  // EIO/HUP would fire on every turn
    try_reap(id);
}

}  // namespace

// ---------------------------------------------------------------------------

PtyId pty_open(PtySpec spec) {
    std::vector<std::string> argv = spec.argv;
    if(argv.empty()) {
        const char* sh = getenv("SHELL");
        argv.emplace_back(sh && *sh ? sh : "/bin/sh");
    }
    // Everything the child needs is built before fork(): no allocation after.
    std::vector<std::string> env = make_env(spec);
    std::vector<char*> av, ev;
    for(auto& a : argv) av.push_back(a.data());
    for(auto& e : env) ev.push_back(e.data());
    av.push_back(nullptr);
    ev.push_back(nullptr);
    const char* cwd = spec.cwd.empty() ? nullptr : spec.cwd.c_str();

    winsize ws{};
    ws.ws_row = static_cast<unsigned short>(std::max(spec.rows, 1));
    ws.ws_col = static_cast<unsigned short>(std::max(spec.cols, 1));

    int master = -1;
    const pid_t pid = forkpty(&master, nullptr, nullptr, &ws);
    if(pid < 0) return 0;
    if(pid == 0) {
        // The editor's dispositions must not leak into the shell: an ignored
        // signal stays ignored across exec.
        // Every signal, not a list: whatever started the editor (nohup, a
        // test runner, a wrapper script) may have ignored any of them.
        for(int s = 1; s < NSIG; ++s) signal(s, SIG_DFL);
        sigset_t none;
        sigemptyset(&none);
        sigprocmask(SIG_SETMASK, &none, nullptr);
        if(cwd && chdir(cwd) != 0) {  // a vanished directory: start in $HOME
            if(const char* h = getenv("HOME")) (void)!chdir(h);
        }
        execvpe(av[0], av.data(), ev.data());
        _exit(127);
    }

    fcntl(master, F_SETFD, FD_CLOEXEC);
    fcntl(master, F_SETFL, fcntl(master, F_GETFL) | O_NONBLOCK);
    termios t{};
    if(tcgetattr(master, &t) == 0) {  // so the line discipline erases UTF-8
        t.c_iflag |= IUTF8;
        tcsetattr(master, TCSANOW, &t);
    }

    auto p = std::make_unique<Pty>();
    p->id = g_next++;
    p->pid = pid;
    p->fd = master;
    p->spec = std::move(spec);
    Pty& r = *p;
    g_ptys.push_back(std::move(p));
    watch(r);
    return r.id;
}

bool pty_write(PtyId id, std::string_view data) {
    Pty* p = find(id);
    if(!p || p->eof) return false;
    if(p->wbuf.size() + data.size() > kMaxBacklog) return false;
    p->wbuf.append(data);
    flush(*p);  // writes now when it can; sets up POLLOUT otherwise
    return true;
}

void pty_resize(PtyId id, int rows, int cols) {
    Pty* p = find(id);
    if(!p || p->eof) return;
    winsize ws{};
    ws.ws_row = static_cast<unsigned short>(std::max(rows, 1));
    ws.ws_col = static_cast<unsigned short>(std::max(cols, 1));
    ioctl(p->fd, TIOCSWINSZ, &ws);
}

void pty_close(PtyId id) {
    Pty* p = find(id);
    if(!p) return;
    const pid_t pid = p->pid;
    loop_unwatch(p->fd);
    close(p->fd);
    p->spec.on_exit = nullptr;
    p->spec.on_data = nullptr;
    std::erase_if(g_ptys, [id](const auto& q) { return q->id == id; });
    kill(pid, SIGHUP);
    bury(pid);
}

void pty_kill_all() {
    std::vector<PtyId> ids;
    for(const auto& p : g_ptys) ids.push_back(p->id);
    for(const PtyId id : ids) pty_close(id);
    // The editor is going away: give them a moment, then make sure.
    for(int i = 0; i < 20 && !g_dying.empty(); ++i) {
        std::erase_if(g_dying, [](const Dying& d) {
            int st = 0;
            const pid_t r = waitpid(d.pid, &st, WNOHANG);
            return r == d.pid || (r == -1 && errno == ECHILD);
        });
        if(!g_dying.empty()) usleep(5000);
    }
    for(const Dying& d : g_dying) kill(d.pid, SIGKILL);
}
