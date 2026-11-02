#include "fuzzy.hpp"

#include <algorithm>
#include <cctype>
#include <climits>
#include <cstddef>

namespace fuzzy {
namespace {

bool has_upper(std::string_view s) {
    return std::ranges::any_of(
        s, [](unsigned char c) { return std::isupper(c) != 0; });
}

bool screaming(std::string_view s) {
    return s.size() >= 2 && has_upper(s) &&
           !std::ranges::any_of(
               s, [](unsigned char c) { return std::islower(c) != 0; });
}

bool word_start(std::string_view f, size_t i) {
    if(i == 0) return true;
    const unsigned char p = static_cast<unsigned char>(f[i - 1]);
    const unsigned char c = static_cast<unsigned char>(f[i]);
    return !std::isalnum(p) || (std::islower(p) && std::isupper(c));
}

bool ieq(char a, char b) {
    return std::tolower(static_cast<unsigned char>(a)) ==
           std::tolower(static_cast<unsigned char>(b));
}

constexpr int kNeg = INT_MIN / 2;
constexpr int kGapLead = -5, kGapTrail = -5, kGapInner = -10;
constexpr int kConsecutive = 1000;
constexpr int kBonusSlash = 900, kBonusWord = 800, kBonusCamel = 700;
constexpr size_t kMaxText = 512, kMaxQuery = 64;

int bonus_at(std::string_view t, size_t i) {
    if(i == 0) return kBonusWord;
    const unsigned char p = static_cast<unsigned char>(t[i - 1]);
    const unsigned char c = static_cast<unsigned char>(t[i]);
    if(p == '/') return kBonusSlash;
    if(!std::isalnum(p)) return kBonusWord;
    if(std::islower(p) && std::isupper(c)) return kBonusCamel;
    return 0;
}

}  // namespace

// Moved unchanged from complete.cpp.
int rank(std::string_view f, std::string_view q) {
    if(q.empty()) return 0;
    if(f.size() >= q.size() && std::equal(q.begin(), q.end(), f.begin(), ieq)) {
        if(has_upper(q) && !f.starts_with(q)) return 1;
        if(!has_upper(q) && screaming(f)) return 1;
        return 0;
    }
    for(size_t i = 0; i < f.size(); ++i) {
        if(!ieq(f[i], q[0]) || !word_start(f, i)) continue;
        size_t qi = 1;
        for(size_t j = i + 1; j < f.size() && qi < q.size(); ++j)
            if(ieq(f[j], q[qi])) ++qi;
        if(qi == q.size()) return 2;
    }
    return -1;
}

bool match(std::string_view t, std::string_view q, Match& out) {
    out.score = 0;
    out.pos.clear();
    if(q.empty()) return true;
    const size_t n = q.size(), m = t.size();
    if(n > m) return false;

    // cheap reject, and the greedy fallback for oversized inputs
    std::vector<size_t> greedy;
    for(size_t j = 0, i = 0; j < m && i < n; ++j)
        if(ieq(t[j], q[i])) {
            greedy.push_back(j);
            ++i;
        }
    if(greedy.size() != n) return false;
    if(m > kMaxText || n > kMaxQuery) {
        out.pos = std::move(greedy);
        return true;
    }

    // D[i][j]: best score with q[i] matched exactly at t[j].
    // M[i][j]: best score for q[0..i] inside t[0..j].
    std::vector<int> D(n * m, kNeg), M(n * m, kNeg);
    const auto at = [m](size_t i, size_t j) { return i * m + j; };
    for(size_t i = 0; i < n; ++i) {
        const int gap = i + 1 == n ? kGapTrail : kGapInner;
        for(size_t j = 0; j < m; ++j) {
            int d = kNeg;
            if(ieq(q[i], t[j])) {
                const int b = bonus_at(t, j);
                if(i == 0) {
                    d = static_cast<int>(j) * kGapLead + b;
                } else if(j > 0) {
                    const int prev_m = M[at(i - 1, j - 1)];
                    const int prev_d = D[at(i - 1, j - 1)];
                    if(prev_m > kNeg) d = prev_m + b;
                    if(prev_d > kNeg) d = std::max(d, prev_d + kConsecutive);
                }
            }
            D[at(i, j)] = d;
            const int left = j > 0 ? M[at(i, j - 1)] : kNeg;
            M[at(i, j)] = std::max(d, left > kNeg ? left + gap : kNeg);
        }
    }

    out.score = M[at(n - 1, m - 1)];
    out.pos.assign(n, 0);
    bool required = false;
    size_t j = m;
    for(size_t i = n; i-- > 0;) {
        while(j-- > 0) {
            const int d = D[at(i, j)];
            if(d > kNeg && (required || d == M[at(i, j)])) {
                required = i > 0 && j > 0 &&
                           M[at(i, j)] == D[at(i - 1, j - 1)] + kConsecutive;
                out.pos[i] = j;
                break;
            }
        }
    }
    return true;
}

}  // namespace fuzzy
