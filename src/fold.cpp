#include "fold.hpp"

#include <algorithm>
#include <cstdint>

namespace fold {

std::vector<Raw> run_providers(const std::vector<Provider>& ps) {
    for(const Provider& p : ps) {
        if(!p) continue;
        std::vector<Raw> r = p();
        if(!r.empty()) return r;
    }
    return {};
}

std::vector<Raw> indent_folds(int nlines,
                              const std::function<int(int)>& indent_of) {
    struct Open {
        int line, ind;
    };
    std::vector<Raw> out;
    std::vector<Open> st;
    int prev_nb = -1;
    const auto pop_to = [&](int ind) {
        while(!st.empty() && st.back().ind >= ind) {
            if(prev_nb > st.back().line) out.push_back({st.back().line, prev_nb});
            st.pop_back();
        }
    };
    for(int i = 0; i < nlines; ++i) {
        const int d = indent_of(i);
        if(d < 0) continue;  // blank lines belong to whatever surrounds them
        pop_to(d);
        st.push_back({i, d});
        prev_nb = i;
    }
    pop_to(-1);
    return out;
}

std::vector<Raw> normalize(std::vector<Raw> raw, int min_lines) {
    std::erase_if(raw, [](const Raw& r) { return r.start < 0 || r.end <= r.start; });
    std::ranges::sort(raw, [](const Raw& a, const Raw& b) {
        return a.start != b.start ? a.start < b.start : a.end > b.end;
    });
    // same start: the outermost (first after the sort) wins
    raw.erase(std::ranges::unique(raw,
                                  [](const Raw& a, const Raw& b) {
                                      return a.start == b.start;
                                  })
                  .begin(),
              raw.end());
    // `} else {`: a body whose last line is its sibling's head stops short, so
    // closing it doesn't swallow the sibling's header
    for(Raw& r : raw) {
        const auto it = std::ranges::lower_bound(
            raw, r.end, {}, [](const Raw& x) { return x.start; });
        if(it != raw.end() && it->start == r.end && it->end > r.end) --r.end;
    }
    const int ml = std::max(min_lines, 1);
    std::erase_if(raw, [&](const Raw& r) { return r.end - r.start < ml; });
    // partial overlaps: the later fold is clipped to its container
    std::vector<Raw> out;
    std::vector<int> st;  // indices into `out`
    for(Raw r : raw) {
        while(!st.empty() && out[static_cast<size_t>(st.back())].end < r.start)
            st.pop_back();
        if(!st.empty()) r.end = std::min(r.end, out[static_cast<size_t>(st.back())].end);
        if(r.end - r.start < ml) continue;
        st.push_back(static_cast<int>(out.size()));
        out.push_back(r);
    }
    return out;
}

// ---- State -----------------------------------------------------------------

void State::assign(std::vector<Raw> raw, int min_lines) {
    min_lines_ = min_lines;
    raw = normalize(std::move(raw), min_lines);
    ranges_.clear();
    ranges_.reserve(raw.size());
    std::vector<int> st;
    for(const Raw& r : raw) {
        while(!st.empty() && ranges_[static_cast<size_t>(st.back())].end < r.start)
            st.pop_back();
        const int idx = static_cast<int>(ranges_.size());
        ranges_.push_back({r.start, r.end, static_cast<int>(st.size()),
                           st.empty() ? -1 : st.back()});
        st.push_back(idx);
    }
}

void State::set_ranges(std::vector<Raw> raw, int min_lines, int level_start) {
    assign(std::move(raw), min_lines);
    stale = false;
    if(!level_applied_ && !ranges_.empty()) {
        level_applied_ = true;
        if(level_start >= 0) {
            apply_level(level_start);
            return;
        }
    }
    rebuild();
}

int State::max_level() const {
    int m = -1;
    for(const Range& r : ranges_) m = std::max(m, r.level);
    return m;
}

void State::clear() {
    ranges_.clear();
    closed_.clear();
    level_ = INT_MAX;
    level_applied_ = false;
    stale = true;
    rebuild();
}

void State::clamp_lines(int total) {
    std::vector<Raw> raw;
    bool cut = false;
    for(const Range& r : ranges_) {
        if(r.end >= total) cut = true;
        raw.push_back({r.start, std::min(r.end, total - 1)});
    }
    const size_t before = closed_.size();
    std::erase_if(closed_, [&](int l) { return l >= total; });
    if(!cut && closed_.size() == before) return;
    assign(std::move(raw), min_lines_);
    rebuild();
}

void State::rebuild() {
    spans_.clear();
    before_.clear();
    hidden_total_ = 0;
    if(enabled_) {
        int cur_end = -1;
        for(const Range& r : ranges_) {
            if(r.start <= cur_end || !is_anchor(r.start)) continue;
            spans_.push_back({r.start, r.end});
            before_.push_back(hidden_total_);
            hidden_total_ += r.end - r.start;
            cur_end = r.end;
        }
    }
    ++gen_;
}

bool State::is_anchor(int line) const {
    return std::ranges::binary_search(closed_, line);
}

void State::add_anchor(int line) {
    const auto it = std::ranges::lower_bound(closed_, line);
    if(it == closed_.end() || *it != line) closed_.insert(it, line);
}

bool State::del_anchor(int line) {
    const auto it = std::ranges::lower_bound(closed_, line);
    if(it == closed_.end() || *it != line) return false;
    closed_.erase(it);
    return true;
}

// ---- edits -----------------------------------------------------------------
//
// Lines that survive an edit keep their identity; the rest are gone.
//   start at column 0:   lines [start_row, old_end_row) are gone, everything
//                        from old_end_row on moves (the text after the edit
//                        carries the line's identity). With start_row ==
//                        old_end_row nothing is gone and the line itself moves
//                        down by the inserted line count: `O` on a head line
//                        pushes the head down.
//   start past column 0: the line keeps its prefix, so it stays; lines
//                        (start_row, old_end_row] are gone.
void State::shift(const Edit& e) {
    const int delta = e.new_end_row - e.old_end_row;
    if(delta == 0 && e.start_row == e.old_end_row) return;  // inside one line
    const bool col0 = e.start_col == 0;
    const int gone_lo = col0 ? e.start_row : e.start_row + 1;
    const int gone_hi = col0 ? e.old_end_row : e.old_end_row + 1;  // exclusive
    const auto gone = [&](int l) { return l >= gone_lo && l < gone_hi; };
    const auto map = [&](int l) { return l >= gone_hi ? l + delta : l; };

    std::vector<Raw> raw;
    raw.reserve(ranges_.size());
    for(const Range& r : ranges_) {
        if(gone(r.start)) continue;
        const int s = map(r.start);
        const int en = gone(r.end) ? gone_lo - 1 : map(r.end);
        if(en > s) raw.push_back({s, en});
    }
    assign(std::move(raw), min_lines_);

    std::vector<int> keep;
    keep.reserve(closed_.size());
    for(const int l : closed_)
        if(!gone(l)) keep.push_back(map(l));
    keep.erase(std::ranges::unique(keep).begin(), keep.end());
    closed_ = std::move(keep);
    stale = true;
    rebuild();
}

// ---- chains ----------------------------------------------------------------

int State::innermost(int line) const {
    const auto it = std::ranges::partition_point(
        ranges_, [&](const Range& r) { return r.start <= line; });
    int i = static_cast<int>(it - ranges_.begin()) - 1;
    while(i >= 0 && ranges_[static_cast<size_t>(i)].end < line)
        i = ranges_[static_cast<size_t>(i)].parent;
    return i;
}

std::vector<int> State::chain(int line) const {
    std::vector<int> c;
    for(int i = innermost(line); i >= 0; i = ranges_[static_cast<size_t>(i)].parent)
        c.push_back(i);
    return c;
}

// ---- commands --------------------------------------------------------------

bool State::open(int line) {
    const Span* s = span_at(line);
    if(!s) return false;
    del_anchor(s->head);
    rebuild();
    return true;
}

bool State::close(int line) {
    for(const int i : chain(line)) {
        if(closed(i)) continue;
        if(hidden(ranges_[static_cast<size_t>(i)].start)) continue;
        add_anchor(ranges_[static_cast<size_t>(i)].start);
        rebuild();
        return true;
    }
    return false;
}

bool State::toggle(int line) { return span_at(line) ? open(line) : close(line); }

bool State::open_rec(int line) {
    const int in = innermost(line);
    if(in < 0) return false;
    bool any = false;
    for(const int i : chain(line)) any |= del_anchor(ranges_[static_cast<size_t>(i)].start);
    const int end = ranges_[static_cast<size_t>(in)].end;
    for(size_t j = static_cast<size_t>(in) + 1;
        j < ranges_.size() && ranges_[j].start <= end;
        ++j)
        any |= del_anchor(ranges_[j].start);
    if(any) rebuild();
    return any;
}

bool State::close_rec(int line) {
    bool any = false;
    for(const int i : chain(line)) {
        const int s = ranges_[static_cast<size_t>(i)].start;
        if(!is_anchor(s)) {
            add_anchor(s);
            any = true;
        }
    }
    if(any) rebuild();
    return any;
}

bool State::toggle_rec(int line) {
    return span_at(line) ? open_rec(line) : close_rec(line);
}

bool State::reveal(int line) {
    bool any = false;
    for(const int i : chain(line)) any |= del_anchor(ranges_[static_cast<size_t>(i)].start);
    if(any) rebuild();
    return any;
}

void State::apply_level(int lvl) {
    closed_.clear();
    for(const Range& r : ranges_)
        if(r.level >= lvl) closed_.push_back(r.start);
    level_ = lvl;
    rebuild();
}

bool State::open_all() {
    const bool had = !closed_.empty();
    closed_.clear();
    level_ = INT_MAX;
    rebuild();
    return had;
}

bool State::close_all() {
    apply_level(0);
    return !ranges_.empty();
}

bool State::more(int n) {
    if(ranges_.empty()) return false;
    const int cur = std::min(level_, max_level() + 1);
    apply_level(std::max(0, cur - std::max(n, 1)));
    return true;
}

bool State::fewer(int n) {
    if(ranges_.empty()) return false;
    const int cur = std::min(level_, max_level() + 1);
    const int to = cur + std::max(n, 1);
    if(to > max_level()) {
        open_all();
        return true;
    }
    apply_level(to);
    return true;
}

bool State::set_enabled(bool on) {
    if(on == enabled_) return false;
    enabled_ = on;
    rebuild();
    return true;
}

// ---- navigation ------------------------------------------------------------

std::optional<int> State::next_fold_start(int line) const {
    const int from = end_of(line);
    auto it = std::ranges::partition_point(
        ranges_, [&](const Range& r) { return r.start <= from; });
    for(; it != ranges_.end(); ++it)
        if(!hidden(it->start)) return it->start;
    return std::nullopt;
}

std::optional<int> State::prev_fold_end(int line) const {
    const int h = head_of(line);
    std::optional<int> best;
    for(const Range& r : ranges_) {
        if(r.start >= h) break;
        const int e = head_of(r.end);
        if(e < h && (!best || e > *best)) best = e;
    }
    return best;
}

std::optional<int> State::enclosing_start(int line) const {
    const int h = head_of(line);
    for(const int i : chain(h)) {
        const Range& r = ranges_[static_cast<size_t>(i)];
        if(r.start < h) return r.start;
    }
    return std::nullopt;
}

std::optional<int> State::enclosing_end(int line) const {
    const int h = head_of(line);
    for(const int i : chain(h)) {
        const int e = head_of(ranges_[static_cast<size_t>(i)].end);
        if(e > h) return e;
    }
    return std::nullopt;
}

// ---- visibility ------------------------------------------------------------

int State::span_idx(int line) const {
    const auto it = std::ranges::partition_point(
        spans_, [&](const Span& s) { return s.head <= line; });
    return static_cast<int>(it - spans_.begin()) - 1;
}

const Span* State::span_at(int line) const {
    const int i = span_idx(line);
    if(i < 0 || line > spans_[static_cast<size_t>(i)].end) return nullptr;
    return &spans_[static_cast<size_t>(i)];
}

bool State::hidden(int line) const {
    const Span* s = span_at(line);
    return s && line > s->head;
}

int State::head_of(int line) const {
    const Span* s = span_at(line);
    return s ? s->head : line;
}

int State::end_of(int line) const {
    const Span* s = span_at(line);
    return s ? s->end : line;
}

int State::next_visible(int line) const { return end_of(line) + 1; }

int State::prev_visible(int line) const {
    const int h = head_of(line);
    return h == 0 ? -1 : head_of(h - 1);
}

int State::visible_row(int line) const {
    const int i = span_idx(line);
    if(i < 0) return line;
    const Span& s = spans_[static_cast<size_t>(i)];
    const int b = before_[static_cast<size_t>(i)];
    return line <= s.end ? s.head - b : line - b - (s.end - s.head);
}

int State::line_at_row(int row) const {
    // head row of span j is head_j - before_j: strictly increasing
    const auto it = std::partition_point(
        spans_.begin(), spans_.end(), [&](const Span& s) {
            const size_t j = static_cast<size_t>(&s - spans_.data());
            return s.head - before_[j] < row;
        });
    const size_t k = static_cast<size_t>(it - spans_.begin());
    // k spans start on a row above `row`: all their hidden lines come first
    const int hid = k == 0 ? 0 : before_[k - 1] + (spans_[k - 1].end - spans_[k - 1].head);
    return row + hid;
}

}  // namespace fold
