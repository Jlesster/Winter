#include "job.hpp"
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <optional>
#include <vector>
#include "file.hpp"
#include "loop.hpp"

namespace {
struct Job {
    JobId id = 0;
    pid_t pid = -1;
    int fd = -1;
    int err_fd = -1;
    int in_fd = -1;
    bool close_in = false;
    bool eof = false;
    bool reaped = false;
    int status = 0;
    std::string out;
    std::string wbuf;
    JobSpec spec;
};
constexpr size_t kReadBugdget = 256 * 1024;
std::vector<std::unique_ptr<Job>> g_jobs;
JobId g_next = 0;

auto find_job(JobId id) {
    return std::ranges::find_if(g_jobs,
                                [id](const auto& j) { return j->id == id; });
}

void on_child(int) { loop_wake(); }

int open_stdin(const std::optional<std::string>& input) {
    if(!input) return open("/dev/null", O_RDONLY | O_CLOEXEC);
    char path[] = "/tmp/winter-XXXXXX";
    const int fd = mkstemp(path);
    if(fd == -1) return -1;
    unlink(path);
    if(!write_all(fd, *input) || lseek(fd, 0, SEEK_SET) == -1) {
        close(fd);
        return -1;
    }
    return fd;
}

bool drain(Job& j, int fd, bool err, size_t budget = SIZE_MAX) {
    char buf[16384];
    for(size_t got = 0; got < budget;) {
        const ssize_t n = read(fd, buf, sizeof buf);
        if(n > 0) {
            got += static_cast<size_t>(n);
            const std::string_view chunk(buf, static_cast<size_t>(n));
            if(err) {
                if(j.spec.on_err) j.spec.on_err(chunk);
            } else {
                if(j.spec.keep_out) j.out.append(chunk);
                if(j.spec.on_data) j.spec.on_data(chunk);
            }
            continue;
        }
        if(n == 0) return true;
        if(errno == EINTR) continue;
        return errno != EAGAIN;
    }
    return false;
}

void close_fd(int& fd) {
    if(fd >= 0) {
        loop_unwatch(fd);
        close(fd);
        fd = -1;
    }
}

void on_writable(JobId id);

void flush_in(Job& j) {
    while(j.in_fd >= 0 && !j.wbuf.empty()) {
        const ssize_t n = write(j.in_fd, j.wbuf.data(), j.wbuf.size());
        if(n > 0) {
            j.wbuf.erase(0, static_cast<size_t>(n));
            continue;
        }
        if(n < 0 && errno == EINTR) continue;
        if(n < 0 && errno == EAGAIN) break;
        j.wbuf.clear();
        close_fd(j.in_fd);
        return;
    }
    if(j.in_fd < 0) return;
    if(!j.wbuf.empty()) {
        const JobId id = j.id;
        loop_watch(j.in_fd, POLLOUT, [id](short) { on_writable(id); });
    } else if(j.close_in) {
        close_fd(j.in_fd);
    } else {
        loop_unwatch(j.in_fd);
    }
}

void on_writable(JobId id) {
    const auto it = find_job(id);
    if(it != g_jobs.end()) flush_in(**it);
}

void close_pipe(Job& j) {
    if(j.fd >= 0) {
        loop_unwatch(j.fd);
        close(j.fd);
        j.fd = -1;
    }
    j.eof = true;
}

void maybe_finish(JobId id) {
    const auto it = find_job(id);
    if(it == g_jobs.end() || !(*it)->reaped) return;
    Job& j = **it;
    if(!j.eof) {
        drain(j, j.fd, false);
        close_pipe(j);
    }
    if(j.err_fd >= 0) {
        drain(j, j.err_fd, true);
        close_fd(j.err_fd);
    }
    close_fd(j.in_fd);
    JobResult r{
        std::move(j.out),
        WIFEXITED(j.status) ? WEXITSTATUS(j.status) : 128 + WTERMSIG(j.status)};
    auto done = std::move(j.spec.on_exit);
    g_jobs.erase(it);
    if(done) done(std::move(r));
}

void on_ready(JobId id) {
    const auto it = find_job(id);
    if(it == g_jobs.end()) return;
    if(drain(**it, (*it)->fd, false, kReadBugdget)) close_pipe(**it);
    maybe_finish(id);
}

void on_err_ready(JobId id) {
    const auto it = find_job(id);
    if(it == g_jobs.end()) return;
    if(drain(**it, (*it)->err_fd, true, kReadBugdget)) close_fd((*it)->err_fd);
    maybe_finish(id);
}

void reap() {
    std::vector<JobId> ids;
    for(const auto& j : g_jobs)
        if(!j->reaped) ids.push_back(j->id);
    for(const JobId id : ids) {
        const auto it = find_job(id);
        if(it == g_jobs.end()) continue;
        int st = 0;
        pid_t r;
        do r = waitpid((*it)->pid, &st, WNOHANG);
        while(r == -1 && errno == EINTR);
        if(r == (*it)->pid || (r == -1 && errno == ECHILD)) {
            (*it)->reaped = true;
            (*it)->status = r == -1 ? 0 : st;
            maybe_finish(id);
        }
    }
}

}  // namespace

void job_init() {
    struct sigaction sa{};
    sa.sa_handler = on_child;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    sigaction(SIGCHLD, &sa, nullptr);
    signal(SIGPIPE, SIG_IGN);
    loop_on_wake(reap);
}

JobId job_start(JobSpec spec) {
    int in_fd = -1, wr_fd = -1;
    if(spec.pipe_stdin) {
        int ip[2];
        if(pipe2(ip, O_CLOEXEC) == -1) return 0;
        in_fd = ip[0];
        wr_fd = ip[1];
    } else {
        in_fd = open_stdin(spec.input);
    }
    if(in_fd == -1) return 0;
    int p[2] = {-1, -1}, e[2] = {-1, -1};
    const auto fail = [&] {
        for(const int fd : {in_fd, wr_fd, p[0], p[1], e[0], e[1]})
            if(fd >= 0) close(fd);
        return JobId{0};
    };
    if(pipe2(p, O_CLOEXEC) == -1) {
        p[0] = p[1] = -1;
        return fail();
    }
    if(spec.split_err && pipe2(e, O_CLOEXEC) == -1) {
        e[0] = e[1] = -1;
        return fail();
    }
    const pid_t pid = fork();
    if(pid == -1) return fail();

    if(pid == 0) {
        setsid();
        signal(SIGPIPE, SIG_DFL);
        dup2(in_fd, 0);
        dup2(p[1], 1);
        dup2(e[1] >= 0 ? e[1] : p[1], 2);
        close(in_fd);
        execlp("bash",
               "bash",
               "-c",
               spec.cmd.c_str(),
               static_cast<char*>(nullptr));
        execlp("/bin/sh",
               "sh",
               "-c",
               spec.cmd.c_str(),
               static_cast<char*>(nullptr));
        _exit(127);
    }
    close(in_fd);
    close(p[1]);
    if(e[1] >= 0) close(e[1]);
    fcntl(p[0], F_SETFL, O_NONBLOCK);
    if(e[0] >= 0) fcntl(e[0], F_SETFL, O_NONBLOCK);
    if(wr_fd >= 0) fcntl(wr_fd, F_SETFL, O_NONBLOCK);
    auto j = std::make_unique<Job>();
    j->id = ++g_next;
    j->pid = pid;
    j->fd = p[0];
    j->err_fd = e[0];
    j->in_fd = wr_fd;
    j->spec = std::move(spec);
    const JobId id = j->id;
    g_jobs.push_back(std::move(j));
    loop_watch(p[0], POLLIN, [id](short) { on_ready(id); });
    if(e[0] >= 0) loop_watch(e[0], POLLIN, [id](short) { on_err_ready(id); });
    return id;
}

bool job_write(JobId id, std::string_view data) {
    const auto it = find_job(id);
    if(it == g_jobs.end()) return false;
    Job& j = **it;
    if(j.in_fd < 0 || j.close_in) return false;
    j.wbuf.append(data);
    flush_in(j);
    return j.in_fd >= 0;
}

void job_close_stdin(JobId id) {
    const auto it = find_job(id);
    if(it == g_jobs.end()) return;
    (*it)->close_in = true;
    flush_in(**it);
}

void job_kill(JobId id) {
    const auto it = find_job(id);
    if(it != g_jobs.end() && !(*it)->reaped) kill(-(*it)->pid, SIGTERM);
}

void job_kill_all() {
    for(const auto& j : g_jobs)
        if(!j->reaped) kill(-j->pid, SIGTERM);
}
