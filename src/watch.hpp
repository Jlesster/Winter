#pragma once
#include <functional>
#include <string>

using WatchId = int;

enum : unsigned {
    WATCH_STRUCT = 1,   // entry created / deleted / renamed in the dir
    WATCH_CONTENT = 2,  // entry written (closed after write) or attrs changed
    WATCH_SELF = 4,     // the watched dir itself went away
};

using WatchFn = std::function<void(const std::string& name, unsigned kind)>;

void watch_init();  // after loop_init()
WatchId watch_dir(const std::string& dir, WatchFn fn);  // 0 on failure
void watch_remove(WatchId id);
