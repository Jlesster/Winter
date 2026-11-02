#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string_view>
#include <vector>

#include "buffer.hpp"
#include "options.hpp"

using OverlayId = int;

struct OverlaySpec {
    enum class At { Cursor, Screen, Center };
    int cursor_x = -1, cursor_y = -1;
    int x = 0, y = 0;
    int w = 1, h = 1;
    int z = 0;
    bool cover_bars = false;
    bool dismiss_on_move = true;
    bool esc_closes = true;
    bool visible = true;
    bool modal = false;
    bool framed = false;
    bool show_in_prompt = false;

    std::vector<int> pass{':'};

    std::optional<Border> border;
    Style border_style{};
    At at = At::Cursor;

    std::function<bool(int)> on_key;
    std::function<void()> on_close;
    std::function<void(OverlayId)> on_paint;
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
void overlay_draw(Screen& scr, const Viewport& v, bool prompt_active = false);

bool overlay_modal();
void overlay_set_cursor(OverlayId id, int x, int y);
bool overlay_cursor(const Viewport& v, int& sx, int& sy);

uint64_t overlay_stamp();
