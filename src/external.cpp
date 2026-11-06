#include "external.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <utility>

#include "bufedit.hpp"
#include "editor.hpp"
#include "events.hpp"
#include "loop.hpp"
#include "motion.hpp"
#include "piece.hpp"
#include "state.hpp"
#include "utf8.hpp"
#include "watch.hpp"

namespace fs = std::filesystem;

namespace {

enum class Res { Same, Applied, Conflict, Gone, Deferred };

std::map<Buffer*, WatchId> g_watch;
std::set<Buffer*> g_pending;   // saw an fs event, check on next tick
std::set<Buffer*> g_deferred;  // can't touch right now (typing / visual)
TimerId g_timer = 0;

bool read_disk(const std::string& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if(!in) return false;
    out.assign(std::istreambuf_iterator<char>(in),
               std::istreambuf_iterator<char>());
    return true;
}

void apply_text(Buffer& b, const std::string& text) {
    buffer_replace_text(b, text, Origin::External);
    b.pt.mark_saved();  // buffer == disk again; `u` brings the old text back
}

Res check(Buffer& b, bool force) {
    std::string text;
    if(!read_disk(b.filename, text)) {
        std::error_code ec;
        b.disk_gone = !fs::exists(b.filename, ec);
        return Res::Gone;
    }
    b.disk_gone = false;
    const uint64_t h = state_hash(text);
    if(!force && h == b.disk_hash && text.size() == b.disk_size) {
        b.disk_changed = false;  // includes the echo of our own save
        return Res::Same;
    }
    if(!force && b.pt.modified()) {
        b.disk_changed = true;
        return Res::Conflict;
    }
    if(&b == &E.buf() && E.mode != NORMAL && E.mode != FILES)
        return Res::Deferred;
    apply_text(b, text);
    b.disk_hash = h;
    b.disk_size = text.size();
    b.disk_changed = false;
    return Res::Applied;
}

std::string base(const Buffer& b) {
    return fs::path(b.filename).filename().string();
}

void report(Buffer& b, Res r) {
    switch(r) {
        case Res::Applied:
            editor_set_status_message("{} reloaded (changed on disk)", base(b));
            {
                EventBufScope scope(&b);
                events_emit("buf_reload", {b.filename});
            }
            break;
        case Res::Conflict:
            editor_set_status_message(
                "{} changed on disk, buffer has unsaved edits (:e! to reload)",
                base(b));
            break;
        case Res::Gone:
            if(b.disk_gone)
                editor_set_status_message("{} deleted on disk", base(b));
            break;
        default: break;
    }
}

void arm() {
    if(g_timer) return;  // throttle, not debounce: never starve under churn
    g_timer = loop_after(80, [] {
        g_timer = 0;
        const auto batch = std::exchange(g_pending, {});
        for(Buffer* b : batch) {
            const Res r = check(*b, false);
            if(r == Res::Deferred) g_deferred.insert(b);
            else report(*b, r);
        }
    });
}

}  // namespace

void external_init() {
    // typing/visual ended -> retry whatever we had to hold back
    static const int id = events_on("mode_changed", [](const auto&) {
        if(g_deferred.empty()) return;
        g_pending.merge(g_deferred);
        g_deferred.clear();
        arm();
    });
    (void)id;
}

void external_watch(Buffer& b) {
    if(b.nofile || b.filename.empty() || g_watch.contains(&b)) return;
    std::error_code ec;
    const fs::path abs = fs::absolute(b.filename, ec);
    if(ec) return;
    const std::string name = abs.filename().string();
    const WatchId id = watch_dir(
        abs.parent_path().string(),
        [&b, name](const std::string& n, unsigned) {
            if(n != name) return;
            g_pending.insert(&b);
            arm();
        });
    if(id) g_watch[&b] = id;
}

void external_unwatch(Buffer& b) {
    if(const auto it = g_watch.find(&b); it != g_watch.end()) {
        watch_remove(it->second);
        g_watch.erase(it);
    }
    g_pending.erase(&b);
    g_deferred.erase(&b);
}

bool external_reload(Buffer& b, bool force) {
    if(b.nofile || b.filename.empty()) return false;
    const Res r = check(b, force);
    if(r == Res::Same && !force)
        editor_set_status_message("{} is up to date", base(b));
    else if(r == Res::Applied) editor_set_status_message("{} reloaded", base(b));
    else report(b, r);
    return r == Res::Applied;
}
