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

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "grapheme.hpp"

inline size_t utf8_seq_len(char lead) {
    const auto b = static_cast<unsigned char>(lead);
    return b >= 0xF0 ? 4 : b >= 0xE0 ? 3 : b >= 0xC0 ? 2 : 1;
}

inline void append_utf8(std::string& out, char32_t c) {
    if(c < 0x80) {
        out += static_cast<char>(c);
    } else if(c < 0x800) {
        out += static_cast<char>(0xC0 | (c >> 6));
        out += static_cast<char>(0x80 | (c & 0x3F));
    } else if(c < 0x10000) {
        out += static_cast<char>(0xE0 | (c >> 12));
        out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (c & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (c >> 18));
        out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (c & 0x3F));
    }
}

struct Decoded {
    char32_t cp;
    uint8_t len;
};

inline bool utf8_is_cont(char c) {
    return (static_cast<unsigned char>(c) & 0xC0) == 0x80;
}

inline Decoded utf8_decode(std::string_view s, size_t i) {
    constexpr Decoded bad{0xFFFD, 1};
    if(i >= s.size()) return bad;
    const auto b0 = static_cast<unsigned char>(s[i]);
    if(b0 < 0x80) return {b0, 1};
    size_t n;
    char32_t cp, min;
    if(b0 >= 0xC2 && b0 <= 0xDF) {
        n = 2;
        cp = b0 & 0x1F;
        min = 0x80;
    } else if(b0 >= 0xE0 && b0 <= 0xEF) {
        n = 3;
        cp = b0 & 0x0F;
        min = 0x800;
    } else if(b0 >= 0xF0 && b0 <= 0xF4) {
        n = 4;
        cp = b0 & 0x07;
        min = 0x10000;
    } else return bad;
    if(i + n > s.size()) return bad;
    for(size_t k = 1; k < n; k++) {
        if(!utf8_is_cont(s[i + k])) return bad;
        cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
    }
    if(cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return bad;
    return {cp, static_cast<uint8_t>(n)};
}

inline size_t utf8_next(std::string_view s, size_t i) {
    if(i >= s.size()) return s.size();
    return i + utf8_decode(s, i).len;
}

inline size_t utf8_prev(std::string_view s, size_t i) {
    if(i == 0) return 0;
    if(i > s.size()) i = s.size();
    size_t j = i - 1;
    size_t back = 0;
    while(j > 0 && back < 3 && utf8_is_cont(s[j])) {
        j--;
        back++;
    }
    if(j + utf8_decode(s, j).len == i) return j;
    return i - 1;
}

inline int utf8_width(char32_t c) { return gr_cp_width(c); }
