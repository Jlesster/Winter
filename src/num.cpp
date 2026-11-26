#include "num.hpp"

#include <algorithm>
#include <cctype>
#include <bit>
#include <charconv>
#include <format>
#include <climits>
#include <utility>
#include <vector>

namespace num {
namespace {

using i128 = __int128;
using u128 = unsigned __int128;

bool dig(char c) { return c >= '0' && c <= '9'; }
bool hexd(char c) {
    return dig(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}
bool word(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_' ||
           static_cast<unsigned char>(c) >= 0x80;
}
bool alpha_(char c) { return word(c) && !dig(c); }
// a '-' / '+' right after one of these is an operator, not a sign
bool sign_block(char c) { return word(c) || c == ')' || c == ']'; }

enum class K { Dec, Hex, Bin, OctC, Oct0o, Real, Ver };

struct Tok {
    K k = K::Dec;
    size_t start = 0, end = 0;  // whole token, sign included
    char sign = 0;              // '-' / '+' / 0
    size_t ds = 0, de = 0;      // digit run (Real: integer part)
    size_t fs = 0, fe = 0;      // Real: fraction digits (empty: fs == fe == de)
    size_t es = 0, ee = 0;      // Real: exponent incl. sign; es == ee: none
    std::vector<std::pair<size_t, size_t>> comps;  // Ver: components
};

Tok lex(std::string_view s, size_t i, unsigned f) {
    const size_t n = s.size();
    Tok t;
    t.start = i;
    const auto sign = [&] {
        if(i > 0 && (s[i - 1] == '-' || s[i - 1] == '+') &&
           (i < 2 || !sign_block(s[i - 2]))) {
            t.sign = s[i - 1];
            t.start = i - 1;
        }
    };
    const auto run = [&](size_t j, auto pred) {
        while(j < n && pred(s[j])) ++j;
        return j;
    };

    if(s[i] == '0' && i + 2 < n) {
        const char c = s[i + 1];
        if((f & kHex) && (c == 'x' || c == 'X') && hexd(s[i + 2])) {
            t.k = K::Hex;
            t.ds = i + 2;
            t.de = t.end = run(i + 2, hexd);
            return t;
        }
        if((f & kBin) && (c == 'b' || c == 'B') &&
           (s[i + 2] == '0' || s[i + 2] == '1')) {
            t.k = K::Bin;
            t.ds = i + 2;
            t.de = t.end =
                run(i + 2, [](char x) { return x == '0' || x == '1'; });
            return t;
        }
        if((f & kOctal) && (c == 'o' || c == 'O') && s[i + 2] >= '0' &&
           s[i + 2] <= '7') {
            t.k = K::Oct0o;
            t.ds = i + 2;
            t.de = t.end =
                run(i + 2, [](char x) { return x >= '0' && x <= '7'; });
            return t;
        }
    }

    const size_t j = run(i, dig);
    t.ds = i;
    t.de = j;

    if(f & kVersion) {  // N.N.N, at least three components
        std::vector<std::pair<size_t, size_t>> comps{{i, j}};
        size_t k = j;
        while(k + 1 < n && s[k] == '.' && dig(s[k + 1])) {
            const size_t m = run(k + 1, dig);
            comps.emplace_back(k + 1, m);
            k = m;
        }
        if(comps.size() >= 3) {
            t.k = K::Ver;
            t.end = k;
            t.comps = std::move(comps);
            return t;
        }
    }

    if(f & kReal) {  // N.N, N.NeX, NeX
        size_t m = j;
        bool have = false;
        if(j + 1 < n && s[j] == '.' && dig(s[j + 1])) {
            t.fs = j + 1;
            m = run(j + 1, dig);
            t.fe = m;
            have = true;
        } else t.fs = t.fe = j;
        size_t ee = m;
        if(m < n && (s[m] == 'e' || s[m] == 'E')) {
            size_t q = m + 1;
            if(q < n && (s[q] == '+' || s[q] == '-')) ++q;
            if(q < n && dig(s[q])) {
                ee = run(q, dig);
                t.es = m + 1;
                t.ee = ee;
                have = true;
            }
        }
        if(have) {
            t.k = K::Real;
            t.end = ee;
            sign();
            return t;
        }
    }

    if((f & kOctal) && s[i] == '0' && j - i >= 2 &&
       std::all_of(
           s.begin() + i, s.begin() + j, [](char c) { return c <= '7'; })) {
        t.k = K::OctC;
        t.end = j;
        return t;
    }

    t.k = K::Dec;
    t.end = j;
    sign();
    return t;
}

std::optional<Tok> find(std::string_view s, size_t col, const Opts& o) {
    for(size_t i = 0; i < s.size();) {
        if(!dig(s[i])) {
            ++i;
            continue;
        }
        const Tok t = lex(s, i, o.formats);
        bool blocked = false;
        if(!(o.formats & kIdent) && i > 0 && alpha_(s[i - 1])) {
            // v1.2.3 is still a version
            const bool v = t.k == K::Ver &&
                           (s[i - 1] == 'v' || s[i - 1] == 'V') &&
                           (i < 2 || !word(s[i - 2]));
            blocked = !v;
        }
        if(!blocked) {
            if(t.start >= o.max_start) break;
            if(t.end > col) return t;
        }
        i = t.end;
    }
    return std::nullopt;
}

std::optional<uint64_t> parse_u(std::string_view d, int base) {
    uint64_t v = 0;
    const auto r = std::from_chars(d.data(), d.data() + d.size(), v, base);
    if(r.ec != std::errc{} || r.ptr != d.data() + d.size()) return std::nullopt;
    return v;
}

std::string radix_str(uint64_t v, int base, bool upper) {
    if(!v) return "0";
    std::string r;
    const char* digs = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    for(; v; v /= static_cast<unsigned>(base))
        r.insert(r.begin(), digs[v % static_cast<unsigned>(base)]);
    return r;
}

std::string u128_str(u128 v) {
    if(!v) return "0";
    std::string r;
    for(; v; v /= 10) r.insert(r.begin(), static_cast<char>('0' + v % 10));
    return r;
}

void pad_to(std::string& s, size_t w) {
    if(s.size() < w) s.insert(0, w - s.size(), '0');
}

bool had_lz(std::string_view d) { return d.size() > 1 && d[0] == '0'; }

// signed decimal with sign handling and leading-zero width (-1 + 1 = 0)
std::optional<std::string> bump_dec(std::string_view digits, char sign,
                                    int64_t delta) {
    const auto m = parse_u(digits, 10);
    if(!m) return std::nullopt;
    i128 v = sign == '-' ? -static_cast<i128>(*m) : static_cast<i128>(*m);
    v += delta;
    v = std::clamp<i128>(v, INT64_MIN, INT64_MAX);
    const bool neg = v < 0;
    std::string s =
        u128_str(neg ? static_cast<u128>(-v) : static_cast<u128>(v));
    if(had_lz(digits)) pad_to(s, digits.size());
    return (neg ? "-" : sign == '+' ? "+" : "") + s;
}

Edit mk(size_t start, size_t len, std::string text) {
    Edit e;
    e.start = start;
    e.len = len;
    e.cursor = start + (text.empty() ? 0 : text.size() - 1);
    e.text = std::move(text);
    return e;
}

}  // namespace

std::optional<Edit> bump(std::string_view line, size_t col, int64_t delta,
                         const Opts& o) {
    const auto found = find(line, col, o);
    if(!found) return std::nullopt;
    const Tok& t = *found;
    const auto sub = [&](size_t a, size_t b) { return line.substr(a, b - a); };

    switch(t.k) {
        case K::Dec: {
            auto s = bump_dec(sub(t.ds, t.de), t.sign, delta);
            if(!s) return std::nullopt;
            return mk(t.start, t.de - t.start, std::move(*s));
        }
        case K::Hex:
        case K::Bin:
        case K::Oct0o: {
            const int base = t.k == K::Hex ? 16 : t.k == K::Bin ? 2 : 8;
            const std::string_view d = sub(t.ds, t.de);
            const auto v = parse_u(d, base);
            if(!v) return std::nullopt;
            const uint64_t nv = *v + static_cast<uint64_t>(delta);  // wraps
            bool upper = line[t.ds - 1] == 'X';  // 0X10 -> 0X1A
            if(base == 16)
                for(size_t i = d.size(); i-- > 0;)
                    if(std::isalpha(static_cast<unsigned char>(d[i]))) {
                        upper = std::isupper(static_cast<unsigned char>(d[i]));
                        break;
                    }
            std::string s = radix_str(nv, base, upper);
            if(had_lz(d)) pad_to(s, d.size());
            return mk(t.ds, d.size(), std::move(s));
        }
        case K::OctC: {  // 0755: the leading 0 is the marker, not a digit
            const std::string_view d = sub(t.ds, t.de);
            const auto v = parse_u(d.substr(1), 8);
            if(!v) return std::nullopt;
            std::string s =
                radix_str(*v + static_cast<uint64_t>(delta), 8, false);
            pad_to(s, d.size() - 1);  // width kept: 0100 - 1 -> 0077
            return mk(t.ds, d.size(), "0" + s);
        }
        case K::Ver: {
            size_t idx = t.comps.size() - 1;  // whole: the last component
            if(o.int_only) {
                idx = 0;
                while(idx + 1 < t.comps.size() && t.comps[idx].second <= col)
                    ++idx;
            }
            const auto [a, b] = t.comps[idx];
            const std::string_view d = sub(a, b);
            const auto v = parse_u(d, 10);
            if(!v) return std::nullopt;
            const i128 nv = std::clamp<i128>(static_cast<i128>(*v) + delta,
                                             0,
                                             static_cast<i128>(UINT64_MAX));
            std::string s = u128_str(static_cast<u128>(nv));
            if(had_lz(d)) pad_to(s, d.size());
            return mk(a, b - a, std::move(s));
        }
        case K::Real: {
            const bool has_exp = t.ee > t.es;
            const size_t ep = t.es - 1;               // the 'e'
            if(o.int_only && has_exp && col >= ep) {  // exponent digit run
                size_t q = t.es;
                char sg = 0;
                if(line[q] == '+' || line[q] == '-') sg = line[q++];
                auto s = bump_dec(sub(q, t.ee), sg, delta);
                if(!s) return std::nullopt;
                return mk(t.es, t.ee - t.es, std::move(*s));
            }
            const size_t k = t.fe - t.fs;
            const std::string_view I = sub(t.ds, t.de);
            if(I.size() + k > 18) return std::nullopt;
            const auto v =
                parse_u(std::string(I) + std::string(sub(t.fs, t.fe)), 10);
            if(!v) return std::nullopt;
            u128 unit = 1;
            const bool on_frac =
                o.int_only && k > 0 && col >= t.de;  // '.' counts: next run
            if(!on_frac)
                for(size_t i = 0; i < k; ++i) unit *= 10;  // 1.0
            i128 val =
                t.sign == '-' ? -static_cast<i128>(*v) : static_cast<i128>(*v);
            val += static_cast<i128>(delta) * static_cast<i128>(unit);
            const bool neg = val < 0;
            std::string s = u128_str(neg ? static_cast<u128>(-val)
                                         : static_cast<u128>(val));
            pad_to(s, k + 1);
            std::string ip = s.substr(0, s.size() - k);
            if(had_lz(I)) pad_to(ip, I.size());
            std::string out = (neg ? "-" : t.sign == '+' ? "+" : "") + ip;
            if(k) out += "." + s.substr(s.size() - k);
            const size_t stop = k ? t.fe : t.de;
            return mk(t.start, stop - t.start, std::move(out));
        }
    }
    return std::nullopt;
}

// ---- numformats string -----------------------------------------------------

namespace {
struct Name {
    std::string_view n;
    unsigned b;
};
constexpr Name kNames[] = {{"bin", kBin},
                           {"hex", kHex},
                           {"octal", kOctal},
                           {"real", kReal},
                           {"version", kVersion},
                           {"ident", kIdent},
                           {"all", kAll},
                           {"none", 0}};
}  // namespace

std::optional<unsigned> parse_formats(std::string_view spec, std::string* err) {
    const auto fail = [&](std::string m) -> std::optional<unsigned> {
        if(err) *err = std::move(m);
        return std::nullopt;
    };
    unsigned abs_bits = 0, rel_bits = kDefault;
    bool rel = false, abs_ = false;
    size_t i = 0;
    while(i <= spec.size()) {
        size_t j = spec.find_first_of(", \t", i);
        if(j == std::string_view::npos) j = spec.size();
        std::string_view tok = spec.substr(i, j - i);
        i = j + 1;
        if(tok.empty()) continue;
        const char c = tok[0];
        const bool is_rel = c == '+' || c == '-';
        const std::string_view name = is_rel ? tok.substr(1) : tok;
        const Name* hit = nullptr;
        for(const Name& n : kNames)
            if(n.n == name) hit = &n;
        if(!hit) return fail("unknown numformat '" + std::string(name) + "'");
        if(is_rel) {
            rel = true;
            if(c == '+') rel_bits |= hit->b;
            else rel_bits &= ~hit->b;
        } else {
            abs_ = true;
            abs_bits |= hit->b;
        }
    }
    if(rel && abs_) return fail("can't mix +/- entries with plain ones");
    return rel ? rel_bits : abs_bits;
}

std::string format_formats(unsigned f) {
    std::string r;
    for(const Name& n : kNames) {
        if(n.n == "all" || n.n == "none" || !(f & n.b)) continue;
        if(!r.empty()) r += ',';
        r += n.n;
    }
    return r.empty() ? "none" : r;
}

namespace {

int nat_width(size_t digits, int bits_per_digit) {
    const size_t bits = digits * static_cast<size_t>(bits_per_digit);
    return bits <= 8 ? 8 : bits <= 16 ? 16 : bits <= 32 ? 32 : 64;
}

}  // namespace

std::vector<Row> describe(std::string_view line, size_t col, unsigned formats) {
    // always lex the integer formats; only kIdent follows the user option
    const Opts o{.formats = (formats & kIdent) | kBin | kHex | kOctal};
    const auto t = find(line, col, o);
    if(!t || t->start > col) return {};  // under the cursor only

    const std::string_view d = line.substr(t->ds, t->de - t->ds);
    int base = 0, bpd = 0;
    bool bare_bin = false;  // 00000111: binary reading of a C octal token
    switch(t->k) {
        case K::Hex: base = 16, bpd = 4; break;
        case K::Bin: base = 2, bpd = 1; break;
        case K::Oct0o: base = 8, bpd = 3; break;
        case K::OctC:
            bare_bin = d.size() >= 4 &&
                       std::all_of(d.begin(), d.end(),
                                   [](char c) { return c == '0' || c == '1'; });
            base = bare_bin ? 2 : 8;
            bpd = bare_bin ? 1 : 3;
            break;
        default: return {};  // decimal, real, version
    }
    const auto v = parse_u(d, base);
    if(!v) return {};  // doesn't fit 64 bits

    std::vector<Row> out;
    const auto row = [&](std::string k, std::string val) {
        out.push_back({std::move(k), std::move(val)});
    };
    row("dec", u128_str(*v));
    row("hex", "0x" + radix_str(*v, 16, true));
    row("bin", "0b" + radix_str(*v, 2, false));
    if(!bare_bin) row("oct", "0" + radix_str(*v, 8, false));

    if(base == 2 || base == 16) {
        const int w = nat_width(d.size(), bpd);
        if(*v & (uint64_t{1} << (w - 1))) {
            const int64_t sv =
                w == 64 ? static_cast<int64_t>(*v)
                        : static_cast<int64_t>(*v) - (int64_t{1} << w);
            row(std::format("i{}", w), std::to_string(sv));
        }
        // a float only when the digits fill the width exactly
        if(d.size() * static_cast<size_t>(bpd) == static_cast<size_t>(w)) {
            if(w == 32)
                row("f32", std::format("{}", std::bit_cast<float>(
                                                 static_cast<uint32_t>(*v))));
            if(w == 64)
                row("f64", std::format("{}", std::bit_cast<double>(*v)));
        }
    }
    if(bare_bin) {  // C reads this as octal; say so rather than guess
        if(const auto oct = parse_u(d, 8)) row("as C octal", u128_str(*oct));
    }
    return out;
}

}  // namespace num
