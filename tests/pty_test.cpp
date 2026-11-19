#include "../src/pty.hpp"
#include <signal.h>
#include <sys/wait.h>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include "../src/loop.hpp"

static int fails = 0;
#define CHECK(c)                                             \
    do {                                                     \
        if(!(c)) {                                           \
            std::printf("FAIL line %d: %s\n", __LINE__, #c); \
            ++fails;                                         \
        }                                                    \
    } while(0)

struct Run {
    std::string out;
    int code = -999;
    bool exited = false;
    int chunks = 0;
    PtyId id = 0;
};

static PtyId start(Run& r, std::vector<std::string> argv, int rows = 24,
                   int cols = 80) {
    PtySpec s;
    s.argv = std::move(argv);
    s.rows = rows;
    s.cols = cols;
    s.on_data = [&r](std::string_view d) {
        r.out.append(d);
        ++r.chunks;
    };
    s.on_exit = [&r](int c) {
        r.code = c;
        r.exited = true;
    };
    return r.id = pty_open(std::move(s));
}
static bool wait_for(const std::function<bool()>& f, int ms = 5000) {
    return loop_pump_until(f, ms);
}

int main() {
    loop_init();
    signal(SIGPIPE, SIG_IGN);  // like job_init: must not leak into the child

    {
        Run r;
        start(r, {"/bin/sh", "-c", "printf hello; exit 3"});
        CHECK(wait_for([&] { return r.exited; }));
        CHECK(r.out == "hello");
        CHECK(r.code == 3);
        std::puts("1 exit code + output");
    }

    setenv("LINES", "7", 1);  // must not reach the child
    {
        Run r;
        start(r,
              {"/bin/sh",
               "-c",
               "echo $TERM $COLORTERM $TERM_PROGRAM; stty size; printenv LINES "
               "|| echo nolines"},
              30,
              100);
        CHECK(wait_for([&] { return r.exited; }));
        CHECK(r.out.find("xterm-256color truecolor winter") !=
              std::string::npos);
        CHECK(r.out.find("30 100") != std::string::npos);
        CHECK(r.out.find("nolines") != std::string::npos);
        std::puts("2 env + size");
    }

    {
        Run r;
        start(r, {"/bin/cat"});
        pty_write(r.id, "abc\n");
        CHECK(wait_for(
            [&] { return r.out.find("abc\r\nabc") != std::string::npos; }));
        pty_write(r.id, "\x04");  // ^D
        CHECK(wait_for([&] { return r.exited; }));
        CHECK(r.code == 0);
        std::puts("3 interactive cat + ^D");
    }

    {
        Run r;
        start(r,
              {"/bin/sh",
               "-c",
               "trap 'stty size; exit 0' WINCH; while :; do sleep 0.05; done"},
              10,
              20);
        wait_for([&] { return false; }, 200);
        pty_resize(r.id, 41, 133);
        CHECK(wait_for([&] { return r.exited; }));
        CHECK(r.out.find("41 133") != std::string::npos);
        std::puts("4 resize -> SIGWINCH");
    }

    {
        Run r;
        start(r, {"/bin/sh", "-c", "yes | head -c 5000000"});
        int ticks = 0;
        loop_after(1, [&] {});  // loop keeps turning while data flows
        CHECK(wait_for(
            [&] {
                ++ticks;
                return r.exited;
            },
            20000));
        CHECK(r.out.size() >= 5000000);
        CHECK(r.chunks > 100);
        std::printf("5 flood: %zu bytes, %d chunks, %d loop turns\n",
                    r.out.size(),
                    r.chunks,
                    ticks);
    }

    {
        Run r;
        start(r,
              {"/bin/sh", "-c", "grep -E 'SigIgn|SigBlk' /proc/self/status"});
        CHECK(wait_for([&] { return r.exited; }));
        CHECK(r.out.find("SigIgn:\t0000000000000000") != std::string::npos);
        CHECK(r.out.find("SigBlk:\t0000000000000000") != std::string::npos);
        std::puts("6 signals reset");
    }

    {
        Run r;
        start(r, {"/bin/sleep", "100"});
        wait_for([&] { return false; }, 100);
        pid_t pid = 0;
        (void)pid;
        pty_close(r.id);
        CHECK(wait_for(
            [&] {
                int st;
                return waitpid(-1, &st, WNOHANG) == -1 && errno == ECHILD;
            },
            3000));
        CHECK(!r.exited);
        std::puts("7 close reaps");
    }

    {
        Run r;
        start(r,
              {"/bin/sh",
               "-c",
               "trap '' HUP; sleep 100"});  // ignores SIGHUP: needs the KILL
        wait_for([&] { return false; }, 150);
        pty_close(r.id);
        CHECK(wait_for(
            [&] {
                int st;
                return waitpid(-1, &st, WNOHANG) == -1 && errno == ECHILD;
            },
            4000));
        std::puts("8 stubborn child killed");
    }

    {
        Run r;
        start(r, {"/nonexistent/x"});
        CHECK(wait_for([&] { return r.exited; }));
        CHECK(r.code == 127);
        std::puts("9 exec failure -> 127");
    }

    {
        Run r;
        start(r, {"/bin/sh", "-c", "sleep 100"});
        pty_kill_all();
        int st;
        CHECK(waitpid(-1, &st, WNOHANG) == -1);
        std::puts("10 kill_all");
    }

    std::printf(fails ? "%d FAILED\n" : "all ok\n", fails);
    return fails != 0;
}
#include <cstdlib>
[[noreturn]] void die(const char* s) {
    std::perror(s);
    std::exit(2);
}
