#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
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
    bool cursor_block = false;  // hardware cursor keeps the normal-mode shape

    std::vector<int> pass{':'};

    std::optional<Border> border;
    Style border_style{};
    At at = At::Cursor;

    std::function<bool(int)> on_key;
    std::function<void()> on_close;
    std::function<void(OverlayId)> on_paint;

    std::string title;   // drawn in the top border, framed only
    std::string footer;  // drawn in the bottom border, right aligned
    bool tee_bottom =
        false;  // bottom corners become tees (box continues below)
    std::vector<int>
        dividers;  // rows inside the frame drawn as a rule joined to both sides

    // Inner rows [first, last) drawn as the focused region: their side edges
    // and the rules directly above and below take `focus_border`.
    std::optional<std::pair<int, int>> focus_span;
    Style focus_border{};
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
// Changes an open overlay's spec in place (e.g. to make it modal). The cell
// buffer is resized if fn changed w or h.
void overlay_edit(OverlayId id, const std::function<void(OverlaySpec&)>& fn);
void overlay_close(OverlayId id);
void overlay_close_all();
bool overlay_any();

void overlay_clear(OverlayId id, Style bg = {});
int overlay_put(OverlayId id, int x, int y, std::string_view text, Style st);

bool overlay_key(int c);
void overlay_tick();
void overlay_draw(Screen& scr, const Viewport& v, bool prompt_active = false);

bool overlay_modal();
void overlay_set_title(OverlayId id, std::string title);
void overlay_set_footer(OverlayId id, std::string footer);
void overlay_set_cursor(OverlayId id, int x, int y);
bool overlay_cursor(const Viewport& v, int& sx, int& sy,
                    bool* block = nullptr);

uint64_t overlay_stamp();
