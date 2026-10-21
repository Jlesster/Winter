#include "events.hpp"
#include <algorithm>

namespace {
struct Sub {
    int id;
    std::string name;
    EventHandler fn;
};
std::vector<Sub> g_subs;
int g_next_id = 1;
}  // namespace

int events_on(std::string name, EventHandler fn) {
    g_subs.push_back({g_next_id, std::move(name), std::move(fn)});
    return g_next_id++;
}

bool events_off(int id) {
    const auto it = std::find_if(g_subs.begin(),
                                 g_subs.end(),
                                 [id](const Sub& s) { return s.id == id; });
    if(it == g_subs.end()) return false;
    g_subs.erase(it);
    return true;
}

void events_emit(std::string_view name, const std::vector<std::string>& args) {
    const std::vector<Sub> snapshot = g_subs;
    for(const Sub& s : snapshot) {
        if(s.name != name) continue;
        const bool alive =
            std::any_of(g_subs.begin(), g_subs.end(), [&](const Sub& t) {
                return t.id == s.id;
            });
        if(alive) s.fn(args);
    }
}
