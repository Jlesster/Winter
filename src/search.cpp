/*
 * Winter - A lightweight text editor
 * Copyright (C) 2026 Jessica Price
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#include "search.hpp"
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>
#include "editor.hpp"
#include "fold_buf.hpp"
#include "menu.hpp"
#include "normal.hpp"
#include "options.hpp"
#include "prompt.hpp"
#include "src/buffer.hpp"
#include "src/core.hpp"
#include "src/fuzzy.hpp"
#include "src/render.hpp"
#include "src/statusline.hpp"
#include "src/virt.hpp"
#include "theme.hpp"

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
Menu g_menu;
std::vector<BufWord> g_words;

std::string folded(std::string s) {
    for(char& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool is_word(char c) {
    const auto u = static_cast<unsigned char>(c);
    return std::isalnum(u) || u == '_' || u >= 0x80;
}

void word_span(const std::string& buf, size_t pos, bool regex, size_t& from,
               size_t& to) {
    from = pos;
    while(from > 0 && is_word(buf[from - 1])) --from;
    if(regex && from > 0 && from < pos && buf[from - 1] == '\\') ++from;
    to = pos;
    while(to < buf.size() && is_word(buf[to])) ++to;
}

std::optional<Found> find_from(const std::string& query, Pos from, int dir) {
    if(query.empty()) return std::nullopt;
    const bool cs = smartcase_sensitive(query);
    const std::string q = cs ? query : folded(query);
    const int n = E.num_rows();
    for(int k = 0; k <= n; ++k) {
        const int raw = dir > 0 ? from.line + k : from.line - k;
        const int ln = ((raw % n) + n) % n;
        std::string text = E.buf().pt.line_text(static_cast<size_t>(ln));
        if(!cs) text = folded(std::move(text));
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
    E.row_sub = 0;
    update_want();
}

void restore_origin() {
    E.cx = g_origin.cx;
    E.cy = g_origin.cy;
    E.col_offset = g_origin.col_offset;
    E.row_offset = g_origin.row_offset;
    E.row_sub = 0;
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

bool smartcase_sensitive(std::string_view pat, bool regex) {
    for(size_t i = 0; i < pat.size(); ++i) {
        const auto c = static_cast<unsigned char>(pat[i]);
        if(regex && c == '\\') {
            ++i;
            continue;
        }
        if(std::isupper(c)) return true;
    }
    return false;
}

std::vector<BufWord> buffer_words() {
    std::unordered_map<std::string, size_t> seen;
    const size_t rows = static_cast<size_t>(E.num_rows());
    for(size_t ln = 0; ln < rows; ++ln) {
        const std::string text = E.buf().pt.line_text(ln);
        for(size_t i = 0; i < text.size();) {
            if(!is_word(text[i])) {
                ++i;
                continue;
            }
            size_t j = i;
            while(j < text.size() && is_word(text[j])) ++j;
            if(j - i >= 2) ++seen[text.substr(i, j - i)];
            i = j;
        }
    }
    std::vector<BufWord> out;
    out.reserve(seen.size());
    for(auto& [w, c] : seen) out.push_back({w, c});
    return out;
}

void words_menu_update(Menu& menu, const std::vector<BufWord>& words,
                       const std::string& buf, size_t pos, bool regex) {
    size_t from, to;
    word_span(buf, pos, regex, from, to);
    const std::string lead = buf.substr(from, pos - from);
    if(lead.empty()) return menu.close();

    const bool cs = smartcase_sensitive(lead);
    struct Hit {
        int score;
        size_t count;
        const std::string* word;
        std::vector<size_t> pos;
    };
    std::vector<Hit> hits;
    fuzzy::Match m;
    for(const BufWord& w : words) {
        if(w.text == lead || !fuzzy::match(w.text, lead, m)) continue;
        if(cs) {  // a capital in the query: every letter must match exactly
            bool exact = true;
            for(size_t i = 0; i < lead.size() && exact; ++i)
                exact = w.text[m.pos[i]] == lead[i];
            if(!exact) continue;
        }
        hits.push_back({m.score, w.count, &w.text, m.pos});
    }
    if(hits.empty()) return menu.close();

    std::ranges::stable_sort(hits, [](const Hit& a, const Hit& b) {
        if(a.score != b.score) return a.score > b.score;
        if(a.count != b.count) return a.count > b.count;
        return *a.word < *b.word;
    });
    if(hits.size() > 200) hits.erase(hits.begin() + 200, hits.end());

    std::vector<MenuItem> items;
    for(Hit& h : hits)
        items.push_back(MenuItem{.label = *h.word,
                                 .detail = std::to_string(h.count),
                                 .hl = std::move(h.pos)});
    if(!menu.is_open())
        menu.open({.at = OverlaySpec::At::Screen,
                   .x = 0,
                   .y = editor_message_row(),
                   .up = true,
                   .cover_bars = true,
                   .in_prompt = true,
                   .max_rows = 8,
                   .min_w = 1,
                   .max_w = 200,
                   .fit_detail = true});
    menu.set_items(std::move(items));
}

bool words_menu_accept(Menu& menu, std::string& buf, size_t& pos, bool regex) {
    const MenuItem* it = menu.selected();
    if(!menu.visible() || !it) return false;
    size_t from, to;
    word_span(buf, pos, regex, from, to);
    buf.replace(from, to - from, it->label);
    pos = from + it->label.size();
    return true;
}

std::string search_last_pattern() { return g_last; }

void editor_find() {
    g_origin = {E.cx, E.cy, E.col_offset, E.row_offset};
    g_saved_hl = g_hl;
    g_found = g_wrapped = false;
    g_words = buffer_words();
    PromptHooks hooks;
    hooks.keymap_mode = SEARCH;
    hooks.pre_key = [](int key, std::string& buf, size_t& pos) {
        switch(key) {
            case '\t':
            case CTRL_KEY('y'):
                if(!words_menu_accept(g_menu, buf, pos, false)) return false;
                find_callback(buf, 0);  // query changed: redo the live search
                words_menu_update(g_menu, g_words, buf, pos, false);
                return true;
            case CTRL_KEY('n'):
            case CTRL_KEY('p'):
                if(!g_menu.visible()) return false;  // history, as before
                g_menu.move(key == CTRL_KEY('n') ? +1 : -1);
                return true;
            default: return false;
        }
    };
    hooks.after_key =
        [](const std::string& buf, size_t pos, int key, bool edited) {
            find_callback(buf, key);
            if(key == '\x1b' || key == '\r' || !edited) g_menu.close();
            else words_menu_update(g_menu, g_words, buf, pos, false);
        };
    editor_prompt("Search: ", hooks);
    g_menu.close();
    g_words = {};
    set_mode(NORMAL);
    if(E.cy != g_origin.cy || E.cx != g_origin.cx)  // not cancelled
        fold_reveal(kFoldSearch);
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
    fold_reveal(kFoldSearch);
}

void search_clear_highlight() { g_hl.clear(); }

void search_spans(int line, std::string_view text, std::vector<LineSpan>& out) {
    if(!opts.hlsearch || g_hl.empty()) return;
    const bool cs = smartcase_sensitive(g_hl);
    const std::string needle = cs ? g_hl : folded(g_hl);
    const std::string hay = cs ? std::string() : folded(std::string(text));
    const std::string_view h = cs ? text : std::string_view(hay);
    size_t n = 0;
    for(size_t at = h.find(needle); at != std::string_view::npos && n < 512;
        at = h.find(needle, at + needle.size()), ++n) {
        const bool cur = line == E.cy && static_cast<int>(at) == E.cx;
        const Style st = cur ? theme_ui("ui.search.current",
                                        Style{.fg = kSearchCurFg,
                                              .bg = kSearchCurBg,
                                              .attrs = ATTR_BOLD})
                             : theme_ui("ui.search", Style{.bg = kSearchBg});
        out.push_back({at, at + needle.size(), kPrioSearch, st});
    }
}
