#include "marks.hpp"

#include <algorithm>
#include <map>
#include <utility>

#include "piece.hpp"

namespace {

MarkHost g_host;
std::map<uint64_t, std::map<char, size_t>> g_local;  // buffer -> name -> anchor id

struct Global {
    uint64_t buf = 0;  // while the buffer lives
    size_t anchor = 0;
    std::string file;
    size_t line = 0, col = 0;  // last known, used once the buffer is gone
    bool live = false;
};
std::map<char, Global> g_global;  // A-Z

struct Prev {
    uint64_t buf = 0;
    size_t anchor = 0;
} g_prev;

bool is_lower(char c) { return c >= 'a' && c <= 'z'; }
bool is_upper(char c) { return c >= 'A' && c <= 'Z'; }
bool is_local(char c) { return is_lower(c) || c == '.' || c == '<' || c == '>'; }
char canon(char c) { return c == '`' ? '\'' : c; }

PieceTable* table(uint64_t buf) {
    return buf && g_host.table ? g_host.table(buf) : nullptr;
}

std::optional<MarkPos> locate(uint64_t buf, size_t anchor) {
    PieceTable* t = table(buf);
    if(!t) return std::nullopt;
    const auto pos = t->anchor_pos(anchor);
    if(!pos) return std::nullopt;
    MarkPos m;
    m.buf = buf;
    m.off = std::min(*pos, t->size());
    m.line = t->line_of(m.off);
    m.col = m.off - t->line_start(m.line);
    if(g_host.file) m.file = g_host.file(buf);
    return m;
}

void drop_anchor(uint64_t buf, size_t anchor) {
    if(PieceTable* t = table(buf)) t->anchor_remove(anchor);
}

// the anchor for a mark in buffer `buf`, replacing any earlier one
size_t place(uint64_t buf, size_t off, size_t old_anchor, uint64_t old_buf) {
    PieceTable* t = table(buf);
    if(old_anchor) drop_anchor(old_buf, old_anchor);
    // Right: text typed at the mark's own spot pushes it along with its character
    return t->anchor_add(std::min(off, t->size()), Gravity::Right);
}

}  // namespace

void marks_set_host(MarkHost host) {
    g_host = std::move(host);
    g_local.clear();
    g_global.clear();
    g_prev = {};
}

bool mark_name_settable(char c) {
    return is_lower(c) || is_upper(c) || c == '\'' || c == '`' || c == '<' ||
           c == '>';
}

bool mark_name_valid(char c) { return mark_name_settable(c) || c == '.'; }

bool mark_put(char name, uint64_t buf, size_t off) {
    name = canon(name);
    if(!mark_name_settable(name) || !table(buf)) return false;
    if(is_local(name)) {
        size_t& slot = g_local[buf][name];  // 0 when new
        slot = place(buf, off, slot, buf);
        return true;
    }
    if(is_upper(name)) {
        Global& g = g_global[name];
        const size_t old = g.live ? g.anchor : 0;
        g.anchor = place(buf, off, old, g.buf);
        g.buf = buf;
        g.live = true;
        if(g_host.file) g.file = g_host.file(buf);
        if(const auto m = locate(buf, g.anchor)) {
            g.line = m->line;
            g.col = m->col;
        }
        return true;
    }
    // '\''
    g_prev.anchor = place(buf, off, g_prev.buf ? g_prev.anchor : 0, g_prev.buf);
    g_prev.buf = buf;
    return true;
}

bool mark_put_at(char name, uint64_t buf, size_t line, size_t col) {
    PieceTable* t = table(buf);
    if(!t) return false;
    line = std::min(line, t->line_count() - 1);
    const size_t ls = t->line_start(line);
    return mark_put(name, buf, ls + std::min(col, t->line_end(line) - ls));
}

std::optional<MarkPos> mark_find(char name, uint64_t cur_buf) {
    name = canon(name);
    if(is_local(name)) {
        const auto b = g_local.find(cur_buf);
        if(b == g_local.end()) return std::nullopt;
        const auto s = b->second.find(name);
        if(s == b->second.end()) return std::nullopt;
        return locate(cur_buf, s->second);
    }
    if(is_upper(name)) {
        const auto it = g_global.find(name);
        if(it == g_global.end()) return std::nullopt;
        const Global& g = it->second;
        if(g.live)
            if(auto m = locate(g.buf, g.anchor)) return m;
        MarkPos m;  // closed (or its anchor was lost): go by file name
        m.file = g.file;
        m.line = g.line;
        m.col = g.col;
        return m;
    }
    if(name == '\'' && g_prev.buf) return locate(g_prev.buf, g_prev.anchor);
    return std::nullopt;
}

bool mark_del(char name, uint64_t cur_buf) {
    name = canon(name);
    if(is_local(name)) {
        const auto b = g_local.find(cur_buf);
        if(b == g_local.end()) return false;
        const auto s = b->second.find(name);
        if(s == b->second.end()) return false;
        drop_anchor(cur_buf, s->second);
        b->second.erase(s);
        return true;
    }
    if(is_upper(name)) {
        const auto it = g_global.find(name);
        if(it == g_global.end()) return false;
        if(it->second.live) drop_anchor(it->second.buf, it->second.anchor);
        g_global.erase(it);
        return true;
    }
    if(name == '\'' && g_prev.buf) {
        drop_anchor(g_prev.buf, g_prev.anchor);
        g_prev = {};
        return true;
    }
    return false;
}

std::vector<char> mark_names(uint64_t cur_buf) {
    std::vector<char> out;
    const auto b = g_local.find(cur_buf);
    if(b != g_local.end()) {
        for(char c = 'a'; c <= 'z'; ++c)
            if(b->second.contains(c)) out.push_back(c);
        for(const char c : {'.', '<', '>'})
            if(b->second.contains(c)) out.push_back(c);
    }
    for(const auto& [k, g] : g_global) out.push_back(k);
    if(g_prev.buf) out.push_back('\'');
    return out;
}

std::map<size_t, char> mark_lines(uint64_t buf) {
    std::map<size_t, char> out;
    for(const char c : mark_names(buf)) {
        if(!is_lower(c) && !is_upper(c)) continue;  // skip . < > '
        if(const auto m = mark_find(c, buf); m && m->buf == buf)
            out.try_emplace(m->line, c);  // several on a line: first one wins
    }
    return out;
}

void mark_prev(uint64_t buf, size_t off) { mark_put('\'', buf, off); }

void mark_changed(uint64_t buf, size_t off) {
    PieceTable* t = table(buf);
    if(!t) return;
    size_t& slot = g_local[buf]['.'];
    if(slot) t->anchor_move(slot, std::min(off, t->size()));
    else slot = t->anchor_add(std::min(off, t->size()), Gravity::Right);
}

void mark_visual(uint64_t buf, size_t a, size_t b) {
    mark_put('<', buf, a);
    mark_put('>', buf, b);
}

void mark_buffer_closing(uint64_t buf) {
    for(auto& [name, g] : g_global) {
        if(!g.live || g.buf != buf) continue;
        if(const auto m = locate(buf, g.anchor)) {  // freeze where it was
            g.line = m->line;
            g.col = m->col;
        }
        g.live = false;
        g.buf = 0;
    }
    if(g_prev.buf == buf) g_prev = {};
    g_local.erase(buf);
}
