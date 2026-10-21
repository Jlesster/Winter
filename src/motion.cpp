#include "motion.hpp"

#include <algorithm>
#include <cctype>

#include "utf8.hpp"

namespace motion {
namespace {

enum Cls { Space, Word, Punct };

// '\n' is whitespace on purpose: word motions walk straight across lines
int cls(char c, bool big) {
    const auto u = static_cast<unsigned char>(c);
    if(u == ' ' || u == '\t' || u == '\n' || u == '\r') return Space;
    if(big) return Word;  // WORD: anything that isn't blank
    if(u >= 0x80 || std::isalnum(u) || u == '_') return Word;
    return Punct;
}

// one codepoint forward / back (never splits a UTF-8 sequence)
size_t fwd(const PieceTable& t, size_t p) {
    const size_t n = t.size();
    if(p >= n) return n;
    ++p;
    while(p < n && utf8_is_cont(t.at(p))) ++p;
    return p;
}

size_t back(const PieceTable& t, size_t p) {
    if(p == 0) return 0;
    --p;
    while(p > 0 && utf8_is_cont(t.at(p))) --p;
    return p;
}

// p sits on an empty line (vim counts those as a word)
bool empty_line_at(const PieceTable& t, size_t p) {
    return (p == t.size() || t.at(p) == '\n') && (p == 0 || t.at(p - 1) == '\n');
}

size_t last_pos(const PieceTable& t) {
    const size_t l = t.line_count() - 1;
    const size_t s = t.line_start(l), e = t.line_end(l);
    return e == s ? s : back(t, e);
}

size_t first_nonblank_of(const PieceTable& t, size_t line) {
    const size_t s = t.line_start(line), e = t.line_end(line);
    size_t p = s;
    while(p < e && (t.at(p) == ' ' || t.at(p) == '\t')) ++p;
    if(p >= e && e > s) p = back(t, e);  // all blank: last char, like vim
    return p;
}

size_t last_char_of(const PieceTable& t, size_t line) {
    const size_t s = t.line_start(line), e = t.line_end(line);
    return e > s ? back(t, e) : s;
}

Target fail(size_t p) { return {p, MKind::Exclusive, false, false}; }

Target fwd_word(const PieceTable& t, size_t p, int count, bool big) {
    const size_t end = t.size(), lastp = last_pos(t), start = p;
    for(int n = std::max(count, 1); n > 0; --n) {
        if(p >= lastp) break;  // already on the very last char
        const int c0 = cls(t.at(p), big);
        p = fwd(t, p);
        // finish the word we were in...
        if(c0 != Space)
            while(p < end && cls(t.at(p), big) == c0) p = fwd(t, p);
        // ...then skip blanks, but an empty line is a word of its own
        while(p < end && cls(t.at(p), big) == Space && !empty_line_at(t, p))
            p = fwd(t, p);
    }
    p = clamp_normal(t, p);
    return {p, MKind::Exclusive, p != start, false};
}

Target end_of_word(const PieceTable& t, size_t p, int count, bool big) {
    const size_t end = t.size(), lastp = last_pos(t), start = p;
    for(int n = std::max(count, 1); n > 0; --n) {
        if(p >= lastp) break;
        const int c0 = cls(t.at(p), big);
        p = fwd(t, p);
        if(c0 != Space && cls(t.at(p), big) == c0) {
            // mid-word: run to the end of it
            while(p < end && cls(t.at(p), big) == c0) p = fwd(t, p);
        } else {
            // at a word end already: skip blanks, run to the end of the next
            while(p < end && cls(t.at(p), big) == Space) p = fwd(t, p);
            if(p >= end) {
                p = lastp;
                break;
            }
            const int c1 = cls(t.at(p), big);
            while(p < end && cls(t.at(p), big) == c1) p = fwd(t, p);
        }
        p = back(t, p);  // we overshot by one
    }
    p = clamp_normal(t, p);
    return {p, MKind::Inclusive, p != start, false};
}

Target back_word(const PieceTable& t, size_t p, int count, bool big) {
    const size_t start = p;
    for(int n = std::max(count, 1); n > 0; --n) {
        if(p == 0) break;
        p = back(t, p);
        bool stop = false;
        while(cls(t.at(p), big) == Space) {
            if(p == 0 || empty_line_at(t, p)) {
                stop = true;  // start of file / empty line is its own word
                break;
            }
            p = back(t, p);
        }
        if(stop) continue;
        const int c = cls(t.at(p), big);
        while(p > 0) {
            const size_t q = back(t, p);
            if(cls(t.at(q), big) != c) break;
            p = q;
        }
    }
    return {p, MKind::Exclusive, p != start, false};
}

bool line_empty(const PieceTable& t, size_t l) {
    return t.line_start(l) == t.line_end(l);
}

Target paragraph(const PieceTable& t, size_t p, int count, int dir) {
    const long last = static_cast<long>(t.line_count()) - 1;
    long cur = static_cast<long>(t.line_of(p));
    for(int n = std::max(count, 1); n > 0;) {
        --n;
        bool seen_text = false;
        for(bool first = true;; first = false) {
            const bool empty = line_empty(t, static_cast<size_t>(cur));
            if(!empty) seen_text = true;
            if(!first && seen_text && empty) break;
            const long nx = cur + dir;
            if(nx < 0 || nx > last) {
                if(n > 0) return fail(p);  // ran out with counts to spare
                break;
            }
            cur = nx;
        }
    }
    const auto line = static_cast<size_t>(cur);
    const size_t s = t.line_start(line), e = t.line_end(line);
    if(cur == last && dir > 0 && e != s)  // `}` into the last line: land on
        return {back(t, e), MKind::Inclusive, true, false};  // its last char
    return {s, MKind::Exclusive, true, false};
}

}  // namespace

size_t clamp_normal(const PieceTable& t, size_t p) {
    p = std::min(p, t.size());
    const size_t l = t.line_of(p);
    const size_t s = t.line_start(l), e = t.line_end(l);
    if(p >= e && e > s) return back(t, e);
    return p;
}

Target left(const PieceTable& t, size_t p, int count) {
    const size_t s = t.line_start(t.line_of(p));
    size_t q = p;
    for(int n = std::max(count, 1); n > 0 && q > s; --n) q = back(t, q);
    return {q, MKind::Exclusive, q != p, false};
}

Target right(const PieceTable& t, size_t p, int count) {
    const size_t e = t.line_end(t.line_of(p));
    size_t q = p;
    for(int n = std::max(count, 1); n > 0; --n) {
        const size_t nx = fwd(t, q);
        if(nx >= e) break;  // q is the last char: can't go further
        q = nx;
    }
    return {q, MKind::Exclusive, q != p, false};
}

Target to_line_start(const PieceTable& t, size_t p, int) {
    return {t.line_start(t.line_of(p)), MKind::Exclusive, true, false};
}

Target to_first_nonblank(const PieceTable& t, size_t p, int) {
    return {first_nonblank_of(t, t.line_of(p)), MKind::Exclusive, true, false};
}

Target to_line_end(const PieceTable& t, size_t p, int count) {
    const size_t last = t.line_count() - 1;
    size_t line = t.line_of(p);
    if(count > 1) {  // 3$ = end of the line two below
        if(line >= last) return fail(p);
        line = std::min(line + static_cast<size_t>(count - 1), last);
    }
    return {last_char_of(t, line), MKind::Inclusive, true, true};
}

Target next_word(const PieceTable& t, size_t p, int n) { return fwd_word(t, p, n, false); }
Target next_WORD(const PieceTable& t, size_t p, int n) { return fwd_word(t, p, n, true); }
Target prev_word(const PieceTable& t, size_t p, int n) { return back_word(t, p, n, false); }
Target prev_WORD(const PieceTable& t, size_t p, int n) { return back_word(t, p, n, true); }
Target end_word(const PieceTable& t, size_t p, int n) { return end_of_word(t, p, n, false); }
Target end_WORD(const PieceTable& t, size_t p, int n) { return end_of_word(t, p, n, true); }

Target to_file_start(const PieceTable& t, size_t, int count) {
    const size_t last = t.line_count() - 1;
    const size_t line = count > 0 ? std::min(static_cast<size_t>(count - 1), last) : 0;
    return {first_nonblank_of(t, line), MKind::Linewise, true, false};
}

Target to_file_end(const PieceTable& t, size_t, int count) {
    const size_t last = t.line_count() - 1;
    const size_t line = count > 0 ? std::min(static_cast<size_t>(count - 1), last) : last;
    return {first_nonblank_of(t, line), MKind::Linewise, true, false};
}

Target para_back(const PieceTable& t, size_t p, int n) { return paragraph(t, p, n, -1); }
Target para_fwd(const PieceTable& t, size_t p, int n) { return paragraph(t, p, n, +1); }

Target match_pair(const PieceTable& t, size_t p, int) {
    constexpr std::string_view kPairs = "()[]{}";
    const size_t e = t.line_end(t.line_of(p));
    size_t q = p;
    while(q < e && kPairs.find(t.at(q)) == std::string_view::npos) ++q;
    if(q >= e) return fail(p);  // no bracket from the cursor to end of line

    const char c = t.at(q);
    const size_t idx = kPairs.find(c);
    const bool opening = idx % 2 == 0;
    const char other = kPairs[opening ? idx + 1 : idx - 1];

    int depth = 0;
    for(size_t i = q;;) {
        const char x = t.at(i);
        if(x == c) ++depth;
        else if(x == other && --depth == 0)
            return {i, MKind::Inclusive, true, false};
        if(opening) {
            if(++i >= t.size()) break;
        } else {
            if(i == 0) break;
            --i;
        }
    }
    return fail(p);
}

Target find_char(const PieceTable& t, size_t p, int count, Find kind,
                 std::string_view needle, bool repeat) {
    if(needle.empty()) return fail(p);
    const size_t line = t.line_of(p);
    const size_t s = t.line_start(line), e = t.line_end(line);
    const bool forward = kind == Find::f || kind == Find::t;
    const bool till = kind == Find::t || kind == Find::T;

    auto matches = [&](size_t r) {
        for(size_t k = 0; k < needle.size(); ++k)
            if(t.at(r + k) != needle[k]) return false;
        return true;
    };

    size_t from = p;
    // `;` after `t` would otherwise be stuck on the char it already stopped
    // in front of, so it hops over an adjacent match first (vim does too)
    if(repeat && till && count <= 1) {
        if(forward) {
            const size_t nx = fwd(t, from);
            if(nx < e && matches(nx)) from = nx;
        } else if(from > s) {
            const size_t pv = back(t, from);
            if(matches(pv)) from = pv;
        }
    }

    for(int n = std::max(count, 1); n > 0; --n) {
        bool found = false;
        if(forward) {
            for(size_t r = fwd(t, from); r < e; r = fwd(t, r))
                if(matches(r)) { from = r; found = true; break; }
        } else if(from > s) {
            for(size_t r = back(t, from);; r = back(t, r)) {
                if(matches(r)) { from = r; found = true; break; }
                if(r <= s) break;
            }
        }
        if(!found) return fail(p);  // any miss cancels the whole motion
    }

    if(till) from = forward ? back(t, from) : fwd(t, from);
    return {from, forward ? MKind::Inclusive : MKind::Exclusive, true, false};
}

}  // namespace motion
