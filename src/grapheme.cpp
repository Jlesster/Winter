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

#include "grapheme.hpp"
#include <algorithm>
#include <unordered_map>
#include <vector>
#include "utf8.hpp"

namespace {
#include "grapheme_tables.inc"

// Extended_Pictographic (emoji-data.txt, Unicode 15): coarse but complete
// enough for GB11 and for deciding emoji width.
constexpr char32_t kExtPict[][2] = {
    {0xA9, 0xA9},       {0xAE, 0xAE},       {0x203C, 0x203C},
    {0x2049, 0x2049},   {0x2122, 0x2122},   {0x2139, 0x2139},
    {0x2194, 0x2199},   {0x21A9, 0x21AA},   {0x231A, 0x231B},
    {0x2328, 0x2328},   {0x2388, 0x2388},   {0x23CF, 0x23CF},
    {0x23E9, 0x23F3},   {0x23F8, 0x23FA},   {0x24C2, 0x24C2},
    {0x25AA, 0x25AB},   {0x25B6, 0x25B6},   {0x25C0, 0x25C0},
    {0x25FB, 0x25FE},   {0x2600, 0x2605},   {0x2607, 0x2612},
    {0x2614, 0x2685},   {0x2690, 0x2705},   {0x2708, 0x2712},
    {0x2714, 0x2714},   {0x2716, 0x2716},   {0x271D, 0x271D},
    {0x2721, 0x2721},   {0x2728, 0x2728},   {0x2733, 0x2734},
    {0x2744, 0x2744},   {0x2747, 0x2747},   {0x274C, 0x274C},
    {0x274E, 0x274E},   {0x2753, 0x2755},   {0x2757, 0x2757},
    {0x2763, 0x2767},   {0x2795, 0x2797},   {0x27A1, 0x27A1},
    {0x27B0, 0x27B0},   {0x27BF, 0x27BF},   {0x2934, 0x2935},
    {0x2B05, 0x2B07},   {0x2B1B, 0x2B1C},   {0x2B50, 0x2B50},
    {0x2B55, 0x2B55},   {0x3030, 0x3030},   {0x303D, 0x303D},
    {0x3297, 0x3297},   {0x3299, 0x3299},   {0x1F000, 0x1F0FF},
    {0x1F10D, 0x1F10F}, {0x1F12F, 0x1F12F}, {0x1F16C, 0x1F171},
    {0x1F17E, 0x1F17F}, {0x1F18E, 0x1F18E}, {0x1F191, 0x1F19A},
    {0x1F1AD, 0x1F1E5}, {0x1F201, 0x1F20F}, {0x1F21A, 0x1F21A},
    {0x1F22F, 0x1F22F}, {0x1F232, 0x1F23A}, {0x1F23C, 0x1F23F},
    {0x1F249, 0x1F3FA}, {0x1F400, 0x1F53D}, {0x1F546, 0x1F64F},
    {0x1F680, 0x1F6FF}, {0x1F774, 0x1F77F}, {0x1F7D5, 0x1F7FF},
    {0x1F80C, 0x1F80F}, {0x1F848, 0x1F84F}, {0x1F85A, 0x1F85F},
    {0x1F888, 0x1F88F}, {0x1F8AE, 0x1F8FF}, {0x1F90C, 0x1F93A},
    {0x1F93C, 0x1F945}, {0x1F947, 0x1FAFF}, {0x1FC00, 0x1FFFD},
};

template <size_t N>
bool in(const char32_t (&t)[N][2], char32_t c) {
    size_t lo = 0, hi = N;
    while(lo < hi) {
        const size_t mid = (lo + hi) / 2;
        if(c < t[mid][0]) hi = mid;
        else if(c > t[mid][1]) lo = mid + 1;
        else return true;
    }
    return false;
}

enum class P : uint8_t {
    Other, CR, LF, Control, Extend, ZWJ, RI, Prepend, Spacing,
    L, V, T, LV, LVT, Pict,
};

P prop(char32_t c) {
    if(c < 0x80) {
        if(c == '\r') return P::CR;
        if(c == '\n') return P::LF;
        return c < 0x20 || c == 0x7F ? P::Control : P::Other;
    }
    if(c == 0x200D) return P::ZWJ;
    if(c >= 0x1F1E6 && c <= 0x1F1FF) return P::RI;
    if(c >= 0x1100 && c <= 0x11FF) {
        if(c <= 0x115F) return P::L;
        if(c <= 0x11A7) return P::V;
        return P::T;
    }
    if(c >= 0xA960 && c <= 0xA97C) return P::L;
    if(c >= 0xD7B0 && c <= 0xD7C6) return P::V;
    if(c >= 0xD7CB && c <= 0xD7FB) return P::T;
    if(c >= 0xAC00 && c <= 0xD7A3) return (c - 0xAC00) % 28 == 0 ? P::LV : P::LVT;
    if(in(kGcbExtend, c)) return P::Extend;
    if(in(kGcbSpacing, c)) return P::Spacing;
    if(in(kGcbPrepend, c)) return P::Prepend;
    if(in(kGcbControl, c)) return P::Control;
    if(in(kExtPict, c)) return P::Pict;
    return P::Other;
}

bool is_pict(char32_t c) { return c >= 0xA9 && in(kExtPict, c); }

// Is there a break between a (state) and b?
struct State {
    P prev = P::Other;
    bool pict = false;  // GB11: Pict Extend* so far
    bool zwj = false;   // ... and then a ZWJ
    int ri = 0;         // regional indicators in the current run
};

void feed(State& st, P p) {
    st.zwj = st.pict && p == P::ZWJ;
    if(p == P::Pict) st.pict = true;
    else if(p != P::Extend && p != P::ZWJ) st.pict = false;
    st.ri = p == P::RI ? st.ri + 1 : 0;
    st.prev = p;
}

bool brk(const State& st, P b) {
    const P a = st.prev;
    if(a == P::CR && b == P::LF) return false;                          // GB3
    if(a == P::Control || a == P::CR || a == P::LF) return true;        // GB4
    if(b == P::Control || b == P::CR || b == P::LF) return true;        // GB5
    if(a == P::L && (b == P::L || b == P::V || b == P::LV || b == P::LVT))
        return false;                                                   // GB6
    if((a == P::LV || a == P::V) && (b == P::V || b == P::T)) return false;
    if((a == P::LVT || a == P::T) && b == P::T) return false;           // GB8
    if(b == P::Extend || b == P::ZWJ) return false;                     // GB9
    if(b == P::Spacing) return false;                                   // GB9a
    if(a == P::Prepend) return false;                                   // GB9b
    if(st.zwj && b == P::Pict) return false;                            // GB11
    if(a == P::RI && b == P::RI && st.ri % 2 == 1) return false;        // GB12/13
    return true;
}

struct Tails {
    std::vector<std::u32string> v{std::u32string{}};
    std::unordered_map<std::u32string, uint32_t> m;
};
Tails& tails() {
    static Tails t;
    return t;
}
constexpr size_t kMaxTails = 1u << 16;

}  // namespace

int gr_cp_width(char32_t c) {
    if(c < 0x300) {
        if(c < 0x20 || (c >= 0x7F && c < 0xA0)) return 0;
        return 1;  // incl. U+00AD, which terminals draw
    }
    if(c == 0x200D || (c >= 0x200B && c <= 0x200F) || c == 0x2060 ||
       c == 0xFEFF)
        return 0;
    if(in(kGcbExtend, c) || in(kGcbControl, c)) return 0;
    if(in(kEaWide, c)) return 2;
    return 1;
}

size_t gr_next(std::string_view s, size_t i) {
    const size_t n = s.size();
    if(i >= n) return n;
    const unsigned char b0 = static_cast<unsigned char>(s[i]);
    if(b0 < 0x80 && b0 != '\r' &&
       (i + 1 >= n || static_cast<unsigned char>(s[i + 1]) < 0x80))
        return i + 1;  // ASCII then ASCII: always a boundary
    Decoded d = utf8_decode(s, i);
    State st;
    feed(st, prop(d.cp));
    if(st.prev == P::Pict) st.pict = true;
    size_t j = i + d.len;
    while(j < n) {
        d = utf8_decode(s, j);
        const P q = prop(d.cp);
        if(brk(st, q)) break;
        feed(st, q);
        j += d.len;
    }
    return j;
}

size_t gr_prev(std::string_view s, size_t i) {
    if(i == 0) return 0;
    if(i > s.size()) i = s.size();
    // Back up to a position that is certainly a boundary: a printable ASCII
    // character not glued to a Prepend before it.
    size_t a = i;
    for(int steps = 0; a > 0 && steps < 512; ++steps) {
        a = utf8_prev(s, a);
        const unsigned char c = static_cast<unsigned char>(s[a]);
        if(c >= 0x20 && c < 0x7F) {
            if(a == 0) break;
            const size_t pp = utf8_prev(s, a);
            if(prop(utf8_decode(s, pp).cp) != P::Prepend) break;
        }
    }
    size_t cur = a, last = a;
    while(cur < i) {
        last = cur;
        cur = gr_next(s, cur);
    }
    return last;
}

size_t gr_snap(std::string_view s, size_t i) {
    if(i >= s.size()) return s.size();
    const size_t p = gr_prev(s, i + 1);  // cluster containing byte i
    return p;
}

int gr_width(std::string_view cl) {
    if(cl.empty()) return 0;
    const Decoded d0 = utf8_decode(cl, 0);
    int w = gr_cp_width(d0.cp);
    const P p0 = prop(d0.cp);
    bool vs16 = false, vs15 = false, zwj_join = false;
    int ri = p0 == P::RI;
    bool pict0 = is_pict(d0.cp);
    for(size_t i = d0.len; i < cl.size();) {
        const Decoded d = utf8_decode(cl, i);
        if(d.cp == 0xFE0F) vs16 = true;
        else if(d.cp == 0xFE0E) vs15 = true;
        else if(d.cp == 0x200D) zwj_join = true;
        else if(prop(d.cp) == P::RI) ++ri;
        i += d.len;
    }
    if(pict0 && vs16 && !vs15) w = 2;
    else if(pict0 && vs15) w = std::min(w, 1);
    else if(pict0 && zwj_join) w = 2;
    if(ri >= 2) w = 2;
    // keycaps: digit/#/* + VS16 + U+20E3
    if(d0.cp < 0x80 && vs16 && cl.find("\xE2\x83\xA3") != std::string_view::npos)
        w = 2;
    if(w == 0 && p0 != P::Control && p0 != P::ZWJ) w = 1;  // orphan mark
    return w;
}

GrCell gr_cell(std::string_view s, size_t i) {
    GrCell c;
    const size_t e = gr_next(s, i);
    c.len = e - i;
    const Decoded d = utf8_decode(s, i);
    c.base = d.cp;
    for(size_t k = i + d.len; k < e;) {
        const Decoded x = utf8_decode(s, k);
        c.tail += x.cp;
        k += x.len;
    }
    c.width = gr_width(s.substr(i, e - i));
    return c;
}

uint32_t gr_tail_intern(std::u32string_view tail) {
    if(tail.empty()) return 0;
    Tails& t = tails();
    const std::u32string key(tail);
    if(const auto it = t.m.find(key); it != t.m.end()) return it->second;
    if(t.v.size() >= kMaxTails) return 0;
    const uint32_t id = static_cast<uint32_t>(t.v.size());
    t.v.push_back(key);
    t.m.emplace(key, id);
    return id;
}

std::u32string_view gr_tail_get(uint32_t id) {
    const Tails& t = tails();
    return id < t.v.size() ? std::u32string_view(t.v[id]) : std::u32string_view{};
}
