#include "buflist.hpp"
#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include "editor.hpp"
#include "events.hpp"
#include "file.hpp"
#include "lsp_sync.hpp"
#include "motion.hpp"
#include "normal.hpp"
#include "piece.hpp"
#include "state.hpp"

namespace {

void remember(const Buffer& b, int cy, int cx) {
    if(!b.nofile) cursor_remember(b.filename, cy, cx);
}

bool is_scratch(const Buffer& b) {
    return b.filename.empty() && b.pt.size() == 0;
}

void switch_to(size_t idx) {
    if(idx >= E.buffers.size() || idx == E.cur) return;
    set_mode(EditorMode::NORMAL);
    set_cursor_offset(motion::clamp_normal(E.buf().pt, cursor_offset()));
    normal_reset();
    Buffer& from = E.buf();
    from.cx = E.cx;
    from.cy = E.cy;
    from.want_rx = E.want_rx;
    from.row_offset = E.row_offset;
    from.col_offset = E.col_offset;
    remember(from, from.cy, from.cx);
    E.alt = &from;
    E.cur = idx;
    Buffer& to = E.buf();
    E.cx = to.cx;
    E.cy = to.cy;
    E.want_rx = to.want_rx;
    E.row_offset = to.row_offset;
    E.col_offset = to.col_offset;
    events_emit("buf_enter", {to.filename});
}

void prune_scratch() {
    for(size_t i = E.buffers.size(); i-- > 0;) {
        if(E.buffers.size() <= 1) break;
        if(i == E.cur || !is_scratch(*E.buffers[i])) continue;
        if(E.alt == E.buffers[i].get()) E.alt = nullptr;
        E.buffers.erase(E.buffers.begin() + static_cast<std::ptrdiff_t>(i));
        if(E.cur > i) --E.cur;
    }
}

std::string norm(const std::string& p) {
    std::error_code ec;
    return std::filesystem::absolute(p, ec).lexically_normal().string();
}

}  // namespace

void buffer_switch(size_t idx) {
    switch_to(idx);
    prune_scratch();
}

size_t buffer_open(const std::string& path) {
    for(size_t i = 0; i < E.buffers.size(); ++i)
        if(!E.buffers[i]->filename.empty() &&
           norm(E.buffers[i]->filename) == norm(path)) {
            buffer_switch(i);
            return i;
        }
    auto b = std::make_unique<Buffer>();
    if(!load_file(*b, path)) {
        editor_set_status_message("Can't open {}", path);
        return E.cur;
    }
    E.buffers.push_back(std::move(b));
    lsp_sync_attach(*E.buffers.back());
    buffer_switch(E.buffers.size() - 1);
    return E.cur;
}

void buffer_remember_cursor() { remember(E.buf(), E.cy, E.cx); }

bool buffer_close(size_t idx, bool force) {
    if(idx >= E.buffers.size()) return false;
    if(!force && !E.buffers[idx]->nofile && !is_scratch(*E.buffers[idx]) &&
       E.buffers[idx]->pt.modified()) {
        editor_set_status_message(
            "No write since last change for buffer {} (add ! to overwrite)",
            idx + 1);
        return false;
    }
    lsp_sync_detach(*E.buffers[idx]);
    if(E.buffers.size() == 1) {
        set_mode(EditorMode::NORMAL);
        remember(*E.buffers[0], E.cy, E.cx);
        normal_reset();
        E.buffers[0] = std::make_unique<Buffer>();
        E.alt = nullptr;
        E.cx = E.cy = E.want_rx = E.rx = E.row_offset = E.col_offset = 0;
        return true;
    }
    if(idx == E.cur) {
        size_t next = idx + 1 < E.buffers.size() ? idx + 1 : idx - 1;
        for(size_t i = 0; E.alt && i < E.buffers.size(); ++i)
            if(i != idx && E.buffers[i].get() == E.alt) {
                next = i;
                break;
            }
        switch_to(next);
    }
    if(E.alt == E.buffers[idx].get()) E.alt = nullptr;
    E.buffers.erase(E.buffers.begin() + static_cast<std::ptrdiff_t>(idx));
    if(E.cur > idx) --E.cur;
    return true;
}

void buffer_cycle(int step) {
    const int n = static_cast<int>(E.buffers.size());
    buffer_switch(
        static_cast<size_t>(((static_cast<int>(E.cur) + step) % n + n) % n));
}

void buffer_goto_alt() {
    for(size_t i = 0; E.alt && i < E.buffers.size(); ++i)
        if(E.buffers[i].get() == E.alt) return buffer_switch(i);
    editor_set_status_message("No alternate buffer");
}

std::string buffer_name(size_t i) {
    const std::string& f = E.buffers[i]->filename;
    return f.empty() ? "[No Name]" : f;
}

std::optional<size_t> buffer_first_modified() {
    for(size_t i = 0; i < E.buffers.size(); ++i)
        if(E.buffers[i]->pt.modified() && !is_scratch(*E.buffers[i])) return i;
    return std::nullopt;
}

std::string buffer_list() {
    std::string out;
    for(size_t i = 0; i < E.buffers.size(); ++i) {
        const Buffer& b = *E.buffers[i];
        out += std::format("{}{}{} {}{}",
                           i ? " | " : "",
                           i == E.cur ? '%' : (&b == E.alt ? '#' : ' '),
                           i + 1,
                           buffer_name(i),
                           b.pt.modified() ? " [+]" : "");
    }
    return out;
}

bool buffer_goto(std::string_view arg) {
    size_t n = 0;
    const auto [p, ec] =
        std::from_chars(arg.data(), arg.data() + arg.size(), n);
    if(ec == std::errc{} && p == arg.data() + arg.size()) {
        if(n < 1 || n > E.buffers.size()) {
            editor_set_status_message("No buffer {}", n);
            return false;
        }
        buffer_switch(n - 1);
        return true;
    }
    std::optional<size_t> hit;
    for(size_t i = 0; i < E.buffers.size(); ++i)
        if(buffer_name(i).find(arg) != std::string::npos) {
            if(hit) {
                editor_set_status_message("More than one match for {}", arg);
                return false;
            }
            hit = i;
        }
    if(!hit) {
        editor_set_status_message("No matching buffer for {}", arg);
        return false;
    }
    buffer_switch(*hit);
    return true;
}

void fill(Buffer& b, const std::string& text) {
    b.orig = text;
    b.eol = b.orig.empty() || b.orig.back() == '\n';
    std::string_view v = b.orig;
    if(!b.orig.empty() && b.orig.back() == '\n') v.remove_suffix(1);
    b.pt = PieceTable(v);
    b.cx = b.cy = b.want_rx = b.row_offset = b.col_offset = 0;
}

void buffer_show_text(const std::string& name, const std::string& text) {
    for(size_t i = 0; i < E.buffers.size(); ++i) {
        Buffer& b = *E.buffers[i];
        if(!b.nofile || b.filename != name) continue;
        fill(b, text);
        if(i == E.cur)
            E.cx = E.cy = E.want_rx = E.row_offset = E.col_offset = 0;
        else buffer_switch(i);
        return;
    }
    auto b = std::make_unique<Buffer>();
    b->filename = name;
    b->nofile = true;
    fill(*b, text);
    E.buffers.push_back(std::move(b));
    buffer_switch(E.buffers.size() - 1);
}
