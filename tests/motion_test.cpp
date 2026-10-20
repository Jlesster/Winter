// Golden cases generated from real vim 9.1 (:normal! on the same text).
// Each row: text, start byte offset, keys, expected byte offset.
#include <cctype>
#include <cstdio>
#include <map>
#include <string>
#include <string_view>

#include "../src/motion.hpp"
#include "../src/piece.hpp"

namespace {

struct Case {
    const char* text;
    size_t pos;
    const char* keys;
    size_t want;
};

const Case kCases[] = {
#include "motion_cases.inc"
};

const std::map<std::string_view, motion::Fn> kMotions = {
    {"h", motion::left},          {"l", motion::right},
    {"0", motion::to_line_start}, {"^", motion::to_first_nonblank},
    {"$", motion::to_line_end},   {"w", motion::next_word},
    {"W", motion::next_WORD},     {"b", motion::prev_word},
    {"B", motion::prev_WORD},     {"e", motion::end_word},
    {"E", motion::end_WORD},      {"gg", motion::to_file_start},
    {"G", motion::to_file_end},   {"{", motion::para_back},
    {"}", motion::para_fwd},      {"%", motion::match_pair},
};

Target run(const PieceTable& t, size_t pos, std::string_view keys) {
    int count = 0;
    size_t i = 0;
    while(i < keys.size() && std::isdigit(static_cast<unsigned char>(keys[i])) &&
          !(keys[i] == '0' && count == 0))
        count = count * 10 + (keys[i++] - '0');
    const std::string_view k = keys.substr(i);

    using motion::Find;
    switch(k[0]) {
        case 'f': return motion::find_char(t, pos, count, Find::f, k.substr(1), false);
        case 'F': return motion::find_char(t, pos, count, Find::F, k.substr(1), false);
        case 't': return motion::find_char(t, pos, count, Find::t, k.substr(1), false);
        case 'T': return motion::find_char(t, pos, count, Find::T, k.substr(1), false);
        default: break;
    }
    return kMotions.at(k)(t, pos, count);
}

}  // namespace

int main() {
    int failed = 0;
    for(const Case& c : kCases) {
        // PieceTable views its original text, so the string must outlive it
        const std::string text = c.text;
        const PieceTable t{text};
        const Target got = run(t, c.pos, c.keys);
        // a failed motion leaves the cursor where it was, like vim
        const size_t at = got.ok ? got.pos : c.pos;
        if(at != c.want) {
            std::printf("FAIL %-4s at %zu in \"%s\": got %zu, vim %zu\n", c.keys,
                        c.pos, c.text, at, c.want);
            ++failed;
        }
    }
    std::printf("%zu cases, %d failed\n", sizeof(kCases) / sizeof(kCases[0]), failed);
    return failed != 0;
}
