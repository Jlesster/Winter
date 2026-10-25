#include "shell.hpp"
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <cerrno>
#include <cstddef>
#include <cstdlib>
#include "file.hpp"

ShellResult shell_run(const std::string& cmd, const std::string* input) {
    ShellResult r;
    int in_fd = -1;
    if(input) {
        char path[] = "/tmp/winter-XXXXXX";
        in_fd = mkstemp(path);
        if(in_fd != -1) {
            unlink(path);
            if(!write_all(in_fd, *input) || lseek(in_fd, 0, SEEK_SET) == -1) {
                close(in_fd);
                in_fd = -1;
            }
        }
    } else {
        in_fd = open("/dev/null", O_RDONLY);
    }
    if(in_fd == -1) {
        r.ok = false;
        return r;
    }
    int p[2];
    if(pipe(p) == -1) {
        close(in_fd);
        r.ok = false;
        return r;
    }
    const pid_t pid = fork();
    if(pid == -1) {
        close(in_fd);
        close(p[0]);
        close(p[1]);
        r.ok = false;
        return r;
    }
    if(pid == 0) {
        dup2(in_fd, 0);
        dup2(p[1], 1);
        dup2(p[1], 2);
        close(in_fd);
        close(p[0]);
        close(p[1]);
        execlp("bash", "bash", "-c", cmd.c_str(), static_cast<char*>(nullptr));
        execlp("/bin/sh", "sh", "-c", cmd.c_str(), static_cast<char*>(nullptr));
        _exit(127);
    }
    close(in_fd);
    close(p[1]);
    char buf[4096];
    for(;;) {
        const ssize_t n = read(p[0], buf, sizeof buf);
        if(n > 0) r.out.append(buf, static_cast<size_t>(n));
        else if(n < 0 && errno == EINTR) continue;
        else break;
    }
    close(p[0]);
    int st = 0;
    while(waitpid(pid, &st, 0) == -1 && errno == EINTR) {}
    r.code = WIFEXITED(st) ? WEXITSTATUS(st) : 128 + WTERMSIG(st);
    return r;
}
