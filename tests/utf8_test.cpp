#include "../src/utf8.hpp"
#include <cassert>
#include <cstdio>
#include <random>
#include <vector>

int main() {
    std::mt19937 rng(1);
    for(char32_t c = 0; c <= 0x10FFFF; c++) {
        if(c >= 0xD800 && c <= 0xDFFF) continue;
        std::string s;
        append_utf8(s, c);
        auto d = utf8_decode(s, 0);
        assert(d.cp == c && d.len == s.size());
        assert(utf8_next(s, 0) == s.size() && utf8_prev(s, s.size()) == 0);
    }
    assert(utf8_decode("\xC0\x80", 0).cp == 0xFFFD);
    assert(utf8_decode("\xED\xA0\x80", 0).cp == 0xFFFD);
    assert(utf8_decode("\xF4\x90\x80\x80", 0).cp == 0xFFFD);
    assert(utf8_decode("\xE2\x82", 0).len == 1);
    for(int it = 0; it < 300000; it++) {
        std::string s;
        int n = rng() % 12;
        for(int k = 0; k < n; k++) {
            if(rng() % 2) s.push_back(static_cast<char>(rng() % 256));
            else {
                static const char32_t pool[] = {
                    'a', 0xE9, 0x20AC, 0x1F600, 0x4E2D, 0x301};
                append_utf8(s, pool[rng() % 6]);
            }
        }
        std::vector<size_t> f{0};
        for(size_t i = 0; i < s.size();) {
            size_t j = utf8_next(s, i);
            assert(j > i && j <= s.size());
            f.push_back(j);
            i = j;
        }
        std::vector<size_t> b{s.size()};
        for(size_t i = s.size(); i > 0;) {
            size_t j = utf8_prev(s, i);
            assert(j < i);
            b.push_back(j);
            i = j;
        }
        assert(f == std::vector<size_t>(b.rbegin(), b.rend()));
    }
    assert(utf8_width('a') == 1 && utf8_width(0x4E2D) == 2 &&
           utf8_width(0x301) == 0 && utf8_width(0x1F600) == 2);
    std::puts("ALL PASS");
}
