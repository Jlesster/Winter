#include "watch.hpp"

#include <poll.h>
#include <sys/inotify.h>
#include <unistd.h>

#include <filesystem>
#include <map>

#include "loop.hpp"

namespace {

struct Dir {
    int wd;
    std::map<WatchId, WatchFn> subs;
};

int g_fd = -1;
WatchId g_next = 0;
std::map<std::string, Dir> g_dirs;  // canonical path -> watch
std::map<int, std::string> g_wd;    // wd -> canonical path

unsigned kind_of(uint32_t m) {
    unsigned k = 0;
    if(m & (IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO))
        k |= WATCH_STRUCT;
    if(m & (IN_CLOSE_WRITE | IN_ATTRIB)) k |= WATCH_CONTENT;
    if(m & (IN_DELETE_SELF | IN_MOVE_SELF)) k |= WATCH_SELF;
    return k;
}

void drain(short) {
    alignas(inotify_event) char buf[16384];
    for(;;) {
        const ssize_t n = read(g_fd, buf, sizeof buf);
        if(n <= 0) return;
        for(char* p = buf; p < buf + n;) {
            const auto* ev = reinterpret_cast<const inotify_event*>(p);
            p += sizeof(inotify_event) + ev->len;
            const auto w = g_wd.find(ev->wd);
            if(w == g_wd.end()) continue;
            const std::string dir = w->second;
            if(ev->mask & IN_IGNORED) {  // kernel dropped the watch
                g_dirs.erase(dir);
                g_wd.erase(w);
                continue;
            }
            const auto d = g_dirs.find(dir);
            if(d == g_dirs.end()) continue;
            const unsigned k = kind_of(ev->mask);
            if(!k) continue;
            const std::string name = ev->len ? ev->name : "";
            const auto subs = d->second.subs;  // callbacks may add/remove
            for(const auto& [id, fn] : subs) fn(name, k);
        }
    }
}

}  // namespace

void watch_init() {
    g_fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if(g_fd >= 0) loop_watch(g_fd, POLLIN, drain);
}

WatchId watch_dir(const std::string& dir, WatchFn fn) {
    if(g_fd < 0) return 0;
    std::error_code ec;
    const std::string key = std::filesystem::canonical(dir, ec).string();
    if(ec) return 0;
    auto it = g_dirs.find(key);
    if(it == g_dirs.end()) {
        const int wd = inotify_add_watch(
            g_fd,
            key.c_str(),
            IN_CLOSE_WRITE | IN_ATTRIB | IN_CREATE | IN_DELETE |
                IN_MOVED_FROM | IN_MOVED_TO | IN_DELETE_SELF | IN_MOVE_SELF |
                IN_ONLYDIR);
        if(wd < 0) return 0;  // ENOSPC = out of watches, EACCES, ...
        g_wd[wd] = key;
        it = g_dirs.emplace(key, Dir{wd, {}}).first;
    }
    it->second.subs[++g_next] = std::move(fn);
    return g_next;
}

void watch_remove(WatchId id) {
    for(auto it = g_dirs.begin(); it != g_dirs.end(); ++it) {
        if(!it->second.subs.erase(id)) continue;
        if(it->second.subs.empty()) {
            inotify_rm_watch(g_fd, it->second.wd);
            g_wd.erase(it->second.wd);
            g_dirs.erase(it);
        }
        return;
    }
}
