#include "columns.hpp"

#include "core.hpp"
#include "utf8.hpp"

bool is_ctl(char32_t cp) { return cp < 0x20 || (cp >= 0x7F && cp < 0xA0); }

int cell_width(char32_t cp, int rx) {
    if(cp == '\t') return WINTER_TAB_STOP - (rx % WINTER_TAB_STOP);
    if(is_ctl(cp)) return 1;
    return utf8_width(cp);
}

int snap_cx(std::string_view line, int cx) {
    if(cx <= 0) return 0;
    if(cx >= static_cast<int>(line.size()))
        return static_cast<int>(line.size());
    return static_cast<int>(utf8_prev(line, utf8_next(line, cx)));
}

int cx_to_rx(std::string_view line, int cx) {
    int rx = 0;
    for(int j = 0;
        j < static_cast<int>(line.size()) && static_cast<int>(j) < cx;) {
        const Decoded d = utf8_decode(line, j);
        rx += cell_width(d.cp, rx);
        j += d.len;
    }
    return rx;
}

int rx_to_cx(std::string_view line, int rx) {
    int cur = 0;
    size_t i = 0;
    while(i < line.size()) {
        const Decoded d = utf8_decode(line, i);
        cur += cell_width(d.cp, cur);
        if(cur > rx) return static_cast<int>(i);
        i += d.len;
    }
    return static_cast<int>(i);
}

int text_width(std::string_view s) {
    int w = 0;
    for(size_t i = 0; i < s.size();) {
        const Decoded d = utf8_decode(s, i);
        w += cell_width(d.cp, w);
        i += d.len;
    }
    return w;
}
