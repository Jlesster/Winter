#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct Style {
    uint32_t fg = 0xFFFFFFFF;
    uint32_t bg = 0xFFFFFFFF;
    uint16_t attrs = 0;

    bool operator==(const Style&) const = default;
};

struct Span {
    size_t start, end;
    Style st;
};

struct Cell {
    char32_t chr = U' ';
    uint8_t width = 1;
    Style style{};
    char32_t mark = 0;
    bool operator==(const Cell&) const = default;
};

enum : uint16_t {
    ATTR_BOLD = 1 << 0,
    ATTR_DIM = 1 << 1,
    ATTR_ITALIC = 1 << 2,
    ATTR_UNDERLINE = 1 << 3,
    ATTR_REVERSE = 1 << 4,
};

class Screen {
    int w = 0, h = 0;
    std::vector<Cell> front, back;
    std::string out;
    int cur_x = 0, cur_y = 0;
    int scroll_rows = 0;

    void try_scroll();

public:
    void set_scroll_rows(int n) { scroll_rows = n; }
    void resize(int nw, int nh) {
        w = nw;
        h = nh;
        back.assign(w * h, Cell{});
        front.assign(w * h, Cell{U'\0'});
    }
    Cell& at(int x, int y) { return back[y * w + x]; }
    void cursor(int x, int y) {
        cur_x = x;
        cur_y = y;
    }
    int width() const { return w; }
    int height() const { return h; }
    void clear() { back.assign(w * h, Cell{}); }
    void flush();
};
