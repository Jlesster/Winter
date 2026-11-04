#include "replace.hpp"

#include <algorithm>
#include <string>
#include <unordered_map>
#include <vector>

#include "utf8.hpp"

namespace {

struct Hunk {
    size_t a, n, b, m;  // old segs [a,a+n) -> new segs [b,b+m)
};

// segments keep their '\n', so "x" and "x\n" differ and end-of-file needs no
// special casing
std::vector<std::string_view> segments(std::string_view s) {
    std::vector<std::string_view> v;
    for(size_t i = 0; i < s.size();) {
        const size_t nl = s.find('\n', i);
        const size_t e = nl == std::string_view::npos ? s.size() : nl + 1;
        v.push_back(s.substr(i, e - i));
        i = e;
    }
    return v;
}

// Myers O(ND). false when the edit distance exceeds max_d.
bool myers(const std::vector<int>& A, const std::vector<int>& B, int max_d,
           std::vector<Hunk>& out, size_t base_a, size_t base_b) {
    const int N = static_cast<int>(A.size()), M = static_cast<int>(B.size());
    const int MAX = std::min(N + M, max_d);
    const auto ix = [MAX](int k) { return static_cast<size_t>(k + MAX + 1); };
    std::vector<int> v(static_cast<size_t>(2 * MAX + 3), 0);
    std::vector<std::vector<int>> trace;
    int dend = -1;
    for(int d = 0; d <= MAX && dend < 0; ++d) {
        trace.push_back(v);
        for(int k = -d; k <= d; k += 2) {
            int x;
            if(k == -d || (k != d && v[ix(k - 1)] < v[ix(k + 1)]))
                x = v[ix(k + 1)];
            else x = v[ix(k - 1)] + 1;
            int y = x - k;
            while(x < N && y < M && A[static_cast<size_t>(x)] ==
                                        B[static_cast<size_t>(y)]) {
                ++x;
                ++y;
            }
            v[ix(k)] = x;
            if(x >= N && y >= M) {
                dend = d;
                break;
            }
        }
    }
    if(dend < 0) return false;

    std::vector<char> del(static_cast<size_t>(N), 0),
        ins(static_cast<size_t>(M), 0);
    int x = N, y = M;
    for(int d = dend; d > 0; --d) {
        const auto& vp = trace[static_cast<size_t>(d)];  // state after d-1
        const int k = x - y;
        const bool up = k == -d || (k != d && vp[ix(k - 1)] < vp[ix(k + 1)]);
        const int pk = up ? k + 1 : k - 1;
        const int px = vp[ix(pk)], py = px - pk;
        if(up) ins[static_cast<size_t>(py)] = 1;
        else del[static_cast<size_t>(px)] = 1;
        x = px;
        y = py;
    }

    size_t i = 0, j = 0;
    const size_t n = static_cast<size_t>(N), m = static_cast<size_t>(M);
    while(i < n || j < m) {
        if(i < n && j < m && !del[i] && !ins[j]) {
            ++i;
            ++j;
            continue;
        }
        const size_t a = i, b = j;
        for(;;) {
            if(i < n && del[i]) ++i;
            else if(j < m && ins[j]) ++j;
            else break;
        }
        out.push_back({base_a + a, i - a, base_b + b, j - b});
    }
    return true;
}

}  // namespace

bool pt_replace(PieceTable& pt, bool& eol, std::string_view text, Origin o,
                Caret* caret) {
    std::string_view nv = text;
    const bool new_eol = text.empty() || text.back() == '\n';
    if(!text.empty() && text.back() == '\n') nv.remove_suffix(1);

    const std::string old = pt.text();
    const auto sa = segments(old), sb = segments(nv);

    std::unordered_map<std::string_view, int> intern;
    const auto id = [&](std::string_view s) {
        return intern.try_emplace(s, static_cast<int>(intern.size()))
            .first->second;
    };
    std::vector<int> A, B;
    A.reserve(sa.size());
    B.reserve(sb.size());
    for(const auto s : sa) A.push_back(id(s));
    for(const auto s : sb) B.push_back(id(s));

    const size_t N = A.size(), M = B.size(), lim = std::min(N, M);
    size_t pre = 0;
    while(pre < lim && A[pre] == B[pre]) ++pre;
    size_t suf = 0;
    while(suf < lim - pre && A[N - 1 - suf] == B[M - 1 - suf]) ++suf;

    std::vector<Hunk> hunks;
    if(pre + suf < N || pre + suf < M) {
        const std::vector<int> ma(A.begin() + static_cast<std::ptrdiff_t>(pre),
                                  A.end() - static_cast<std::ptrdiff_t>(suf));
        const std::vector<int> mb(B.begin() + static_cast<std::ptrdiff_t>(pre),
                                  B.end() - static_cast<std::ptrdiff_t>(suf));
        if(!myers(ma, mb, 1000, hunks, pre, pre)) {
            hunks.clear();  // too different: one big hunk
            hunks.push_back({pre, N - pre - suf, pre, M - pre - suf});
        }
    }

    if(caret) {
        size_t line = caret->line, nl = line;
        std::ptrdiff_t shift = 0;
        bool inside = false;
        for(const Hunk& h : hunks) {
            if(line < h.a) break;
            if(line < h.a + h.n) {
                nl = h.b + (h.m ? std::min(line - h.a, h.m - 1) : 0);
                inside = true;
                break;
            }
            shift += static_cast<std::ptrdiff_t>(h.m) -
                     static_cast<std::ptrdiff_t>(h.n);
        }
        if(!inside) nl = static_cast<size_t>(
                       static_cast<std::ptrdiff_t>(line) + shift);
        caret->line = nl;  // clamped to the new text below
    }

    if(!hunks.empty()) {
        std::vector<size_t> off(N + 1, 0);
        for(size_t i = 0; i < N; ++i) off[i + 1] = off[i] + sa[i].size();
        const Origin prev = pt.origin;
        pt.origin = o;
        {
            PieceTable::Group g(pt);
            for(auto it = hunks.rbegin(); it != hunks.rend(); ++it) {
                const size_t s = off[it->a], e = off[it->a + it->n];
                std::string rep;
                for(size_t j = it->b; j < it->b + it->m; ++j) rep += sb[j];
                if(e > s) pt.remove(s, e - s);
                if(!rep.empty()) pt.insert(s, rep);
            }
        }
        pt.origin = prev;
    }
    eol = new_eol;

    if(caret) {
        caret->line = std::min(caret->line, pt.line_count() - 1);
        const std::string t = pt.line_text(caret->line);
        size_t c = std::min(caret->col, t.size());
        while(c > 0 && c < t.size() && utf8_is_cont(t[c])) --c;
        caret->col = c;
    }
    return !hunks.empty();
}
