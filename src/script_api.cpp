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

// winter.api: read and edit ANY buffer by its stable id (0 = the current one),
// the way nvim_buf_* does. winter.buf.* keeps addressing the current buffer;
// this is the layer buffer handles (runtime.lua) and plugins build on.
//
// Lines and columns are 1-based like the rest of winter.*, line ranges are
// inclusive, columns are byte columns.
#include <algorithm>
#include <cstddef>
#include <lua.hpp>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "columns.hpp"
#include "core.hpp"
#include "edit_bus.hpp"
#include "editor.hpp"
#include "events.hpp"
#include "grapheme.hpp"
#include "motion.hpp"
#include "normal.hpp"
#include "script_internal.hpp"
#include "shell.hpp"

namespace {

Buffer* find_buf(lua_Integer id) {
    if(id == 0) return &E.buf();
    for(const auto& b : E.buffers)
        if(static_cast<lua_Integer>(b->id) == id) return b.get();
    return nullptr;
}

// Raises for a bad id. Call before any C++ object with a destructor exists:
// luaL_error longjmps.
Buffer* need_buf(lua_State* L, int i) {
    const lua_Integer id = luaL_checkinteger(L, i);
    Buffer* b = find_buf(id);
    if(!b) luaL_error(L, "invalid buffer id %d", static_cast<int>(id));
    return b;
}

bool is_current(const Buffer& b) { return &b == &E.buf(); }

// Keep the buffer's cursor legal after an edit. Background buffers carry
// their cursor in Buffer; the current one in E.
void settle(Buffer& b) {
    const int n = static_cast<int>(b.pt.line_count());
    if(is_current(b)) {
        E.cy = std::clamp(E.cy, 0, n - 1);
        if(!insertish(E.mode))
            set_cursor_offset(motion::clamp_normal(b.pt, cursor_offset()));
        update_want();
        return;
    }
    b.cy = std::clamp(b.cy, 0, n - 1);
    const std::string t = b.pt.line_text(static_cast<size_t>(b.cy));
    b.cx = snap_cx(t, std::min(b.cx, static_cast<int>(t.size())));
    b.sync_anchors();  // nobody is rendering it: keep decos/diags in step
}

size_t offset_in(const PieceTable& t, lua_Integer line, lua_Integer col) {
    const size_t l = static_cast<size_t>(line - 1);
    const size_t want =
        t.line_start(l) + static_cast<size_t>(std::max<lua_Integer>(col - 1, 0));
    return std::min(want, t.line_end(l));
}

// {string...} at idx -> lines joined with \n; false when not a list of strings
bool join_lines(lua_State* L, int idx, std::string& out, lua_Integer& cnt) {
    idx = lua_absindex(L, idx);
    cnt = static_cast<lua_Integer>(lua_rawlen(L, idx));
    for(lua_Integer i = 1; i <= cnt; ++i) {
        lua_rawgeti(L, idx, i);
        if(lua_type(L, -1) != LUA_TSTRING) {
            lua_pop(L, 1);
            return false;
        }
        size_t len;
        const char* s = lua_tolstring(L, -1, &len);
        if(i > 1) out += '\n';
        out.append(s, len);
        lua_pop(L, 1);
    }
    return true;
}

// same rules as winter.buf.set_lines
void apply_lines(Buffer& b, lua_Integer first, lua_Integer last,
                 lua_Integer cnt, const std::string& text) {
    PieceTable& t = b.pt;
    const lua_Integer n = static_cast<lua_Integer>(t.line_count());
    std::unique_ptr<PieceTable::Group> group;
    if(!is_current(b) || !insertish(E.mode))
        group = std::make_unique<PieceTable::Group>(t);
    const size_t f = static_cast<size_t>(first - 1);
    if(last < first) {  // nothing replaced: insert before line `first`
        if(cnt == 0) return;
        if(first <= n) t.insert(t.line_start(f), text + "\n");
        else t.insert(t.size(), "\n" + text);
    } else {
        const size_t lo = t.line_start(f);
        const size_t hi = t.line_end(static_cast<size_t>(last - 1));
        if(cnt > 0) {
            if(hi > lo) t.remove(lo, hi - lo);
            t.insert(lo, text);
        } else if(hi < t.size()) {
            t.remove(lo, hi + 1 - lo);
        } else if(lo > 0) {
            t.remove(lo - 1, hi - (lo - 1));
        } else {
            t.remove(lo, hi - lo);
        }
    }
    settle(b);
}

// ---- buffer ids ---------------------------------------------------------------

// winter.buf.by_id(id) -> index | nil   (the index winter.buf.* functions take)
int l_by_id(lua_State* L) {
    const lua_Integer id = luaL_checkinteger(L, 1);
    for(size_t i = 0; i < E.buffers.size(); ++i)
        if(static_cast<lua_Integer>(E.buffers[i]->id) == id) {
            lua_pushinteger(L, static_cast<lua_Integer>(i) + 1);
            return 1;
        }
    lua_pushnil(L);
    return 1;
}

// winter.buf.id([index]) -> id | nil   (index defaults to the current buffer)
int l_id(lua_State* L) {
    const lua_Integer n =
        luaL_optinteger(L, 1, static_cast<lua_Integer>(E.cur) + 1);
    if(n < 1 || n > static_cast<lua_Integer>(E.buffers.size())) {
        lua_pushnil(L);
        return 1;
    }
    lua_pushinteger(L, static_cast<lua_Integer>(E.buffers[n - 1]->id));
    return 1;
}

// ---- winter.api -----------------------------------------------------------------

int l_valid(lua_State* L) {
    lua_pushboolean(L, find_buf(luaL_checkinteger(L, 1)) != nullptr);
    return 1;
}

int l_line_count(lua_State* L) {
    const Buffer* b = need_buf(L, 1);
    lua_pushinteger(L, static_cast<lua_Integer>(b->pt.line_count()));
    return 1;
}

// winter.api.buf_get_lines(buf [, first=1 [, last=-1]]) -> {string...}
// last < 0 counts from the end (-1 = the last line).
int l_get_lines(lua_State* L) {
    const Buffer* b = need_buf(L, 1);
    const lua_Integer n = static_cast<lua_Integer>(b->pt.line_count());
    const lua_Integer first = std::max<lua_Integer>(luaL_optinteger(L, 2, 1), 1);
    lua_Integer last = luaL_optinteger(L, 3, -1);
    if(last < 0) last = n + 1 + last;
    last = std::min(last, n);
    lua_newtable(L);
    for(lua_Integer l = first; l <= last; ++l) {
        const std::string t = b->pt.line_text(static_cast<size_t>(l - 1));
        lua_pushlstring(L, t.data(), t.size());
        lua_rawseti(L, -2, l - first + 1);
    }
    return 1;
}

// winter.api.buf_set_lines(buf, first, last, {string...}): replace lines
// first..last with the list; last == first - 1 inserts before `first`, an empty
// list deletes. One undo step.
int l_set_lines(lua_State* L) {
    Buffer* b = need_buf(L, 1);
    const lua_Integer n = static_cast<lua_Integer>(b->pt.line_count());
    const lua_Integer first = luaL_checkinteger(L, 2);
    const lua_Integer last = luaL_checkinteger(L, 3);
    luaL_checktype(L, 4, LUA_TTABLE);
    luaL_argcheck(L, first >= 1 && first <= n + 1, 2, "line out of range");
    luaL_argcheck(L, last >= first - 1 && last <= n, 3, "line out of range");
    bool bad = false;
    {
        std::string text;
        lua_Integer cnt = 0;
        if(join_lines(L, 4, text, cnt)) apply_lines(*b, first, last, cnt, text);
        else bad = true;
    }
    if(bad) return luaL_argerror(L, 4, "expected a list of strings");
    return 0;
}

// winter.api.buf_get_text(buf, line1, col1, line2, col2) -> string   [start, end)
int l_get_text(lua_State* L) {
    const Buffer* b = need_buf(L, 1);
    const lua_Integer n = static_cast<lua_Integer>(b->pt.line_count());
    const lua_Integer l1 = luaL_checkinteger(L, 2), c1 = luaL_checkinteger(L, 3);
    const lua_Integer l2 = luaL_checkinteger(L, 4), c2 = luaL_checkinteger(L, 5);
    luaL_argcheck(L, l1 >= 1 && l1 <= n, 2, "line out of range");
    luaL_argcheck(L, l2 >= 1 && l2 <= n, 4, "line out of range");
    const size_t a = offset_in(b->pt, l1, c1), e = offset_in(b->pt, l2, c2);
    luaL_argcheck(L, e >= a, 4, "range ends before it starts");
    const std::string s = b->pt.text(a, e - a);
    lua_pushlstring(L, s.data(), s.size());
    return 1;
}

// winter.api.buf_set_text(buf, line1, col1, line2, col2, {string...}): replace
// [start, end) with the lines. One undo step.
int l_set_text(lua_State* L) {
    Buffer* b = need_buf(L, 1);
    const lua_Integer n = static_cast<lua_Integer>(b->pt.line_count());
    const lua_Integer l1 = luaL_checkinteger(L, 2), c1 = luaL_checkinteger(L, 3);
    const lua_Integer l2 = luaL_checkinteger(L, 4), c2 = luaL_checkinteger(L, 5);
    luaL_checktype(L, 6, LUA_TTABLE);
    luaL_argcheck(L, l1 >= 1 && l1 <= n, 2, "line out of range");
    luaL_argcheck(L, l2 >= 1 && l2 <= n, 4, "line out of range");
    const size_t a = offset_in(b->pt, l1, c1), e = offset_in(b->pt, l2, c2);
    luaL_argcheck(L, e >= a, 4, "range ends before it starts");
    bool bad = false;
    {
        std::string text;
        lua_Integer cnt = 0;
        if(join_lines(L, 6, text, cnt)) {
            std::unique_ptr<PieceTable::Group> group;
            if(!is_current(*b) || !insertish(E.mode))
                group = std::make_unique<PieceTable::Group>(b->pt);
            if(e > a) b->pt.remove(a, e - a);
            if(!text.empty()) b->pt.insert(a, text);
            group.reset();
            settle(*b);
        } else {
            bad = true;
        }
    }
    if(bad) return luaL_argerror(L, 6, "expected a list of strings");
    return 0;
}

int l_get_name(lua_State* L) {
    const Buffer* b = need_buf(L, 1);
    lua_pushlstring(L, b->filename.data(), b->filename.size());
    return 1;
}

// the text revision: changes with every edit, same number winter.buf tick uses
int l_changedtick(lua_State* L) {
    const Buffer* b = need_buf(L, 1);
    lua_pushinteger(L, static_cast<lua_Integer>(b->pt.revision()));
    return 1;
}

// winter.api.buf_get_cursor(buf) -> line, col. For a background buffer: the
// cursor it will have when entered.
int l_get_cursor(lua_State* L) {
    const Buffer* b = need_buf(L, 1);
    lua_pushinteger(L, (is_current(*b) ? E.cy : b->cy) + 1);
    lua_pushinteger(L, (is_current(*b) ? E.cx : b->cx) + 1);
    return 2;
}

int l_set_cursor(lua_State* L) {
    Buffer* b = need_buf(L, 1);
    const lua_Integer line = luaL_checkinteger(L, 2);
    const lua_Integer col = luaL_optinteger(L, 3, 1);
    const int n = static_cast<int>(b->pt.line_count());
    const int cy = static_cast<int>(std::clamp<lua_Integer>(line - 1, 0, n - 1));
    const std::string t = b->pt.line_text(static_cast<size_t>(cy));
    const int size = static_cast<int>(t.size());
    int cx = snap_cx(
        t, static_cast<int>(std::clamp<lua_Integer>(col - 1, 0, size)));
    const bool cur = is_current(*b);
    if(!(cur && insertish(E.mode)) && cx >= size && size > 0)
        cx = static_cast<int>(gr_prev(t, static_cast<size_t>(size)));
    if(cur) {
        E.cy = cy;
        E.cx = cx;
        update_want();
    } else {
        b->cy = cy;
        b->cx = cx;
    }
    return 0;
}

// ---- attach: change feed -------------------------------------------------------

struct Attach {
    int handle;
    uint64_t buf;
    LuaFn on_edit, on_close;
};
std::vector<Attach> g_att;
int g_next_handle = 1;
bool g_wired = false;

const char* origin_name(Origin o) {
    switch(o) {
    case Origin::User: return "user";
    case Origin::Undo: return "undo";
    case Origin::Redo: return "redo";
    case Origin::Snippet: return "snippet";
    case Origin::Lsp: return "lsp";
    case Origin::External: return "external";
    }
    return "user";
}

void set_int(lua_State* L, const char* k, lua_Integer v) {
    lua_pushinteger(L, v);
    lua_setfield(L, -2, k);
}

bool attached(uint64_t buf) {
    return std::ranges::any_of(g_att, [&](const Attach& a) {
        return a.buf == buf && a.on_edit;
    });
}

void deliver_edit(uint64_t buf, const EditEvent& ev) {
    std::vector<int> handles;  // callbacks may attach/detach: work on a snapshot
    for(const Attach& a : g_att)
        if(a.buf == buf && a.on_edit) handles.push_back(a.handle);
    for(const int h : handles) {
        const auto it = std::ranges::find(g_att, h, &Attach::handle);
        lua_State* L = script_state();
        if(it == g_att.end() || !L) continue;
        it->on_edit->push();
        lua_createtable(L, 0, 12);
        set_int(L, "buf", static_cast<lua_Integer>(buf));
        set_int(L, "tick", static_cast<lua_Integer>(ev.version));
        set_int(L, "offset", static_cast<lua_Integer>(ev.pos));  // 0-based byte
        set_int(L, "old_len", static_cast<lua_Integer>(ev.old_len));
        set_int(L, "new_len", static_cast<lua_Integer>(ev.new_len));
        set_int(L, "first_line", ev.start_pt.row + 1);
        set_int(L, "first_col", ev.start_pt.column + 1);
        set_int(L, "old_last_line", ev.old_end_pt.row + 1);  // in the old text
        set_int(L, "old_last_col", ev.old_end_pt.column + 1);
        set_int(L, "new_last_line", ev.new_end_pt.row + 1);  // in the new text
        set_int(L, "new_last_col", ev.new_end_pt.column + 1);
        lua_pushstring(L, origin_name(ev.origin));
        lua_setfield(L, -2, "origin");
        // like nvim: returning true detaches
        if(script_call(1, 1)) {
            const bool detach = lua_toboolean(L, -1) != 0;
            lua_pop(L, 1);
            if(detach)
                std::erase_if(g_att, [h](const Attach& a) { return a.handle == h; });
        }
    }
}

void wire_once() {
    if(g_wired) return;
    g_wired = true;
    // Edits are announced mid-mutation; callbacks run right after the dispatch
    // (edit_defer), when buffers may be touched again.
    edit_subscribe(prio::decor + 5, [](const EditEvent& ev) {
        if(g_att.empty()) return;
        for(const auto& b : E.buffers) {
            if(&b->pt != ev.src) continue;
            if(attached(b->id))
                edit_defer([id = b->id, ev] { deliver_edit(id, ev); });
            return;
        }
    });
    events_on("buf_close", [](const std::vector<std::string>&) {
        const Buffer* b = events_buf();
        if(!b) return;
        std::vector<Attach> gone;
        for(Attach& a : g_att)
            if(a.buf == b->id) gone.push_back(a);
        std::erase_if(g_att, [&](const Attach& a) { return a.buf == b->id; });
        lua_State* L = script_state();
        for(const Attach& a : gone) {
            if(!a.on_close || !L) continue;
            a.on_close->push();
            lua_pushinteger(L, static_cast<lua_Integer>(b->id));
            script_call(1);
        }
    });
}

// winter.api.buf_attach(buf, {on_edit=fn(ev), on_close=fn(buf)}) -> handle
// on_edit(ev) runs after each edit of the buffer with
//   {buf, tick, origin, offset, old_len, new_len,
//    first_line, first_col, old_last_line, old_last_col,
//    new_last_line, new_last_col}
// (the old_* end is a position in the text before the edit, new_* after it,
// both exclusive). Returning true detaches. on_close(buf) runs when the buffer
// is closed. Attachments are dropped on config reload.
int l_attach(lua_State* L) {
    const Buffer* b = need_buf(L, 1);
    luaL_checktype(L, 2, LUA_TTABLE);
    lua_getfield(L, 2, "on_edit");
    lua_getfield(L, 2, "on_close");
    const bool has_edit = lua_isfunction(L, -2), has_close = lua_isfunction(L, -1);
    const bool bad = (!lua_isnil(L, -2) && !has_edit) ||
                     (!lua_isnil(L, -1) && !has_close) ||
                     (!has_edit && !has_close);
    if(bad) return luaL_argerror(L, 2, "on_edit and/or on_close functions");
    Attach a{g_next_handle++, b->id, nullptr, nullptr};
    if(has_edit) a.on_edit = lua_fn(L, -2);
    if(has_close) a.on_close = lua_fn(L, -1);
    lua_pop(L, 2);
    g_att.push_back(std::move(a));
    wire_once();
    lua_pushinteger(L, g_att.back().handle);
    return 1;
}

int l_detach(lua_State* L) {
    const int h = static_cast<int>(luaL_checkinteger(L, 1));
    const size_t before = g_att.size();
    std::erase_if(g_att, [h](const Attach& a) { return a.handle == h; });
    lua_pushboolean(L, g_att.size() != before);
    return 1;
}

// ---- winter.reg ------------------------------------------------------------------

// winter.shell(cmd [, stdin]) -> output, exit code. BLOCKS the editor until the
// command ends: for short commands only, use winter.job.run for anything slow.
int l_shell(lua_State* L) {
    size_t n;
    const char* cmd = luaL_checklstring(L, 1, &n);
    ShellResult r;
    {
        const std::string c(cmd, n);
        if(lua_type(L, 2) == LUA_TSTRING) {
            size_t sn;
            const char* s = lua_tolstring(L, 2, &sn);
            const std::string in(s, sn);
            r = shell_run(c, &in);
        } else {
            r = shell_run(c, nullptr);
        }
    }
    lua_pushlstring(L, r.out.data(), r.out.size());
    lua_pushinteger(L, r.code);
    return 2;
}

const luaL_Reg kTop[] = {{"shell", l_shell}, {nullptr, nullptr}};
const luaL_Reg kBufId[] = {{"by_id", l_by_id}, {"id", l_id}, {nullptr, nullptr}};
const luaL_Reg kApi[] = {{"buf_valid", l_valid},
                         {"buf_line_count", l_line_count},
                         {"buf_get_lines", l_get_lines},
                         {"buf_set_lines", l_set_lines},
                         {"buf_get_text", l_get_text},
                         {"buf_set_text", l_set_text},
                         {"buf_get_name", l_get_name},
                         {"buf_changedtick", l_changedtick},
                         {"buf_get_cursor", l_get_cursor},
                         {"buf_set_cursor", l_set_cursor},
                         {"buf_attach", l_attach},
                         {"buf_detach", l_detach},
                         {nullptr, nullptr}};
}  // namespace

void script_api_reset() { g_att.clear(); }

void script_open_api() {
    script_register(nullptr, kTop);
    script_register("buf", kBufId);
    script_register("api", kApi);
}
