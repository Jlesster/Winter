#include "edit_bus.hpp"
#include <algorithm>
#include <cassert>
#include <type_traits>
#include <vector>

namespace {
struct Sub {
    int prio;
    EditFn fn;
};
std::vector<Sub> subs;
std::vector<GroupFn> group_subs;
std::vector<std::function<void()>> deferred;
bool dispatching = false;
}  // namespace

void edit_subscribe(int priority, EditFn fn) {
    subs.push_back({priority, std::move(fn)});
    std::stable_sort(subs.begin(), subs.end(), [](const Sub& a, const Sub& b) {
        return a.prio < b.prio;
    });
}

void edit_group_end_subscribe(GroupFn fn) {
    group_subs.push_back(std::move(fn));
}

void edit_defer(std::function<void()> fn) { deferred.push_back(std::move(fn)); }

void edit_emit(const EditEvent& ev) {
    assert(!dispatching && "mutation during dispatch, use edit_defer");
    dispatching = true;
    for(auto& s : subs) s.fn(ev);
    dispatching = false;
    auto run = std::move(deferred);
    deferred.clear();
    for(auto& f : run) f();
}

void edit_group_end() {
    for(auto& f : group_subs) f();
}

EditEvent make_event(const PieceTable& pt, const TextEdit& e, Origin o) {
    const auto p = [](LineCol c) {
        return TSPoint{static_cast<uint32_t>(c.line),
                       static_cast<uint32_t>(c.col)};
    };
    return {e.start,
            e.old_end - e.start,
            e.new_end - e.start,
            p(e.start_pt),
            p(e.old_end_pt),
            p(e.new_end_pt),
            {static_cast<uint32_t>(e.start_pt.line),
             static_cast<uint32_t>(e.start_u16)},
            {static_cast<uint32_t>(e.old_end_pt.line),
             static_cast<uint32_t>(e.old_end_u16)},
            pt.revision(),
            o,
            &pt};
}
