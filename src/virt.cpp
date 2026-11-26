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

#include "virt.hpp"
#include <algorithm>
#include <cstddef>
#include <iterator>
#include "piece.hpp"

NsId deco_ns(std::string_view name) {
    static std::vector<std::string> names;
    const auto it = std::ranges::find(names, name);
    if(it != names.end()) return static_cast<NsId>(it - names.begin());
    names.emplace_back(name);
    return static_cast<NsId>(names.size() - 1);
}

namespace {
// Apply edit [e.start, e.old_end) -> [e.start, e.new_end) to a strict span.
// Text typed at either edge or inside is left uncovered; what survives of the
// span keeps its colour. False: nothing of it is left.
bool cut_span(SpanDeco& s, const TextEdit& e, std::vector<SpanDeco>& halves) {
    const auto d = static_cast<std::ptrdiff_t>(e.new_end) -
                   static_cast<std::ptrdiff_t>(e.old_end);
    const auto moved = [&](size_t p) {
        return static_cast<size_t>(static_cast<std::ptrdiff_t>(p) + d);
    };
    if(s.end <= e.start) return true;  // wholly before the edit
    if(s.start >= e.old_end) {         // wholly after it (or typed in front)
        s.start = moved(s.start);
        s.end = moved(s.end);
        return true;
    }
    const bool left = e.start > s.start, right = s.end > e.old_end;
    if(left && right) {
        SpanDeco r = s;
        r.start = e.new_end;
        r.end = moved(s.end);
        halves.push_back(std::move(r));
        s.end = e.start;
    } else if(left) {
        s.end = e.start;
    } else if(right) {
        s.start = e.new_end;
        s.end = moved(s.end);
    } else {
        return false;
    }
    return true;
}
}  // namespace

void DecoStore::apply(const TextEdit& e) {
    if(e.reset) {
        _eol.clear();
        _spans.clear();
        _max_len = 0;
        return;
    }
    std::vector<SpanDeco> halves;  // right halves of strict spans an edit split
    size_t w = 0;
    for(size_t i = 0; i < _spans.size(); ++i) {
        SpanDeco& s = _spans[i];
        if(s.strict) {
            if(!cut_span(s, e, halves)) continue;  // swallowed whole: drop it
        } else {
            s.start = shift_pos(s.start, e, false);
            s.end = std::max(shift_pos(s.end, e, true), s.start);
        }
        if(w != i) _spans[w] = std::move(s);
        ++w;
    }
    _spans.erase(_spans.begin() + static_cast<std::ptrdiff_t>(w), _spans.end());
    _spans.insert(_spans.end(), halves.begin(), halves.end());
    if(!std::ranges::is_sorted(_spans, {}, &SpanDeco::start))
        std::ranges::stable_sort(_spans, {}, &SpanDeco::start);
    size_t mx = 0;
    for(const SpanDeco& s : _spans) mx = std::max(mx, s.end - s.start);
    _max_len = mx;
    auto it = std::ranges::upper_bound(_eol, e.start, {}, &EolDeco::pos);
    for(; it != _eol.end(); ++it) it->pos = shift_pos(it->pos, e, false);
}

void DecoStore::add(EolDeco d) {
    const auto it = std::ranges::upper_bound(_eol, d.pos, {}, &EolDeco::pos);
    _eol.insert(it, std::move(d));
}

void DecoStore::clear_ns(NsId ns) {
    std::erase_if(_eol, [&](const EolDeco& d) { return d.ns == ns; });
    std::erase_if(_spans, [&](const SpanDeco& d) { return d.ns == ns; });
}

void DecoStore::replace_ns_spans(NsId ns, std::vector<SpanDeco> spans) {
    std::erase_if(_eol, [&](const EolDeco& d) { return d.ns == ns; });
    std::erase_if(_spans, [&](const SpanDeco& d) { return d.ns == ns; });
    if(spans.empty()) return;
    if(!std::ranges::is_sorted(spans, {}, &SpanDeco::start))
        std::ranges::stable_sort(spans, {}, &SpanDeco::start);
    for(const SpanDeco& s : spans)
        _max_len = std::max(_max_len, s.end > s.start ? s.end - s.start : 0);
    std::vector<SpanDeco> out;
    out.reserve(_spans.size() + spans.size());
    // stable: on equal starts the old spans stay first, like add_span
    std::ranges::merge(_spans,
                       spans,
                       std::back_inserter(out),
                       {},
                       &SpanDeco::start,
                       &SpanDeco::start);
    _spans = std::move(out);
}

void DecoStore::eol_on_line(size_t ls, size_t len,
                            std::vector<EolItem>& out) const {
    auto it = std::ranges::lower_bound(_eol, ls, {}, &EolDeco::pos);
    for(; it != _eol.end() && it->pos <= ls + len; ++it)
        out.push_back({it->prio, &it->vt});
}

void DecoStore::add_span(SpanDeco d) {
    _max_len = std::max(_max_len, d.end > d.start ? d.end - d.start : 0);
    const auto it =
        std::ranges::upper_bound(_spans, d.start, {}, &SpanDeco::start);
    _spans.insert(it, std::move(d));
}

void DecoStore::spans_on_line(size_t ls, size_t len,
                              std::vector<LineSpan>& out) const {
    const size_t le = ls + len;
    const size_t from = ls > _max_len ? ls - _max_len : 0;
    const auto first =
        std::ranges::lower_bound(_spans, from, {}, &SpanDeco::start);
    for(auto it = first; it != _spans.end(); ++it) {
        const SpanDeco& s = *it;
        if(s.start > le) break;
        if(s.end <= ls) continue;
        const size_t lo = std::max(s.start, ls) - ls;
        const size_t hi = std::min(s.end, le) - ls;
        if(hi > lo) out.push_back({lo, hi, s.prio, s.st});
    }
}

std::optional<Style> DecoStore::fill_on_line(size_t ls, size_t len) const {
    const size_t le = ls + len;
    const size_t from = ls > _max_len ? ls - _max_len : 0;
    std::vector<const SpanDeco*> hit;
    auto it = std::ranges::lower_bound(_spans, from, {}, &SpanDeco::start);
    for(; it != _spans.end(); ++it) {
        if(it->start > le) break;
        if(it->fill && it->end > ls) hit.push_back(&*it);
    }
    if(hit.empty()) return std::nullopt;
    std::ranges::stable_sort(hit, {}, &SpanDeco::prio);
    Style st{};
    for(const SpanDeco* s : hit) style_patch(st, s->st);
    return st;
}

void style_patch(Style& base, const Style& p) {
    constexpr uint32_t none = 0xFFFFFFFF;
    if(p.fg != none) base.fg = p.fg;
    if(p.bg != none) base.bg = p.bg;
    if(p.ul != none) base.ul = p.ul;
    base.attrs = static_cast<uint16_t>(base.attrs | p.attrs);
    if(p.attrs & ATTR_NODIM)
        base.attrs = static_cast<uint16_t>(base.attrs & ~ATTR_DIM);
}

void compose_spans(std::vector<LineSpan>& in, std::vector<LineSpan>& out) {
    out.clear();
    std::erase_if(in, [](const LineSpan& s) { return s.hi <= s.lo; });
    if(in.empty()) return;
    std::ranges::stable_sort(in, {}, &LineSpan::lo);
    std::vector<size_t> cuts;
    for(const LineSpan& s : in) {
        cuts.push_back(s.lo);
        cuts.push_back(s.hi);
    }
    std::ranges::sort(cuts);
    cuts.erase(std::unique(cuts.begin(), cuts.end()), cuts.end());
    std::vector<const LineSpan*> active;
    size_t next = 0;
    for(size_t i = 0; i + 1 < cuts.size(); ++i) {
        const size_t lo = cuts[i], hi = cuts[i + 1];
        while(next < in.size() && in[next].lo <= lo)
            active.push_back(&in[next++]);
        std::erase_if(active, [&](const LineSpan* s) { return s->hi <= lo; });
        if(active.empty()) continue;
        std::ranges::stable_sort(active, {}, &LineSpan::prio);
        Style st{};
        for(const LineSpan* s : active) style_patch(st, s->st);
        out.push_back({lo, hi, 0, st});
    }
}
