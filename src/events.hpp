#pragma once
#include <functional>
#include <string>
#include <string_view>
#include <vector>

struct Buffer;

using EventHandler = std::function<void(const std::vector<std::string>&)>;
int events_on(std::string name, EventHandler fn);
bool events_off(int id);
void events_emit(std::string_view name,
                 const std::vector<std::string>& args = {});

// The buffer an event is about, for handlers that touch buffer-local state.
// nullptr outside a scope: callers fall back to the current buffer.
Buffer* events_buf();
class EventBufScope {
    Buffer* prev_;

public:
    explicit EventBufScope(Buffer* b);
    ~EventBufScope();
    EventBufScope(const EventBufScope&) = delete;
    EventBufScope& operator=(const EventBufScope&) = delete;
};
