#include "multicursor.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <tuple>

#include "core.hpp"
#include "editor.hpp"
#include "grapheme.hpp"
#include "motion.hpp"
#include "normal.hpp"
#include "piece.hpp"
#include "reg.hpp"

namespace {

struct Cur {
    size_t pos, van;  // anchor ids: cursor, visual anchor
    int want;
};

struct State {
    Buffer* buf = nullptr;
    std::vector<Cur> extra;
    std::vector<size_t> hist;  // pos ids of extras, in the order they came
    bool running = false;
    size_t index = 0, count = 0;  // the cursor mc_each runs now / of how many
    size_t prim = 0;              // which of them is the primary
    uint64_t pass = 0;
    // add_next / add_all match whole words only, for the word they selected
    // themselves (like vim's *); a selection made by hand matches anywhere.
    bool whole_word = false;
    bool seeded = false;  // seed_* is the selection add_next made on its own
    size_t seed_pos = 0, seed_van = 0;
} S;

// Extras belong to one buffer; switching buffer forgets them. (The old
// buffer may be gone, so its anchors are not touched.)
void check_buf() {
    if(S.buf != &E.buf()) {
        S.extra.clear();
        S.hist.clear();
        S.seeded = false;
        S.buf = &E.buf();
    }
}

PieceTable& pt() { return E.buf().pt; }
size_t at(size_t id) { return pt().anchor_pos(id).value_or(0); }

void drop(const Cur& c) {
    pt().anchor_remove(c.pos);
    pt().anchor_remove(c.van);
}

Cur make(size_t pos, size_t van, int want) {
    return {pt().anchor_add(pos, Gravity::Right),
            pt().anchor_add(van, Gravity::Right),
            want};
}

void load(const Cur& c) {
    set_cursor_offset(at(c.pos));
    E.v_anchor = at(c.van);
    E.want_rx = c.want;
}

void store(Cur& c) {
    pt().anchor_move(c.pos, cursor_offset());
    pt().anchor_move(c.van, E.v_anchor);
    c.want = E.want_rx;
}

// A cursor for a place the editor is not at: the want column needs the
// editor's cursor to be there for a moment, so the caller restores it.
Cur make_at(size_t pos, size_t van) {
    set_cursor_offset(pos);
    update_want();
    return make(pos, van, E.want_rx);
}

struct KeepPrimary {
    size_t pos = cursor_offset(), van = E.v_anchor;
    int want = E.want_rx;
    ~KeepPrimary() {
        E.v_anchor = van;
        set_cursor_offset(pos);
        E.want_rx = want;
    }
};

// Document order. The positions are read once: anchor_pos is a scan, so
// asking for them inside the comparison would cost a scan per comparison.
void sort_by_pos(std::vector<Cur>& v) {
    std::vector<std::pair<size_t, size_t>> k(v.size());  // {offset, index}
    for(size_t i = 0; i < v.size(); ++i) k[i] = {at(v[i].pos), i};
    std::ranges::stable_sort(
        k, {}, &std::pair<size_t, size_t>::first);
    std::vector<Cur> out;
    out.reserve(v.size());
    for(const auto& p : k) out.push_back(v[p.second]);
    v = std::move(out);
}

void remember(const Cur& c) { S.hist.push_back(c.pos); }

bool word_char(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

// One past the grapheme the cursor sits on (the visual selection includes it).
size_t grapheme_end(size_t pos) {
    PieceTable& t = pt();
    const LineCol lc = t.to_linecol(pos);
    const std::string line = t.line_text(lc.line);
    if(lc.col >= line.size()) return pos + 1;  // on the newline
    return pos + (gr_next(line, lc.col) - lc.col);
}

struct Sel {
    size_t lo, hi;  // [lo, hi) of the visual selection
};

Sel selection() {
    const size_t a = E.v_anchor, b = cursor_offset();
    return {std::min(a, b), grapheme_end(std::max(a, b))};
}

// Select the word under the cursor (the first press of add_next).
bool seed_word() {
    PieceTable& t = pt();
    const std::string line = t.line_text(static_cast<size_t>(E.cy));
    size_t a = std::min(static_cast<size_t>(E.cx), line.size());
    if(a >= line.size() || !word_char(line[a])) return false;
    size_t b = a;
    while(a > 0 && word_char(line[a - 1])) --a;
    while(b < line.size() && word_char(line[b])) ++b;
    const size_t base = t.line_start(static_cast<size_t>(E.cy));
    E.v_line = false;
    E.v_anchor = base + a;
    set_cursor_offset(base + b - 1);
    set_mode(VISUAL);
    update_want();
    S.whole_word = true;
    S.seeded = true;
    S.seed_pos = cursor_offset();
    S.seed_van = E.v_anchor;
    return true;
}

// The visual selection as the needle. False if there is nothing to search.
bool needle_of(Sel& s, std::string& needle) {
    s = selection();
    if(s.hi > pt().size() || s.hi <= s.lo) return false;
    if(S.extra.empty())  // a fresh start: is this the word we selected?
        S.whole_word = S.seeded && E.v_anchor == S.seed_van &&
                       cursor_offset() == S.seed_pos;
    needle = pt().text(s.lo, s.hi - s.lo);
    return true;
}

// Whole-word is only asked on the sides where the needle starts / ends with a
// word character.
bool is_whole(const std::string& s, size_t p, size_t n) {
    const bool l = word_char(s[p]) && p > 0 && word_char(s[p - 1]);
    const bool r = word_char(s[p + n - 1]) && p + n < s.size() &&
                   word_char(s[p + n]);
    return !l && !r;
}

// Start of every (non-overlapping) match, ascending.
std::vector<size_t> find_all(const std::string& hay, const std::string& needle,
                             bool whole) {
    std::vector<size_t> out;
    size_t h = hay.find(needle);
    while(h != std::string::npos) {
        if(!whole || is_whole(hay, h, needle.size())) {
            out.push_back(h);
            h = hay.find(needle, h + needle.size());
        } else {
            h = hay.find(needle, h + 1);
        }
    }
    return out;
}

// Where selections already start: the primary's and every extra's. Sorted.
std::vector<size_t> taken_starts(size_t primary_lo) {
    std::vector<size_t> v{primary_lo};
    for(const Cur& c : S.extra) v.push_back(std::min(at(c.pos), at(c.van)));
    std::ranges::sort(v);
    return v;
}

// The cursor of a selection [start, start + n): on its last grapheme.
size_t last_grapheme(const std::string& needle) {
    return gr_prev(needle, needle.size());
}

void add_next_impl(bool skip) {
    check_buf();
    if(E.mode != VISUAL) {  // first press: select the word under the cursor
        seed_word();
        return;
    }
    // Visual: the selection is the needle; add the next occurrence.
    Sel s;
    std::string needle;
    if(!needle_of(s, needle)) return;
    const std::string all = pt().text();
    const std::vector<size_t> hits = find_all(all, needle, S.whole_word);
    const std::vector<size_t> taken = taken_starts(s.lo);
    const auto free = [&](size_t h) {
        return !std::ranges::binary_search(taken, h);
    };
    size_t from = s.hi;
    for(const Cur& c : S.extra) from = std::max(from, at(c.pos) + 1);
    size_t hit = std::string::npos;
    for(size_t h : hits)
        if(h >= from && free(h)) {
            hit = h;
            break;
        }
    if(hit == std::string::npos)  // wrap
        for(size_t h : hits)
            if(free(h)) {
                hit = h;
                break;
            }
    if(hit == std::string::npos) {
        editor_set_status_message("No more matches");
        return;
    }
    // the old primary becomes an extra (unless it is skipped); the new match
    // is the primary, so the view follows it
    if(!skip) {
        S.extra.push_back(make(cursor_offset(), E.v_anchor, E.want_rx));
        remember(S.extra.back());
    }
    E.v_anchor = hit;
    set_cursor_offset(hit + last_grapheme(needle));
    update_want();
}

}  // namespace

bool mc_active() {
    check_buf();
    return !S.extra.empty();
}

void mc_clear() {
    check_buf();
    for(const Cur& c : S.extra) drop(c);
    S.extra.clear();
    S.hist.clear();
    S.seeded = false;
    reg_mc_forget();
}

bool mc_action_multi(std::string_view a) {
    if(a == "edit.undo" || a == "edit.redo" || a == "edit.dot") return false;
    if(a == "mode.command" || a == "mode.search") return false;
    for(std::string_view p :
        {"motion.", "visual.", "edit.", "mode.", "search.", "find."})
        if(a.starts_with(p)) return true;
    return false;  // lsp.*, jump.*, win.*, tree.*, output.*, user actions
}

void mc_set_extras(const std::vector<size_t>& offsets) {
    check_buf();
    if(S.running) return;
    for(const Cur& c : S.extra) drop(c);
    S.extra.clear();
    S.hist.clear();
    S.seeded = false;
    KeepPrimary keep;
    std::vector<size_t> seen{cursor_offset()};
    for(size_t o : offsets) {
        o = std::min(o, pt().size());
        if(std::ranges::find(seen, o) != seen.end()) continue;
        seen.push_back(o);
        S.extra.push_back(make_at(o, o));
        remember(S.extra.back());
    }
}

bool mc_running() { return S.running; }
size_t mc_index() { return S.index; }
size_t mc_count() { return S.count; }
bool mc_is_primary() { return !S.running || S.index == S.prim; }
uint64_t mc_pass() { return S.pass; }

void mc_each(const std::function<void()>& fn) {
    check_buf();
    if(S.extra.empty() || S.running) {
        fn();
        return;
    }
    PieceTable::Group group(pt());
    S.running = true;
    ++S.pass;

    // Every cursor, the primary included, becomes an anchor pair, in
    // document order. Each runs as the editor's cursor in turn; the anchors
    // of the ones still waiting move with the edits of the ones before.
    std::vector<Cur> all = S.extra;
    const Cur prim = make(cursor_offset(), E.v_anchor, E.want_rx);
    all.push_back(prim);
    sort_by_pos(all);

    S.count = all.size();
    S.prim = 0;
    for(size_t i = 0; i < all.size(); ++i)
        if(all[i].pos == prim.pos) S.prim = i;
    for(size_t i = 0; i < all.size(); ++i) {
        S.index = i;
        load(all[i]);
        fn();
        store(all[i]);
    }
    S.running = false;

    // Cursors that ended on the same spot merge (the primary wins). The
    // positions are read once, then equal ones are found by sorting.
    struct Key {
        size_t pos, van, i;
    };
    std::vector<Key> keys(all.size());
    for(size_t i = 0; i < all.size(); ++i)
        keys[i] = {at(all[i].pos),
                   E.mode == VISUAL ? at(all[i].van) : size_t{0},
                   i};
    std::ranges::stable_sort(keys, [](const Key& a, const Key& b) {
        return std::tie(a.pos, a.van) < std::tie(b.pos, b.van);
    });
    std::vector<Cur> keep;
    for(size_t a = 0; a < keys.size();) {
        size_t b = a, win = a;
        while(b < keys.size() && keys[b].pos == keys[a].pos &&
              keys[b].van == keys[a].van) {
            if(all[keys[b].i].pos == prim.pos) win = b;
            ++b;
        }
        for(size_t k = a; k < b; ++k)
            if(k != win) drop(all[keys[k].i]);
        keep.push_back(all[keys[win].i]);
        a = b;
    }
    S.extra.clear();
    for(const Cur& c : keep) {
        if(c.pos == prim.pos) {
            load(c);
            drop(c);
        } else S.extra.push_back(c);
    }
    if(S.extra.empty()) reg_mc_forget();
}

void mc_add_vertical(int d) {
    check_buf();
    const size_t here = cursor_offset();
    for(const Cur& c : S.extra)
        if(at(c.pos) == here) return;
    S.extra.push_back(make(here, E.v_anchor, E.want_rx));
    move_vertical(d, 1);
    if(cursor_offset() == here) {  // buffer edge: nowhere to go
        drop(S.extra.back());
        S.extra.pop_back();
        return;
    }
    remember(S.extra.back());
    // landed on an existing cursor: that one is now the primary
    const size_t now = cursor_offset();
    const auto it = std::ranges::find_if(
        S.extra, [&](const Cur& c) { return at(c.pos) == now; });
    if(it != S.extra.end() && std::next(it) != S.extra.end()) {
        drop(*it);
        S.extra.erase(it);
    }
}

void mc_add_next() { add_next_impl(false); }
void mc_skip_next() { add_next_impl(true); }

void mc_add_all() {
    check_buf();
    if(E.mode != VISUAL && !seed_word()) return;
    Sel s;
    std::string needle;
    if(!needle_of(s, needle)) return;
    const std::string all = pt().text();
    const std::vector<size_t> hits = find_all(all, needle, S.whole_word);
    const std::vector<size_t> taken = taken_starts(s.lo);
    const size_t tail = last_grapheme(needle);
    {
        KeepPrimary keep;
        for(size_t h : hits) {
            if(std::ranges::binary_search(taken, h)) continue;
            S.extra.push_back(make_at(h + tail, h));
            remember(S.extra.back());
        }
    }
    editor_set_status_message("{} cursors", S.extra.size() + 1);
}

void mc_remove_last() {
    check_buf();
    while(!S.hist.empty()) {
        const size_t id = S.hist.back();
        S.hist.pop_back();
        const auto it = std::ranges::find(S.extra, id, &Cur::pos);
        if(it == S.extra.end()) continue;  // merged away since
        const Cur c = *it;
        S.extra.erase(it);
        load(c);  // the one added last was the primary: this takes its place
        drop(c);
        if(S.extra.empty()) reg_mc_forget();
        return;
    }
}

void mc_rotate(int d) {
    check_buf();
    if(S.extra.empty()) return;
    const Cur prim = make(cursor_offset(), E.v_anchor, E.want_rx);
    std::vector<Cur> all = S.extra;
    all.push_back(prim);
    sort_by_pos(all);
    const size_t n = all.size();
    const size_t i = static_cast<size_t>(
        std::ranges::find(all, prim.pos, &Cur::pos) - all.begin());
    const size_t j = (i + (d >= 0 ? 1 : n - 1)) % n;
    S.extra.clear();
    for(size_t k = 0; k < n; ++k)
        if(k != j) S.extra.push_back(all[k]);
    load(all[j]);
    drop(all[j]);
}

void mc_split_lines() {
    check_buf();
    if(E.mode != VISUAL) return;
    PieceTable& t = pt();
    const size_t head = cursor_offset(), tail = E.v_anchor;
    const Sel s = selection();
    const size_t l0 = t.line_of(s.lo), l1 = t.line_of(std::max(head, tail));
    const size_t hl = t.line_of(head);
    struct Seg {
        size_t pos, van;
        bool primary;
    };
    std::vector<Seg> segs;
    for(size_t l = l0; l <= l1; ++l) {
        const size_t ls = t.line_start(l), le = t.line_end(l);
        const size_t a = E.v_line ? ls : std::max(s.lo, ls);
        const size_t b = E.v_line ? le : std::min(s.hi, le);
        if(b <= a) continue;  // nothing on it to select (an empty line)
        const std::string line = t.line_text(l);
        segs.push_back({ls + gr_prev(line, b - ls), a, l == hl});
    }
    if(segs.size() < 2) {
        editor_set_status_message("Selection covers a single line");
        return;
    }
    if(std::ranges::none_of(segs, &Seg::primary))
        segs.front().primary = true;  // the head sat on an empty line
    E.v_line = false;
    Seg prim{};
    for(const Seg& g : segs) {
        if(g.primary) {
            prim = g;
            continue;
        }
        KeepPrimary keep;
        S.extra.push_back(make_at(g.pos, g.van));
        remember(S.extra.back());
    }
    E.v_anchor = prim.van;
    set_cursor_offset(prim.pos);
    update_want();
}

std::vector<McMark> mc_marks() {
    check_buf();
    std::vector<McMark> v;
    for(const Cur& c : S.extra) v.push_back({at(c.pos), at(c.van)});
    return v;
}
