#include "buffer.hpp"
#include <unistd.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <iterator>
#include "utf8.hpp"

static void emit_sgr(std::string& out, const Style& s) {
    out += "\x1b[0";
    if(s.attrs & ATTR_BOLD) out += ";1";
    if(s.attrs & ATTR_DIM) out += ";2";
    if(s.attrs & ATTR_ITALIC) out += ";3";
    if(s.attrs & ATTR_UNDERCURL) out += ";4:3";
    else if(s.attrs & ATTR_UNDERLINE) out += ";4";
    if(s.attrs & ATTR_REVERSE) out += ";7";
    if(s.fg != 0xFFFFFFFF)
        std::format_to(std::back_inserter(out),
                       ";38;2;{};{};{}",
                       (s.fg >> 16) & 0xFF,
                       (s.fg >> 8) & 0xFF,
                       s.fg & 0xFF);
    if(s.bg != 0xFFFFFFFF)
        std::format_to(std::back_inserter(out),
                       ";48;2;{};{};{}",
                       (s.bg >> 16) & 0xFF,
                       (s.bg >> 8) & 0xFF,
                       s.bg & 0xFF);
    if(s.ul != 0xFFFFFFFF)
        std::format_to(std::back_inserter(out),
                       ";58;2;{};{};{}",
                       (s.ul >> 16) & 0xFF,
                       (s.ul >> 8) & 0xFF,
                       s.ul & 0xFF);
    out += 'm';
}

void Screen::try_scroll() {
    const int rows = std::min(scroll_rows, h);
    if(rows < 4) return;
    const auto row_eq = [&](int yb, int yf) {
        const auto b = back.begin() + static_cast<std::ptrdiff_t>(yb) * w;
        return std::equal(
            b, b + w, front.begin() + static_cast<std::ptrdiff_t>(yf) * w);
    };
    std::vector<char> same(static_cast<size_t>(rows));
    int changed = 0;
    for(int y = 0; y < rows; ++y) {
        same[static_cast<size_t>(y)] = row_eq(y, y);
        changed += !same[static_cast<size_t>(y)];
    }
    if(changed < std::max(4, rows / 4)) return;

    const auto rescued = [&](int y, int s) {
        const int src = y + s;
        return src >= 0 && src < rows && !same[static_cast<size_t>(y)] &&
               row_eq(y, src);
    };
    int best = 0, best_gain = 0;
    for(int k = 1; k < rows && best_gain < rows - k; ++k) {
        for(const int s : {k, -k}) {
            int gain = 0;
            for(int y = 0; y < rows; ++y) gain += rescued(y, s);
            if(gain > best_gain) {
                best_gain = gain;
                best = s;
            }
        }
    }
    if(best_gain < std::max(3, changed / 2)) return;

    const int m = std::abs(best);
    int lo = rows, hi = -1;
    for(int y = 0; y < rows; ++y)
        if(rescued(y, best)) {
            lo = std::min(lo, y);
            hi = std::max(hi, y);
        }
    const int top = best > 0 ? lo : lo - m;
    const int bot = best > 0 ? hi + m : hi;

    out += "\x1b[0m";
    std::format_to(std::back_inserter(out),
                   "\x1b[{};{}r\x1b[{}{}\x1b[r",
                   top + 1,
                   bot + 1,
                   m,
                   best > 0 ? 'S' : 'T');

    const auto fr = [&](int y) {
        return front.begin() + static_cast<std::ptrdiff_t>(y) * w;
    };
    if(best > 0) {
        for(int y = top; y <= bot - m; ++y)
            std::copy(fr(y + m), fr(y + m) + w, fr(y));
        for(int y = bot - m + 1; y <= bot; ++y)
            std::fill(fr(y), fr(y) + w, Cell{});
    } else {
        for(int y = bot; y >= top + m; --y)
            std::copy(fr(y - m), fr(y - m) + w, fr(y));
        for(int y = top; y < top + m; ++y) std::fill(fr(y), fr(y) + w, Cell{});
    }
}

void Screen::flush() {
    out.clear();
    out += "\x1b[?2026h\x1b[?25l";
    if(scroll_rows > 0) try_scroll();
    int cx = -1, cy = -1;
    Style cur{};
    bool style_known = false;
    for(int y = 0; y < h; ++y) {
        for(int x = 0; x < w; ++x) {
            const int i = y * w + x;
            const Cell& b = back[i];
            if(b == front[i]) continue;
            if(b.width == 0) {
                front[i] = b;
                continue;
            }
            if(cy != y || cx != x)
                std::format_to(
                    std::back_inserter(out), "\x1b[{};{}H", y + 1, x + 1);
            if(!style_known || b.style != cur) {
                emit_sgr(out, b.style);
                cur = b.style;
                style_known = true;
            }
            append_utf8(out, b.chr);
            if(b.mark) append_utf8(out, b.mark);
            front[i] = b;
            cx = x + b.width;
            cy = y;
            if(cx >= w) cx = -1;
        }
    }
    out += "\x1b[0m";
    std::format_to(std::back_inserter(out),
                   "\x1b[{};{}H\x1b[?25h\x1b[?2026l",
                   cur_y + 1,
                   cur_x + 1);
    for(size_t off = 0; off < out.size();) {
        const ssize_t n =
            ::write(STDOUT_FILENO, out.data() + off, out.size() - off);
        if(n <= 0) break;
        off += static_cast<size_t>(n);
    }
}
