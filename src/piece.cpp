#include "piece.hpp"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

static size_t span_len(const std::vector<Piece>& v) {
    size_t n = 0;
    for(const Piece& p : v) n += p.len;
    return n;
}

PieceTable::PieceTable(std::string_view original)
    : _original(original) {
    for(size_t i = _original.find('\n'); i != std::string_view::npos;
        i = _original.find('\n', i + 1))
        _orig_lf.push_back(i);
    if(!original.empty()) _root = make({Src::Orig, 0, original.size()});
}

size_t PieceTable::count_lf(Src s, size_t off, size_t len) const {
    const auto& v = lf_index(s);
    const auto lo = std::lower_bound(v.begin(), v.end(), off);
    const auto hi = std::lower_bound(lo, v.end(), off + len);
    return static_cast<size_t>(hi - lo);
}

std::string_view PieceTable::bytes(const Piece& p) const {
    const std::string_view buf =
        (p.src == Src::Orig) ? _original : std::string_view(_add);
    return buf.substr(p.off, p.len);
}

uint64_t PieceTable::next_rand() {
    _rng ^= _rng >> 12;
    _rng ^= _rng << 25;
    _rng ^= _rng >> 27;
    return _rng * 0x2545F4914F6CDD1DULL;
}

PieceTable::Id PieceTable::make(Piece p) {
    Node node;
    node.p = p;
    node.prio = next_rand();
    node.lf = count_lf(p.src, p.off, p.len);
    node.sum_len = p.len;
    node.sum_lf = node.lf;
    if(!_free.empty()) {
        const Id id = _free.back();
        _free.pop_back();
        _n[id] = node;
        return id;
    }
    _n.push_back(node);
    return static_cast<Id>(_n.size() - 1);
}

void PieceTable::release(Id t) {
    if(t == NIL) return;
    release(_n[t].l);
    release(_n[t].r);
    _free.push_back(t);
}

void PieceTable::gather(Id t, std::vector<Piece>& out) const {
    if(t == NIL) return;
    gather(_n[t].l, out);
    out.push_back(_n[t].p);
    gather(_n[t].r, out);
}

void PieceTable::pull(Id t) {
    Node& node = _n[t];
    node.sum_len = node.p.len + len_of(node.l) + len_of(node.r);
    node.sum_lf = node.lf + lf_of(node.l) + lf_of(node.r);
}

PieceTable::Id PieceTable::merge(Id a, Id b) {
    if(a == NIL) return b;
    if(b == NIL) return a;
    if(_n[a].prio > _n[b].prio) {
        _n[a].r = merge(_n[a].r, b);
        pull(a);
        return a;
    }
    _n[b].l = merge(a, _n[b].l);
    pull(b);
    return b;
}

std::pair<PieceTable::Id, PieceTable::Id> PieceTable::split(Id t, size_t pos) {
    if(t == NIL) return {NIL, NIL};
    const size_t lsz = len_of(_n[t].l);
    const size_t plen = _n[t].p.len;
    if(pos <= lsz) {
        auto [a, b] = split(_n[t].l, pos);
        _n[t].l = b;
        pull(t);
        return {a, t};
    }
    if(pos >= lsz + plen) {
        auto [a, b] = split(_n[t].r, pos - lsz - plen);
        _n[t].r = a;
        pull(t);
        return {t, b};
    }
    const size_t k = pos - lsz;
    const Piece old = _n[t].p;
    const Id oldr = _n[t].r;
    const Id tail = make({old.src, old.off + k, old.len - k});
    _n[t].p.len = k;
    _n[t].lf -= _n[tail].lf;
    _n[t].r = NIL;
    pull(t);
    return {t, merge(tail, oldr)};
}

bool PieceTable::try_expanded(Id t, size_t add_off, size_t n) {
    if(t == NIL) return false;
    std::vector<Id> path;
    for(Id c = t; c != NIL; c = _n[c].r) path.push_back(c);
    Node& last = _n[path.back()];
    if(last.p.src != Src::Add || last.p.off + last.p.len != add_off)
        return false;
    last.p.len += n;
    last.lf += count_lf(Src::Add, add_off, n);
    for(auto it = path.rbegin(); it != path.rend(); ++it) pull(*it);
    return true;
}

size_t PieceTable::raw_insert(size_t pos, std::string_view text) {
    const size_t add_off = _add.size();
    _add.append(text);
    for(size_t i = text.find('\n'); i != std::string_view::npos;
        i = text.find('\n', i + 1))
        _add_lf.push_back(add_off + i);
    auto [l, r] = split(_root, pos);
    if(!try_expanded(l, add_off, text.size()))
        l = merge(l, make({Src::Add, add_off, text.size()}));
    _root = merge(l, r);
    ++_rev;
    return add_off;
}

void PieceTable::raw_remove(size_t pos, size_t n, std::vector<Piece>* removed) {
    auto [l, mr] = split(_root, pos);
    auto [m, r] = split(mr, n);
    if(removed) gather(m, *removed);
    release(m);
    _root = merge(l, r);
    ++_rev;
}

void PieceTable::raw_insert_pieces(size_t pos, const std::vector<Piece>& ps) {
    auto [l, r] = split(_root, pos);
    Id mid = NIL;
    for(const Piece& p : ps) mid = merge(mid, make(p));
    _root = merge(merge(l, mid), r);
    ++_rev;
}

void PieceTable::replace_span(size_t pos, size_t del,
                              const std::vector<Piece>& ins) {
    if(del > 0) raw_remove(pos, del, nullptr);
    if(!ins.empty()) raw_insert_pieces(pos, ins);
}

void PieceTable::begin_group() {
    if(_depth++ == 0) _rev_open = false;
}

void PieceTable::end_group() {
    if(_depth == 0) return;
    if(--_depth == 0 && _rev_open) {
        _head = _hist.size();
        _rev_open = false;
        trim_history();
    }
}

void PieceTable::trim_history() {
    if(_max_hist == 0 || _hist.size() <= _max_hist) return;
    const size_t drop = std::min(_hist.size() - _max_hist, _head);
    if(drop == 0) return;
    _hist.erase(_hist.begin(),
                _hist.begin() + static_cast<std::ptrdiff_t>(drop));
    _head -= drop;
    if(_saved_head != npos)
        _saved_head = _saved_head < drop ? npos : _saved_head - drop;
}

void PieceTable::set_history_limit(size_t n) {
    _max_hist = n;
    if(_depth == 0) trim_history();
}

PieceTable::Revision& PieceTable::open_rev() {
    if(!_rev_open) {
        if(_saved_head != npos && _saved_head > _head) _saved_head = npos;
        _hist.erase(_hist.begin() + static_cast<std::ptrdiff_t>(_head),
                    _hist.end());
        _hist.emplace_back();
        _rev_open = true;
    }
    return _hist.back();
}

void PieceTable::record_insert(size_t pos, size_t add_off, size_t len) {
    Revision& rev = open_rev();
    if(!rev.edits.empty()) {
        Edit& e = rev.edits.back();
        if(e.removed.empty() && e.inserted.size() == 1) {
            Piece& ip = e.inserted[0];
            if(ip.src == Src::Add && ip.off + ip.len == add_off &&
               e.pos + ip.len == pos) {
                ip.len += len;
                return;
            }
        }
    }
    rev.edits.push_back({pos, {}, {Piece{Src::Add, add_off, len}}});
}

namespace {
size_t u16_units(std::string_view s) {
    size_t n = 0;
    for(const char ch : s) {
        const auto b = static_cast<unsigned char>(ch);
        if((b ^ 0xC0) != 0x80) ++n;
        if(b >= 0xF0) ++n;
    }
    return n;
}

struct Pt {
    size_t off, line, col, u16;
};
Pt pt_start(const TextEdit& e) {
    return {e.start, e.start_pt.line, e.start_pt.col, e.start_u16};
}
Pt pt_old(const TextEdit& e) {
    return {e.old_end, e.old_end_pt.line, e.old_end_pt.col, e.old_end_u16};
}
Pt pt_new(const TextEdit& e) {
    return {e.new_end, e.new_end_pt.line, e.new_end_pt.col, e.new_end_u16};
}

Pt shift(const Pt& p, const Pt& from, const Pt& to) {
    Pt r{p.off - from.off + to.off, 0, 0, 0};
    if(p.line == from.line) {
        r.line = to.line;
        r.col = p.col - from.col + to.col;
        r.u16 = p.u16 - from.u16 + to.u16;
    } else {
        r.line = p.line - from.line + to.line;
        r.col = p.col;
        r.u16 = p.u16;
    }
    return r;
}

}  // namespace

TextEdit merge_edits(const TextEdit& a, const TextEdit& b) {
    TextEdit c;
    if(a.reset || b.reset) {
        c.reset = true;
        return c;
    }
    const Pt a_old = pt_old(a), a_new = pt_new(a), b_old = pt_old(b),
             b_new = pt_new(b);
    const Pt s = b.start < a.start ? pt_start(b) : pt_start(a);
    const Pt oe = b_old.off > a_new.off ? shift(b_old, a_new, a_old) : a_old;
    const Pt ne = b_old.off >= a_new.off ? b_new : shift(a_new, b_old, b_new);
    c.start = s.off;
    c.start_pt = {s.line, s.col};
    c.start_u16 = s.u16;
    c.old_end = oe.off;
    c.old_end_pt = {oe.line, oe.col};
    c.old_end_u16 = oe.u16;
    c.new_end = ne.off;
    c.new_end_pt = {ne.line, ne.col};
    c.new_end_u16 = ne.u16;
    return c;
}

void PieceTable::lsp_track(bool on, bool utf16) {
    _lsp_on = on;
    _lsp_u16 = on && utf16;
    _lsp_pending.reset();
}

std::optional<TextEdit> PieceTable::take_lsp_edit() {
    return std::exchange(_lsp_pending, std::nullopt);
}

void PieceTable::note_lsp(const TextEdit& e) {
    _lsp_pending = _lsp_pending ? merge_edits(*_lsp_pending, e) : e;
}

size_t PieceTable::u16_range(size_t pos, size_t n) const {
    size_t units = 0;
    for_each_chunk(pos, n, [&](std::string_view s) { units += u16_units(s); });
    return units;
}

void PieceTable::log_edit(const TextEdit& e) {
    if(_lsp_on) note_lsp(e);
    if(!_journal.empty() && _journal.back().reset) return;
    if(e.reset || _journal.size() >= kJournalCap) {
        TextEdit r;
        r.reset = true;
        _journal.assign(1, r);
        return;
    }
    _journal.push_back(e);
}

std::optional<size_t> PieceTable::undo() {
    if(!can_undo()) return std::nullopt;
    const Revision& rev = _hist[--_head];
    for(auto it = rev.edits.rbegin(); it != rev.edits.rend(); ++it)
        replace_span(it->pos, span_len(it->inserted), it->removed);
    TextEdit r;
    r.reset = true;
    log_edit(r);
    size_t first = size();
    for(const auto& e : rev.edits) first = std::min(first, e.pos);
    return std::min(first, size());
}

std::optional<size_t> PieceTable::redo() {
    if(!can_redo()) return std::nullopt;
    const Revision& rev = _hist[_head++];
    for(const Edit& e : rev.edits)
        replace_span(e.pos, span_len(e.removed), e.inserted);
    TextEdit r;
    r.reset = true;
    log_edit(r);
    return std::min(rev.edits.front().pos, size());
}

PieceTable& PieceTable::insert(size_t pos, std::string_view text) {
    if(text.empty()) return *this;
    pos = std::min(pos, size());
    const LineCol sp = to_linecol(pos);
    const size_t su = _lsp_u16 ? u16_col(sp) : 0;
    Group g(*this);
    const size_t add_off = raw_insert(pos, text);
    record_insert(pos, add_off, text.size());
    TextEdit te;
    te.start = te.old_end = pos;
    te.new_end = pos + text.size();
    te.start_pt = te.old_end_pt = sp;
    const size_t last_nl = text.rfind('\n');
    if(last_nl == std::string_view::npos) {
        te.new_end_pt = {sp.line, sp.col + text.size()};
    } else {
        const size_t nls =
            static_cast<size_t>(std::count(text.begin(), text.end(), '\n'));
        te.new_end_pt = {sp.line + nls, text.size() - (last_nl + 1)};
    }
    if(_lsp_u16) {
        te.start_u16 = te.old_end_u16 = su;
        te.new_end_u16 = last_nl == std::string_view::npos
                             ? su + u16_units(text)
                             : u16_units(text.substr(last_nl + 1));
    }
    log_edit(te);
    return *this;
}

PieceTable& PieceTable::remove(size_t pos, size_t n) {
    const size_t total = size();
    if(pos >= total || n == 0) return *this;
    n = std::min(n, total - pos);
    const LineCol sp = to_linecol(pos), ep = to_linecol(pos + n);
    size_t su = 0, eu = 0;
    if(_lsp_u16) {
        su = u16_col(sp);
        eu = ep.line == sp.line ? su + u16_range(pos, n) : u16_col(ep);
    }
    Group g(*this);
    std::vector<Piece> removed;
    raw_remove(pos, n, &removed);

    TextEdit te;
    te.start = te.new_end = pos;
    te.old_end = pos + n;
    te.start_pt = te.new_end_pt = sp;
    te.old_end_pt = ep;
    te.start_u16 = te.new_end_u16 = su;
    te.old_end_u16 = eu;
    log_edit(te);
    Revision& rev = open_rev();
    if(!rev.edits.empty()) {
        Edit& e = rev.edits.back();
        if(e.inserted.empty() && !e.removed.empty()) {
            if(pos + n == e.pos) {
                removed.insert(
                    removed.end(), e.removed.begin(), e.removed.end());
                e.removed = std::move(removed);
                e.pos = pos;
                return *this;
            }
            if(pos == e.pos) {
                e.removed.insert(
                    e.removed.end(), removed.begin(), removed.end());
                return *this;
            }
        }
    }
    rev.edits.push_back({pos, std::move(removed), {}});
    return *this;
}

void PieceTable::collect(Id t, size_t lo, size_t hi, std::string& out) const {
    if(t == NIL || lo >= hi) return;
    const Node& n = _n[t];
    const size_t lsz = len_of(n.l);
    const size_t mid = lsz + n.p.len;
    if(lo < lsz) collect(n.l, lo, std::min(hi, lsz), out);
    if(hi > lsz && lo < mid) {
        const size_t a = lo > lsz ? lo - lsz : 0;
        const size_t b = std::min(hi, mid) - lsz;
        out.append(bytes(n.p).substr(a, b - a));
    }
    if(hi > mid) collect(n.r, lo > mid ? lo - mid : 0, hi - mid, out);
}

std::string PieceTable::text() const {
    std::string out;
    out.reserve(size());
    collect(_root, 0, size(), out);
    return out;
}

std::string PieceTable::text(size_t pos, size_t n) const {
    std::string out;
    if(pos >= size()) return out;
    n = std::min(n, size() - pos);
    collect(_root, pos, pos + n, out);
    return out;
}

void PieceTable::chunks(Id t, size_t lo, size_t hi,
                        const std::function<void(std::string_view)>& fn) const {
    if(t == NIL || lo >= hi) return;
    const Node& n = _n[t];
    const size_t lsz = len_of(n.l);
    const size_t mid = lsz + n.p.len;
    if(lo < lsz) chunks(n.l, lo, std::min(hi, lsz), fn);
    if(hi > lsz && lo < mid) {
        const size_t a = lo > lsz ? lo - lsz : 0;
        const size_t b = std::min(hi, mid) - lsz;
        fn(bytes(n.p).substr(a, b - a));
    }
    if(hi > mid) chunks(n.r, lo > mid ? lo - mid : 0, hi - mid, fn);
}

void PieceTable::for_each_chunk(
    size_t pos, size_t n,
    const std::function<void(std::string_view)>& fn) const {
    if(pos >= size()) return;
    n = std::min(n, size() - pos);
    chunks(_root, pos, pos + n, fn);
}

char PieceTable::at(size_t pos) const {
    Id t = _root;
    while(t != NIL) {
        const Node& n = _n[t];
        size_t lsz = len_of(n.l);
        if(pos < lsz) t = n.l;
        else if(pos < lsz + n.p.len) return bytes(n.p)[pos - lsz];
        else {
            pos -= lsz + n.p.len;
            t = n.r;
        }
    }
    return '\0';
}

std::string_view PieceTable::chunk_at(size_t pos) const {
    Id t = _root;
    while(t != NIL) {
        const Node& n = _n[t];
        const size_t lsz = len_of(n.l);
        if(pos < lsz) t = n.l;
        else if(pos < lsz + n.p.len) return bytes(n.p).substr(pos - lsz);
        else {
            pos -= lsz + n.p.len;
            t = n.r;
        }
    }
    return {};
}

size_t PieceTable::nth_lf(Id t, size_t k) const {
    size_t base = 0;
    while(t != NIL) {
        const Node& n = _n[t];
        const size_t lf_l = lf_of(n.l);
        if(k < lf_l) {
            t = n.l;
            continue;
        }
        k -= lf_l;
        const size_t lsz = len_of(n.l);
        if(k < n.lf) {
            const auto& v = lf_index(n.p.src);
            const auto first = std::lower_bound(v.begin(), v.end(), n.p.off);
            const size_t abs_lf = *(first + static_cast<std::ptrdiff_t>(k));
            return base + lsz + (abs_lf - n.p.off);
        }
        k -= n.lf;
        base += lsz + n.p.len;
        t = n.r;
    }
    return npos;
}

size_t PieceTable::line_start(size_t line) const {
    if(line == 0) return 0;
    if(line >= line_count()) return npos;
    return nth_lf(_root, line - 1) + 1;
}

size_t PieceTable::line_of(size_t pos) const {
    pos = std::min(pos, size());
    size_t lines = 0;
    Id t = _root;
    while(t != NIL) {
        const Node& n = _n[t];
        size_t lsz = len_of(n.l);
        if(pos < lsz) {
            t = n.l;
            continue;
        }
        lines += lf_of(n.l);
        pos -= lsz;
        if(pos < n.p.len) { return lines + count_lf(n.p.src, n.p.off, pos); }
        lines += n.lf;
        pos -= n.p.len;
        t = n.r;
    }
    return lines;
}

size_t PieceTable::line_end(size_t line) const {
    if(line >= line_count()) return npos;
    if(line + 1 == line_count()) return size();
    return line_start(line + 1) - 1;
}

std::string PieceTable::line_text(size_t line) const {
    const size_t s = line_start(line);
    if(s == npos) return {};
    return text(s, line_end(line) - s);
}

LineCol PieceTable::to_linecol(size_t pos) const {
    pos = std::min(pos, size());
    const size_t line = line_of(pos);
    return {line, pos - line_start(line)};
}

size_t PieceTable::from_linecol(size_t line, size_t col) const {
    line = std::min(line, line_count() - 1);
    return std::min(line_start(line) + col, line_end(line));
}
