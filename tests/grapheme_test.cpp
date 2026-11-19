#include "../src/grapheme.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>

static int fails = 0;
#define CHECK(c)                                             \
    do {                                                     \
        if(!(c)) {                                           \
            std::printf("FAIL line %d: %s\n", __LINE__, #c); \
            ++fails;                                         \
        }                                                    \
    } while(0)

static std::vector<std::string> clusters(std::string_view s) {
    std::vector<std::string> out;
    for(size_t i = 0; i < s.size();) {
        size_t e = gr_next(s, i);
        out.emplace_back(s.substr(i, e - i));
        i = e;
    }
    return out;
}
static void expect(const char* name, std::string s, size_t n, int w0 = -1) {
    auto c = clusters(s);
    if(c.size() != n) {
        std::printf("FAIL %s: %zu clusters, want %zu\n", name, c.size(), n);
        ++fails;
        return;
    }
    if(w0 >= 0 && gr_width(c[0]) != w0) {
        std::printf("FAIL %s: width %d want %d\n", name, gr_width(c[0]), w0);
        ++fails;
    }
}

int main() {
    expect("e+acute", "e\xCC\x81", 1, 1);
    expect("ascii", "abc", 3, 1);
    expect("crlf", "\r\n", 1);
    expect("flags2",
           "\xF0\x9F\x87\xAF\xF0\x9F\x87\xB5\xF0\x9F\x87\xBA\xF0\x9F\x87\xB8",
           2,
           2);
    expect("flags3", "\xF0\x9F\x87\xAF\xF0\x9F\x87\xB5\xF0\x9F\x87\xBA", 2);
    expect("family",
           "\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F"
           "\x91\xA7\xE2\x80\x8D\xF0\x9F\x91\xA6",
           1,
           2);
    expect("thumb+skin", "\xF0\x9F\x91\x8D\xF0\x9F\x8F\xBD", 1, 2);
    expect("heart_vs16", "\xE2\x9D\xA4\xEF\xB8\x8F", 1, 2);
    expect("heart_text", "\xE2\x9D\xA4", 1, 1);
    expect("check_text", "\xE2\x9C\x93", 1, 1);   // U+2713, narrow
    expect("ballot_box", "\xE2\x98\x91", 1, 1);   // U+2611, narrow
    expect("white_check", "\xE2\x9C\x85", 1, 2);  // U+2705, wide
    expect("hangul_syl", "\xED\x95\x9C", 1, 2);
    expect("hangul_jamo", "\xE1\x84\x92\xE1\x85\xA1\xE1\x86\xAB", 1, 2);
    expect("keycap", "1\xEF\xB8\x8F\xE2\x83\xA3", 1, 2);
    expect("zwj_not_pict",
           "a\xE2\x80\x8D"
           "b",
           2);
    expect("prepend",
           "\xD8\x80"
           "1",
           1);
    expect("cjk", "\xE6\x97\xA5\xE6\x9C\xAC", 2, 2);
    expect("devanagari_spacing",
           "\xE0\xA4\xA8\xE0\xA4\xBF",
           1);  // na + vowel sign i (Mc)
    expect("tag_flag",
           "\xF0\x9F\x8F\xB4\xF3\xA0\x81\xA7\xF3\xA0\x81\xA2\xF3\xA0\x81\xA5"
           "\xF3\xA0\x81\xAE\xF3\xA0\x81\xA7\xF3\xA0\x81\xBF",
           1,
           2);
    expect("multi_marks", "e\xCC\x81\xCC\xA3", 1, 1);
    CHECK(gr_width("\xCC\x81") == 1);  // orphan mark gets a cell
    CHECK(gr_cp_width(0x200B) == 0);
    CHECK(gr_cp_width(0x2713) == 1);
    CHECK(gr_cp_width(0x2705) == 2);
    CHECK(gr_cp_width(0x4E2D) == 2);
    // invalid bytes: one cluster each, never loops forever
    expect("invalid", "\xFF\xFE\x80", 3);

    // property test: backward walk reproduces forward boundaries
    const std::vector<std::string> atoms = {"a",
                                            "b",
                                            " ",
                                            "\xCC\x81",
                                            "\xE2\x80\x8D",
                                            "\xF0\x9F\x91\xA8",
                                            "\xF0\x9F\x91\xA9",
                                            "\xF0\x9F\x87\xAF",
                                            "\xF0\x9F\x87\xB5",
                                            "\xEF\xB8\x8F",
                                            "\xE2\x9D\xA4",
                                            "\xED\x95\x9C",
                                            "\xE1\x84\x92",
                                            "\xE1\x85\xA1",
                                            "\xE1\x86\xAB",
                                            "\xE6\x97\xA5",
                                            "\xD8\x80",
                                            "\r",
                                            "\n",
                                            "\xF0\x9F\x8F\xBD",
                                            "\xE0\xA4\xBF",
                                            "\xFF"};
    std::mt19937 rng(12345);
    for(int iter = 0; iter < 20000; ++iter) {
        std::string s;
        const int n = 1 + rng() % 14;
        for(int k = 0; k < n; ++k) s += atoms[rng() % atoms.size()];
        std::vector<size_t> fwd{0};
        for(size_t i = 0; i < s.size();) {
            i = gr_next(s, i);
            fwd.push_back(i);
        }
        std::vector<size_t> bwd{s.size()};
        for(size_t i = s.size(); i > 0;) {
            i = gr_prev(s, i);
            bwd.push_back(i);
        }
        std::reverse(bwd.begin(), bwd.end());
        if(fwd != bwd) {
            std::printf(
                "FAIL fwd/bwd mismatch at iter %d (len %zu)\n", iter, s.size());
            ++fails;
            break;
        }
        for(size_t i = 0; i <= s.size(); ++i) {
            size_t sn = gr_snap(s, i);
            if(i < s.size() &&
               !(std::find(fwd.begin(), fwd.end(), sn) != fwd.end() &&
                 sn <= i)) {
                std::printf("FAIL snap\n");
                ++fails;
                break;
            }
        }
    }
    std::printf(fails ? "%d FAILURES\n" : "all grapheme tests passed\n", fails);
    return fails != 0;
}
