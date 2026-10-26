#include "search.hpp"
#include <algorithm>
#include <optional>
#include "editor.hpp"
#include "normal.hpp"
#include "options.hpp"
#include "prompt.hpp"

namespace {
struct Pos {
    int line, col;
};
struct Found {
    Pos at;
    bool wrapped;
};

std::string g_last;
std::string g_hl;
std::string g_saved_hl;
struct {
    int cx = 0, cy = 0, col_offset = 0, row_offset = 0;
} g_origin;
bool g_found = false;
bool g_wrapped = false;

std::optional<Found> find_from(const std::string& q, Pos from, int dir) {
    if(q.empty()) return std::nullopt;
    const int n = E.num_rows();
    for(int k = 0; k <= n; ++k) {
        const int raw = dir > 0 ? from.line + k : from.line - k;
        const int ln = ((raw % n) + n) % n;
        const std::string text = E.buf().pt.line_text(static_cast<size_t>(ln));
        size_t at = std::string::npos;
        if(dir > 0) {
            const size_t start = k == 0 ? static_cast<size_t>(from.col) + 1 : 0;
            if(start <= text.size()) at = text.find(q, start);
        } else if(k != 0) {
            at = text.rfind(q);
        } else if(from.col > 0) {
            at = text.rfind(q, static_cast<size_t>(from.col) - 1);
        }
        if(at != std::string::npos)
            return Found{{ln, static_cast<int>(at)}, raw >= n || raw < 0};
    }
    return std::nullopt;
}

void jump_to(Pos p) {
    const bool off =
        p.line < E.row_offset || p.line >= E.row_offset + E.term_rows;
    E.cy = p.line;
    E.cx = p.col;
    if(off) E.row_offset = std::max(p.line - E.term_rows / 2, 0);
    update_want();
}

void restore_origin() {
    E.cx = g_origin.cx;
    E.cy = g_origin.cy;
    E.col_offset = g_origin.col_offset;
    E.row_offset = g_origin.row_offset;
}

void find_callback(std::string query, int key) {
    if(key == '\x1b') {
        restore_origin();
        g_hl = g_saved_hl;
        return;
    }
    if(key == '\r') {
        if(!g_found) {
            restore_origin();
            g_hl = g_saved_hl;
            editor_set_status_message("Pattern not found: {}", query);
            return;
        }
        g_last = query;
        g_hl = query;
        return;
    }
    g_hl = query;
    const auto f = find_from(query, {g_origin.cy, g_origin.cx}, +1);
    g_found = f.has_value();
    g_wrapped = f && f->wrapped;
    if(f) jump_to(f->at);
    else restore_origin();
}

}  // namespace

void editor_find() {
    g_origin = {E.cx, E.cy, E.col_offset, E.row_offset};
    g_saved_hl = g_hl;
    g_found = g_wrapped = false;
    editor_prompt("Search: ", find_callback);
    set_mode(NORMAL);
}

void search_step(int dir, int count) {
    if(g_last.empty()) {
        editor_set_status_message("No previous search");
        return;
    }
    Pos p{E.cy, E.cx};
    bool wrapped = false;
    for(int i = 0; i < std::max(count, 1); ++i) {
        const auto f = find_from(g_last, p, dir);
        if(!f) {
            editor_set_status_message("Pattern not found: {}", g_last);
            return;
        }
        p = f->at;
        wrapped |= f->wrapped;
    }
    g_hl = g_last;
    jump_to(p);
}

void search_clear_highlight() { g_hl.clear(); }

void search_marks(int line, std::string_view text,
                  std::vector<SearchMark>& out) {
    out.clear();
    if(!opts.hlsearch || g_hl.empty()) return;
    for(size_t at = text.find(g_hl);
        at != std::string_view::npos && out.size() < 512;
        at = text.find(g_hl, at + g_hl.size()))
        out.push_back({at,
                       at + g_hl.size(),
                       line == E.cy && static_cast<int>(at) == E.cx});
}
