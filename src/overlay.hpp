#pragma once

#include <cstdint>
#include <functional>
#include <string_view>

#include "buffer.hpp"

using OverlayId = int;

struct OverlaySpec {
    enum class At { Cursor, Screen };
    At at = At::Cursor;
    int x = 0, y = 0;
    int w = 1, h = 1;
    int z = 0;
    bool cover_bars = false;
    bool dismiss_on_move = true;
    bool esc_closes = true;
    bool visible = true;

    std::function<bool(int)> on_key;
    std::function<void()> on_close;
};

struct Viewport {
    int cols, text_rows, rows;
    int cur_x, cur_y;
};

struct Rect {
    int x, y, w, h;
};

Rect overlay_place(const OverlaySpec& s, const Viewport& v);

OverlayId overlay_open(OverlaySpec spec);
void overlay_close(OverlayId id);
void overlay_close_all();
bool overlay_any();

void overlay_clear(OverlayId id, Style bg = {});
int overlay_put(OverlayId id, int x, int y, std::string_view text, Style st);

bool overlay_key(int c);
void overlay_tick();
void overlay_draw(Screen& scr, const Viewport& v);

uint64_t overlay_stamp();
