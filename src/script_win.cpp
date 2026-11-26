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

// winter.win: the window layout for plugins. A window is addressed by its id
// (stable for the window's life; 0 = the focused window). Pointers into the
// layout are NOT kept between calls: win::only() rebuilds the focused window.
//
// Lines and columns are 1-based like the rest of winter.*; x/y are 0-based
// screen cells.
#include <algorithm>
#include <cstring>
#include <lua.hpp>
#include <string>
#include <vector>

#include "buflist.hpp"
#include "columns.hpp"
#include "core.hpp"
#include "editor.hpp"
#include "events.hpp"
#include "grapheme.hpp"
#include "normal.hpp"
#include "options.hpp"
#include "script_internal.hpp"
#include "window.hpp"
#include "window_ed.hpp"

namespace {

using win::Win;

Win* find(lua_Integer id) {
    windows_ensure();
    if(id == 0) return win::focus();
    for(Win* w : win::all())
        if(w->id == id) return w;
    return nullptr;
}

// Raises for a bad id: call before any C++ object with a destructor exists.
Win* need(lua_State* L, int i) {
    Win* w = find(luaL_checkinteger(L, i));
    if(!w) luaL_error(L, "invalid window id");
    return w;
}

bool is_focus(const Win* w) { return w == win::focus(); }

Buffer& buf_of(const Win* w) { return is_focus(w) ? E.buf() : *w->buf; }

Buffer* buffer_by_id(lua_Integer id, size_t* index = nullptr) {
    for(size_t i = 0; i < E.buffers.size(); ++i)
        if(static_cast<lua_Integer>(E.buffers[i]->id) == id) {
            if(index) *index = i;
            return E.buffers[i].get();
        }
    return nullptr;
}

void set_int(lua_State* L, const char* k, lua_Integer v) {
    lua_pushinteger(L, v);
    lua_setfield(L, -2, k);
}

// The focused window's view lives in E, the others' is parked in the Win.
struct View {
    int cx, cy, row_offset, col_offset;
};
View view_of(const Win* w) {
    return is_focus(w) ? View{E.cx, E.cy, E.row_offset, E.col_offset}
                       : View{w->cx, w->cy, w->row_offset, w->col_offset};
}

int text_rows(const Win* w) {
    return is_focus(w) ? E.term_rows : std::max(w->rect.h - 1, 1);
}

// cursor to (cy, cx) in that window, normal-mode legal
void put_cursor(Win* w, int cy, int cx) {
    const Buffer& b = buf_of(w);
    const int n = static_cast<int>(b.pt.line_count());
    cy = std::clamp(cy, 0, n - 1);
    const std::string t = b.pt.line_text(static_cast<size_t>(cy));
    const int size = static_cast<int>(t.size());
    cx = snap_cx(t, std::clamp(cx, 0, size));
    if(cx >= size && size > 0 && !(is_focus(w) && insertish(E.mode)))
        cx = static_cast<int>(gr_prev(t, static_cast<size_t>(size)));
    if(is_focus(w)) {
        E.cy = cy;
        E.cx = cx;
        update_want();
    } else {
        w->cy = cy;
        w->cx = cx;
        w->want_rx = cx_to_rx(t, cx);
    }
}

// ---- reading ------------------------------------------------------------------

int l_current(lua_State* L) {
    windows_ensure();
    lua_pushinteger(L, win::focus()->id);
    return 1;
}

// winter.win.list() -> { id, ... } in layout order (left to right, top to bottom)
int l_list(lua_State* L) {
    windows_ensure();
    lua_newtable(L);
    lua_Integer i = 1;
    for(const Win* w : win::all()) {
        lua_pushinteger(L, w->id);
        lua_rawseti(L, -2, i++);
    }
    return 1;
}

// winter.win.info(id) -> {id, buf, focused, x, y, width, height, line, col, top,
//                         left} | nil
// buf is a stable buffer id; width/height are the text area (height excludes
// the status row); top is the first visible line, left the first visible column.
int l_info(lua_State* L) {
    const Win* w = find(luaL_checkinteger(L, 1));
    if(!w) {
        lua_pushnil(L);
        return 1;
    }
    const View v = view_of(w);
    lua_createtable(L, 0, 11);
    set_int(L, "id", w->id);
    set_int(L, "buf", static_cast<lua_Integer>(buf_of(w).id));
    lua_pushboolean(L, is_focus(w));
    lua_setfield(L, -2, "focused");
    set_int(L, "x", w->rect.x);
    set_int(L, "y", w->rect.y);
    set_int(L, "width", w->rect.w);
    set_int(L, "height", text_rows(w));
    set_int(L, "line", v.cy + 1);
    set_int(L, "col", v.cx + 1);
    set_int(L, "top", v.row_offset + 1);
    set_int(L, "left", v.col_offset + 1);
    return 1;
}

// winter.win.showing(buf) -> { id, ... }: the windows displaying buffer `buf`
int l_showing(lua_State* L) {
    windows_ensure();
    const Buffer* b = buffer_by_id(luaL_checkinteger(L, 1));
    lua_newtable(L);
    if(!b) return 1;
    lua_Integer i = 1;
    for(const Win* w : win::showing(b)) {
        lua_pushinteger(L, w->id);
        lua_rawseti(L, -2, i++);
    }
    return 1;
}

// ---- cursor, buffer, view ----------------------------------------------------------

int l_get_cursor(lua_State* L) {
    const Win* w = need(L, 1);
    const View v = view_of(w);
    lua_pushinteger(L, v.cy + 1);
    lua_pushinteger(L, v.cx + 1);
    return 2;
}

// winter.win.set_cursor(id, line [, col=1]): clamped into the buffer
int l_set_cursor(lua_State* L) {
    Win* w = need(L, 1);
    const lua_Integer line = luaL_checkinteger(L, 2);
    const lua_Integer col = luaL_optinteger(L, 3, 1);
    put_cursor(w, static_cast<int>(std::clamp<lua_Integer>(line - 1, 0, 1 << 30)),
               static_cast<int>(std::clamp<lua_Integer>(col - 1, 0, 1 << 30)));
    return 0;
}

int l_get_buf(lua_State* L) {
    const Win* w = need(L, 1);
    lua_pushinteger(L, static_cast<lua_Integer>(buf_of(w).id));
    return 1;
}

// winter.win.set_buf(id, buf) -> bool: show buffer `buf` (a stable id) in the
// window. A window that is not focused gets the buffer's remembered cursor and
// scroll; entering it later fires buf_enter as usual.
int l_set_buf(lua_State* L) {
    Win* w = need(L, 1);
    size_t index = 0;
    Buffer* b = buffer_by_id(luaL_checkinteger(L, 2), &index);
    if(!b) {
        lua_pushboolean(L, 0);
        return 1;
    }
    if(is_focus(w)) {
        buffer_switch(index);
    } else {
        w->buf = b;
        w->cx = b->cx;
        w->cy = b->cy;
        w->want_rx = b->want_rx;
        w->row_offset = b->row_offset;
        w->row_sub = 0;
        w->col_offset = b->col_offset;
    }
    lua_pushboolean(L, 1);
    return 1;
}

// winter.win.set_top(id, line): make `line` the first visible line. The cursor
// is pulled into view (keeping 'scrolloff'), otherwise the next frame would
// scroll straight back. Wrapped lines and closed folds are not accounted for.
int l_set_top(lua_State* L) {
    Win* w = need(L, 1);
    const lua_Integer line = luaL_checkinteger(L, 2);
    const Buffer& b = buf_of(w);
    const int n = static_cast<int>(b.pt.line_count());
    const int top = static_cast<int>(std::clamp<lua_Integer>(line - 1, 0, n - 1));
    const int rows = text_rows(w);
    const int so = std::min(opts.scrolloff, (rows - 1) / 2);
    const View v = view_of(w);
    const int cy = std::clamp(v.cy, top + so, std::max(top + rows - 1 - so, top));
    if(is_focus(w)) {
        E.row_offset = top;
        E.row_sub = 0;
    } else {
        w->row_offset = top;
        w->row_sub = 0;
    }
    if(cy != v.cy) put_cursor(w, cy, v.cx);
    return 0;
}

// ---- layout ----------------------------------------------------------------------------

int l_focus(lua_State* L) {
    Win* w = need(L, 1);
    win_focus_window(w);
    lua_pushboolean(L, 1);
    return 1;
}

// winter.win.split([{vertical=false, before=true}]) -> new window id
// The new window shows the same buffer and view, and takes focus.
int l_split(lua_State* L) {
    bool vertical = false, before = true;
    if(lua_istable(L, 1)) {
        lua_getfield(L, 1, "vertical");
        vertical = lua_toboolean(L, -1) != 0;
        lua_getfield(L, 1, "before");
        if(!lua_isnil(L, -1)) before = lua_toboolean(L, -1) != 0;
        lua_pop(L, 2);
    }
    if(!win_split(vertical, before)) {
        lua_pushnil(L);
        return 1;
    }
    lua_pushinteger(L, win::focus()->id);
    return 1;
}

// winter.win.close(id [, force]) -> true | false, err
// Refuses the last window, and (without force) a window holding the only view
// of a modified buffer.
int l_close(lua_State* L) {
    Win* w = need(L, 1);
    const bool force = lua_toboolean(L, 2) != 0;
    std::string err;
    {
        if(win::count() <= 1) {
            err = "cannot close the last window";
        } else {
            const Buffer& b = buf_of(w);
            const bool elsewhere = win::showing(&b).size() > 1;
            if(!force && !elsewhere && !b.nofile && b.pt.modified()) {
                err = "buffer has unsaved changes";
            } else if(is_focus(w)) {
                win_close_current(true);
            } else {
                events_emit("win_close", {std::to_string(w->id)});
                win::close(w);
                win_apply_rect(win::focus());  // the survivors changed size
            }
        }
    }
    if(!err.empty()) {
        lua_pushboolean(L, 0);
        lua_pushlstring(L, err.data(), err.size());
        return 2;
    }
    lua_pushboolean(L, 1);
    return 1;
}

int l_only(lua_State*) {
    win_only();
    return 0;
}

// winter.win.cmd(key [, count]) -> bool: any CTRL-W command ("x" swap, "r"
// rotate, "H" move far left, "=" equalize, ...), like :wincmd.
int l_cmd(lua_State* L) {
    size_t n = 0;
    const char* k = luaL_checklstring(L, 1, &n);
    luaL_argcheck(L, n == 1, 1, "a one-character CTRL-W key");
    lua_pushboolean(L, win_do(k[0], static_cast<int>(luaL_optinteger(L, 2, 0))));
    return 1;
}

// winter.win.neighbor(dir) -> id | nil: the window next to the focused one.
// dir: "left" "down" "up" "right" (or h j k l)
int l_neighbor(lua_State* L) {
    windows_ensure();
    const char* s = luaL_checkstring(L, 1);  // no C++ objects: argerror longjmps
    win::Dir d;
    if(!std::strcmp(s, "left") || !std::strcmp(s, "h")) d = win::Dir::Left;
    else if(!std::strcmp(s, "down") || !std::strcmp(s, "j")) d = win::Dir::Down;
    else if(!std::strcmp(s, "up") || !std::strcmp(s, "k")) d = win::Dir::Up;
    else if(!std::strcmp(s, "right") || !std::strcmp(s, "l")) d = win::Dir::Right;
    else return luaL_argerror(L, 1, "left, down, up or right");
    const Win* f = win::focus();
    const Win* n = win::neighbor(d, f->rect.x + f->rect.w / 2, f->rect.y + f->rect.h / 2);
    if(n) lua_pushinteger(L, n->id);
    else lua_pushnil(L);
    return 1;
}

// winter.win.resize(dw, dh): grow (or shrink) the focused window by that many
// columns / rows
int l_resize(lua_State* L) {
    windows_ensure();
    const int dw = static_cast<int>(luaL_optinteger(L, 1, 0));
    const int dh = static_cast<int>(luaL_optinteger(L, 2, 0));
    if(dw) win::resize(true, dw);
    if(dh) win::resize(false, dh);
    return 0;
}

int l_equalize(lua_State*) {
    windows_ensure();
    win::equalize();
    return 0;
}

// winter.win.maximize("width" | "height"): the focused window takes the space
int l_maximize(lua_State* L) {
    windows_ensure();
    const char* s = luaL_checkstring(L, 1);
    luaL_argcheck(L, !std::strcmp(s, "width") || !std::strcmp(s, "height"), 1,
                  "width or height");
    win::maximize(!std::strcmp(s, "width"));
    return 0;
}

const luaL_Reg kWin[] = {{"current", l_current},
                         {"list", l_list},
                         {"info", l_info},
                         {"showing", l_showing},
                         {"get_cursor", l_get_cursor},
                         {"set_cursor", l_set_cursor},
                         {"get_buf", l_get_buf},
                         {"set_buf", l_set_buf},
                         {"set_top", l_set_top},
                         {"focus", l_focus},
                         {"split", l_split},
                         {"close", l_close},
                         {"only", l_only},
                         {"cmd", l_cmd},
                         {"neighbor", l_neighbor},
                         {"resize", l_resize},
                         {"equalize", l_equalize},
                         {"maximize", l_maximize},
                         {nullptr, nullptr}};

}  // namespace

void script_open_win() { script_register("win", kWin); }
