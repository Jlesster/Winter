#include "snippet.hpp"
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <iterator>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include "core.hpp"
#include "edit_bus.hpp"
#include "editor.hpp"
#include "indent.hpp"
#include "keys.hpp"
#include "piece.hpp"
#include "theme.hpp"
#include "virt.hpp"

namespace {
struct Range {
    size_t s, e;
};

struct Session {
    PieceTable* pt = nullptr;
    std::map<int, std::vector<Range>> stops;
    std::vector<int> order;
    int cur = 0;
    bool sel = false;
    std::string last;
};

std::optional<Session> S;
std::map<std::string, std::string> g_snips;

size_t P(size_t id) { return *S->pt->anchor_pos(id); }
Range& prim(int idx) { return S->stops[idx][0]; }
std::string text_of(const Range& r) {
    return S->pt->text(P(r.s), P(r.e) - P(r.s));
}

void normalize(Range& r, bool active) {
    const bool inside = active || P(r.s) == P(r.e);
    S->pt->anchor_set_gravity(r.s, inside ? Gravity::Left : Gravity::Right);
    S->pt->anchor_set_gravity(r.e, inside ? Gravity::Right : Gravity::Left);
}

struct Parsed {
    std::string text;
    std::vector<std::tuple<int, size_t, size_t>> ph;
};

class Parser {
public:
    Parser(std::string_view b, std::string_view base, const std::string& unit)
        : b_(b), base_(base), unit_(unit) {}

        Parsed run() {
            seq(false);
            return std::move(o_);
        }

private:
    std::string_view b_, base_;
    const std::string& unit_;
    size_t i_ = 0;
    Parsed o_;

    bool peek(char c) const { return i_ < b_.size() && b_[i_] == c; }

    void put(char ch) {
        if(ch == '\n') {
            o_.text += '\n';
            o_.text.append(base_);
        } else if(ch == '\t') o_.text += unit_;
        else o_.text += ch;
    }

    static bool escapable(char c) { return c == '$' || c == '}' || c == '\\'; }

    int number() {
        int n = 0;
        while(i_ < b_.size() &&
              std::isdigit(static_cast<unsigned char>(b_[i_])))
            n = n * 10 + (b_[i_++] - '0');
        return n;
    }

    // text and $-constructs up to an unescaped '}' (nested) or end of input
    void seq(bool nested) {
        while(i_ < b_.size()) {
            const char c = b_[i_];
            if(nested && c == '}') return;
            if(c == '\\' && i_ + 1 < b_.size() && escapable(b_[i_ + 1])) {
                put(b_[i_ + 1]);
                i_ += 2;
            } else if(c == '$') {
                dollar();
            } else {
                put(c);
                ++i_;
            }
        }
    }

    // consume up to (not including) the matching '}', e.g. a ${1/re/fmt/} transform
    void skip_close() {
        int depth = 0;
        while(i_ < b_.size()) {
            const char c = b_[i_];
            if(c == '\\') {
                i_ += 2;
                continue;
            }
            if(c == '{') ++depth;
            else if(c == '}') {
                if(depth == 0) return;
                --depth;
            }
            ++i_;
        }
    }

    // ${1|a,b,c|}: no choice UI, so the first choice becomes the default text
    void choice() {
        bool first = true;
        while(i_ < b_.size()) {
            const char c = b_[i_];
            if(c == '|' && i_ + 1 < b_.size() && b_[i_ + 1] == '}') {
                ++i_;
                return;
            }
            if(c == '\\' && i_ + 1 < b_.size()) {
                if(first) put(b_[i_ + 1]);
                i_ += 2;
                continue;
            }
            if(c == ',') first = false;
            else if(first) put(c);
            ++i_;
        }
    }

    static bool variable(const std::string& n, std::string& out) {
        namespace fs = std::filesystem;
        const std::string& f = E.buf().filename;
        if(n == "TM_FILENAME") out = fs::path(f).filename().string();
        else if(n == "TM_FILENAME_BASE") out = fs::path(f).stem().string();
        else if(n == "TM_DIRECTORY") out = fs::path(f).parent_path().string();
        else if(n == "TM_FILEPATH") out = f;
        else if(n == "TM_LINE_INDEX") out = std::to_string(E.cy);
        else if(n == "TM_LINE_NUMBER") out = std::to_string(E.cy + 1);
        else if(n == "TM_SELECTED_TEXT" || n == "CLIPBOARD") out.clear();
        else return false;
        return true;
    }

    void dollar() {
        const size_t at = i_++;
        const bool brace = peek('{');
        if(brace) ++i_;

        if(i_ < b_.size() && std::isdigit(static_cast<unsigned char>(b_[i_]))) {
            const int n = number();
            const size_t s = o_.text.size();
            if(brace) {
                if(peek(':')) {
                    ++i_;
                    seq(true);
                } else if(peek('|')) {
                    ++i_;
                    choice();
                } else skip_close();
                if(peek('}')) ++i_;
            }
            o_.ph.emplace_back(n, s, o_.text.size());
            return;
        }

        size_t k = i_;
        while(k < b_.size() &&
              (std::isalnum(static_cast<unsigned char>(b_[k])) || b_[k] == '_'))
            ++k;
        if(k == i_) {  // lone '$': literal
            o_.text += '$';
            i_ = at + 1;
            return;
        }
        const std::string name(b_.substr(i_, k - i_));
        i_ = k;
        std::string val;
        const bool known = variable(name, val);
        if(brace) {
            if(peek(':')) {
                ++i_;
                if(known) skip_close();
                else seq(true);  // unknown variable: its default text
            } else skip_close();
            if(peek('}')) ++i_;
        }
        if(known)
            for(const char ch : val) put(ch);
    }
};

Parsed parse(std::string_view b, std::string_view base,
             const std::string& unit) {
    return Parser(b, base, unit).run();
}

void go_to(int idx) {
    S->cur = idx;
    for(auto& [n, v] : S->stops)
        for(size_t i = 0; i < v.size(); ++i)
            normalize(v[i], n == idx && i == 0);
    Range& r = prim(idx);
    set_cursor_offset(P(r.e));
    S->sel = P(r.s) != P(r.e);
    S->last = text_of(r);
}

void rewrite_mirror(Range& r, const std::string& text) {
    PieceTable& pt = *S->pt;
    const size_t s = P(r.s), n = P(r.e) - s;
    pt.remove(s, n);
    pt.anchor_set_gravity(r.s, Gravity::Left);
    pt.anchor_set_gravity(r.e, Gravity::Right);
    pt.insert(s, text);
    normalize(r, false);
}

void sync(bool force) {
    const std::string t = text_of(prim(S->cur));
    if(!force && t == S->last) return;
    S->last = t;
    auto& v = S->stops[S->cur];
    if(v.size() < 2) return;
    PieceTable& pt = *S->pt;
    const size_t ca = pt.anchor_add(cursor_offset(), Gravity::Left);
    const Origin old = pt.origin;
    pt.origin = Origin::Snippet;
    for(size_t i = 1; i < v.size(); ++i) rewrite_mirror(v[i], t);
    pt.origin = old;
    set_cursor_offset(*pt.anchor_pos(ca));
    pt.anchor_remove(ca);
}

}  // namespace

void snippet_init() {
    edit_subscribe(prio::snippet, [](const EditEvent& ev) {
        if(!S || ev.src != S->pt) return;
        if(ev.origin == Origin::Undo || ev.origin == Origin::Redo) {
            snippet_end();
        }
    });
}

bool snippet_active() { return S.has_value(); }

void snippet_spans(size_t ls, size_t len, std::vector<LineSpan>& out) {
    if(!S || S->pt != &E.buf().pt) return;
    const Style idle = theme_ui("ui.snippet", Style{.bg = 0x313244});
    const Style live = theme_ui("ui.snippet.active", Style{.bg = 0x45475a});
    for(const auto& [n, v] : S->stops)
        for(const Range& r : v) {
            const size_t s = P(r.s), e = P(r.e);
            if(e <= s || e <= ls || s >= ls + len) continue;
            out.push_back({std::max(s, ls) - ls,
                           std::min(e, ls + len) - ls,
                           3,
                           n == S->cur ? live : idle});
        }
}

void snippet_end() {
    if(!S) return;
    for(auto& [n, v] : S->stops)
        for(Range& r : v) {
            S->pt->anchor_remove(r.s);
            S->pt->anchor_remove(r.e);
        }
    S.reset();
}

bool snippet_expand(std::string_view body) {
    if(E.mode != EditorMode::INSERT) return false;
    snippet_end();
    PieceTable& pt = E.buf().pt;
    const std::string line = pt.line_text(E.cy);
    std::string_view base = indent::leading_ws(line);
    base = base.substr(0, std::min(base.size(), static_cast<size_t>(E.cx)));
    const Parsed p = parse(body, base, indent::unit());
    const size_t at = cursor_offset();
    PieceTable::Group g(pt);
    pt.insert(at, p.text);
    if(p.ph.empty()) {
        set_cursor_offset(at + p.text.size());
        return true;
    }
    S.emplace();
    S->pt = &pt;
    for(const auto& [n, s, e] : p.ph)
        S->stops[n].push_back({pt.anchor_add(at + s, Gravity::Right),
                               pt.anchor_add(at + e, Gravity::Left)});
    if(!S->stops.contains(0)) {
        const size_t end = at + p.text.size();
        S->stops[0].push_back({pt.anchor_add(end, Gravity::Right),
                               pt.anchor_add(end, Gravity::Left)});
    }
    for(const auto& [n, v] : S->stops)
        if(n != 0) S->order.push_back(n);
    S->order.push_back(0);
    for(int n : S->order)
        if(n != 0 && S->stops[n].size() > 1) {
            S->cur = n;
            S->last.clear();
            sync(true);
        }
    go_to(S->order.front());
    if(S->order.front() == 0) snippet_end();
    return true;
}

void snippet_next() {
    if(!S) return;
    const auto it = std::ranges::find(S->order, S->cur);
    if(it == S->order.end() || std::next(it) == S->order.end()) {
        snippet_end();
        return;
    }
    const int nxt = *std::next(it);
    go_to(nxt);
    if(nxt == 0) snippet_end();
}

bool snippet_pre_key(int c) {
    if(!S || !S->sel) return false;
    const bool typed = (c >= 32 && c < 127) || (c >= 0x80 && c < 0x100);
    const bool del = c == BACKSPACE || c == DELETE || c == CTRL_BACKSPACE ||
                     c == CTRL_KEY('h') || c == CTRL_KEY('w');
    if(!typed && !del) {
        S->sel = false;
        return false;
    }
    Range& r = prim(S->cur);
    const size_t s = P(r.s), n = P(r.e) - s;
    S->sel = false;
    S->pt->remove(s, n);
    set_cursor_offset(s);
    return del;
}

void snippet_post_key() {
    if(!S) return;
    if(E.mode != EditorMode::INSERT) {
        snippet_end();
        return;
    }
    sync(false);
    const size_t c = cursor_offset();
    for(auto& [n, v] : S->stops)
        if(c >= P(v[0].s) && c <= P(v[0].e)) return;
    snippet_end();
}

void snippet_add(std::string trigger, std::string body) {
    g_snips[std::move(trigger)] = std::move(body);
}

bool snippet_try_trigger() {
    if(E.mode != EditorMode::INSERT || S) return false;
    PieceTable& pt = E.buf().pt;
    const std::string line = pt.line_text(E.cy);
    const size_t e = static_cast<size_t>(E.cx);
    size_t s = e;
    while(s > 0 && (std::isalnum(static_cast<unsigned char>(line[s - 1])) ||
                    line[s - 1] == '_'))
        --s;
    if(s == e) return false;
    const auto it = g_snips.find(line.substr(s, e - s));
    if(it == g_snips.end()) return false;
    PieceTable::Group g(pt);
    pt.remove(pt.from_linecol(E.cy, s), e - s);
    E.cx = static_cast<int>(s);
    return snippet_expand(it->second);
}
