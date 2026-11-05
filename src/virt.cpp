#include "virt.hpp"
#include <algorithm>
#include <cstddef>
#include "piece.hpp"

NsId deco_ns(std::string_view name) {
    static std::vector<std::string> names;
    const auto it = std::ranges::find(names, name);
    if(it != names.end()) return static_cast<NsId>(it - names.begin());
    names.emplace_back(name);
    return static_cast<NsId>(names.size() - 1);
}

void DecoStore::apply(const TextEdit& e) {
    if(e.reset) {
        _eol.clear();
        _spans.clear();
        _max_len = 0;
        return;
    }
    size_t mx = 0;
    for(SpanDeco& s : _spans) {
        s.start = shift_pos(s.start, e, false);
        s.end = std::max(shift_pos(s.end, e, true), s.start);
        mx = std::max(mx, s.end - s.start);
    }
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

void style_patch(Style& base, const Style& p) {
    constexpr uint32_t none = 0xFFFFFFFF;
    if(p.fg != none) base.fg = p.fg;
    if(p.bg != none) base.bg = p.bg;
    if(p.ul != none) base.ul = p.ul;
    base.attrs = static_cast<uint16_t>(base.attrs | p.attrs);
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
