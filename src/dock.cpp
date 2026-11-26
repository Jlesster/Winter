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

#include "dock.hpp"

#include <algorithm>
#include <cstddef>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "chord.hpp"
#include "columns.hpp"
#include "core.hpp"
#include "editor.hpp"
#include "grapheme.hpp"
#include "keys.hpp"
#include "options.hpp"
#include "output.hpp"
#include "overlay.hpp"
#include "render.hpp"
#include "src/buffer.hpp"
#include "src/panel.hpp"
#include "theme.hpp"

std::string_view dock_clip(std::string_view s, int w) {
    int used = 0;
    size_t at = 0;
    while(at < s.size()) {
        const size_t e = gr_next(s, at);
        const int cw = cluster_width(s, at, e, used);
        if(used + cw > w) break;
        used += cw;
        at = e;
    }
    return s.substr(0, at);
}

namespace {

struct Slot {
    std::unique_ptr<Panel> p;
    int w = -1, h = -1;  // body size the panel last heard about
};

std::map<PanelId, Slot> g_panels;
PanelId g_next = 1;
PanelId g_active = 0;
bool g_dock = false;
bool g_focus = false;
OutMode g_last = OutMode::Bottom;  // where dock_toggle reopens
OverlayId g_float = 0;
int g_float_w = 0, g_float_h = 0;
int g_dock_rows = 0;
PanelId g_notified = 0;  // the panel that was told it has focus

Slot* slot(PanelId id) {
    const auto it = g_panels.find(id);
    return it == g_panels.end() ? nullptr : &it->second;
}

Slot* active() { return slot(g_active); }

bool visible() { return (g_dock || g_float) && active(); }

// Panels hear focus changes, not the host's bookkeeping.
void notify() {
    const PanelId want = g_focus && visible() ? g_active : 0;
    if(want == g_notified) return;
    if(Slot* s = slot(g_notified)) s->p->focus_changed(false);
    g_notified = want;
    if(Slot* s = slot(want)) s->p->focus_changed(true);
}

void sized(Slot& s, int w, int h) {
    if(s.w == w && s.h == h) return;
    s.w = w;
    s.h = h;
    s.p->resize(w, h);
}

bool route(int c) {
    Slot* s = active();
    if(!g_focus || !visible() || !s) return false;
    if(!s->p->raw_keys() && E.mode != NORMAL) return false;
    return s->p->key(c);
}

void close_float() {
    if(g_float) {
        const OverlayId id = g_float;
        g_float = 0;
        overlay_close(id);
    }
}

void open_float() {
    if(g_float) return;
    OverlaySpec s;
    s.at = OverlaySpec::At::Center;
    s.w = std::max(24, editor_screen_cols() * 8 / 10);
    s.h = std::max(8, editor_message_row() * 6 / 10);
    s.framed = true;
    s.dismiss_on_move = false;
    s.on_paint = [](OverlayId id) {
        overlay_clear(id, Style{});
        Slot* a = active();
        if(!a) return;
        overlay_set_title(id, a->p->title());
        overlay_set_footer(id, a->p->status());
        const int w = g_float_w - 2, h = g_float_h - 2;
        if(w < 1 || h < 1) return;
        sized(*a, w, h);
        a->p->paint(
            [id](int x, int y, std::string_view t, Style st) {
                overlay_put(id, x + 1, y + 1, t, st);
            },
            w,
            h);
        int cx = 0, cy = 0;
        if(a->p->cursor(cx, cy)) overlay_set_cursor(id, cx + 1, cy + 1);
        else overlay_set_cursor(id, -1, -1);
    };
    s.on_key = [](int c) { return route(c); };
    s.on_close = [] {
        g_float = 0;
        g_focus = false;
        notify();
    };
    g_float_w = s.w;
    g_float_h = s.h;
    g_float = overlay_open(std::move(s));
}

}  // namespace

// ---- panels ---------------------------------------------------------------

PanelId dock_add(std::unique_ptr<Panel> p) {
    const PanelId id = g_next++;
    g_panels[id].p = std::move(p);
    return id;
}

void dock_remove(PanelId id) {
    if(id == g_active) {
        dock_close();
        g_active = 0;
    }
    if(id == g_notified) g_notified = 0;
    g_panels.erase(id);
}

void dock_show(PanelId id, OutMode m) {
    if(!slot(id)) return;
    if(m == OutMode::Full) return dock_close();
    g_active = id;
    g_last = m;
    if(m == OutMode::Bottom) {
        close_float();
        g_dock = true;
    } else {
        g_dock = false;
        open_float();
    }
    notify();
}

void dock_switch(PanelId id) {
    if(!slot(id)) return;
    g_active = id;
    notify();
}

void dock_close() {
    g_dock = false;
    g_focus = false;
    close_float();
    notify();
}

void dock_focus(bool on) {
    g_focus = on && visible();
    notify();
}

void dock_toggle() {
    if(visible()) return dock_close();
    if(g_active) dock_show(g_active, g_last);
}

bool dock_visible() { return visible(); }
bool dock_focused() { return g_focus && visible(); }
PanelId dock_current() { return g_active; }

bool dock_raw() {
    const Slot* s = active();
    return dock_focused() && s && s->p->raw_keys();
}

bool dock_pinned() {
    const Slot* s = active();
    return visible() && s && s->p->pinned();
}

// ---- geometry and drawing -------------------------------------------------

void dock_layout(int total) {
    g_dock_rows = 0;
    if(!g_dock || !active() || total < 8) return;
    const int h = std::clamp(opts.output_height, 3, 50);
    g_dock_rows = std::min(h, total - 4);  // the text area keeps 4 rows
    if(g_dock_rows < 2) g_dock_rows = 0;
}

int dock_rows() { return g_dock_rows; }

void dock_paint(const PanelPut& put, int w, int h) {
    Slot* s = active();
    if(!s || w < 4 || h < 1) return;
    Panel& p = *s->p;

    // Body first: painting may move the panel's view, and the title bar
    // reports where it ended up.
    if(h > 1) {
        sized(*s, w, h - 1);
        p.paint(
            [&](int x, int y, std::string_view t, Style st) {
                put(x, y + 1, t, st);
            },
            w,
            h - 1);
    }

    const Style bar =
        theme_ui("output.title",
                 Style{.fg = 0xcdd6f4, .bg = 0x313244, .attrs = ATTR_BOLD});
    put(0, 0, std::string(static_cast<size_t>(w), ' '), bar);
    std::string right = " " + p.status();
    if(!g_focus) right += "<C-w>{h,j,k,l} to focus windows";
    right += ' ';
    const int rw = text_width(right);
    const std::string left = " " + p.title();
    put(0, 0, std::string(dock_clip(left, std::max(w - rw - 1, 0))), bar);
    Style ss = p.title_style();
    ss.bg = bar.bg;
    if(w > rw) put(w - rw, 0, right, ss);
}

bool dock_cursor(int& sx, int& sy) {
    const Slot* s = active();
    if(!g_dock || g_dock_rows == 0 || !dock_focused() || !s) return false;
    int x = 0, y = 0;
    if(!s->p->cursor(x, y)) return false;
    sx = x;
    sy = y + 1;  // the title bar
    return true;
}

bool dock_key(int c) {
    if(g_float) return false;  // the overlay routes its own keys
    return route(c);
}

namespace {
Chord g_tchord;  // <C-w>h etc. while a raw-key panel (terminal) has focus

void term_replay(int k) {
    if(Slot* s = active()) s->p->key(k);
}
}  // namespace

bool dock_raw_key(int c) {
    if(!dock_raw()) {
        g_tchord.reset();
        return false;
    }
    if(c == '\x1b' && g_tchord.pending()) g_tchord.timeout(term_replay);
    // t-mode binds first; anything that isn't one goes to the shell
    if(c != PASTE &&
       g_tchord.feed(TERMINAL, c, term_replay) != Chord::Result::Pass)
        return true;
    active()->p->key(c);
    return true;
}

bool dock_chord_pending() { return g_tchord.pending(); }
void dock_chord_timeout() { g_tchord.timeout(term_replay); }
